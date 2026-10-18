#include "fin/domain/MonthClose.h"

#include "fin/core/Checked.h"
#include "fin/core/Sql.h"
#include "fin/db/AccountRepository.h"
#include "fin/db/DocumentRepository.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/ExchangeDifferenceEngine.h"

#include <limits>

namespace fin {
namespace {

Result<std::int64_t> accountNetThrough(QSqlDatabase db, AccountId id, QDate through)
{
  if (!id) {
    return Result<std::int64_t>::fail(
        Error::validation(QStringLiteral("немає рахунку для закриття")));
  }
  sql::Query query(db);
  Status status = query.prepare(QStringLiteral(
      "SELECT "
      "COALESCE(SUM(CASE WHEN debit_account_id = :d THEN amount_cents ELSE 0 END), 0) - "
      "COALESCE(SUM(CASE WHEN credit_account_id = :c THEN amount_cents ELSE 0 END), 0) "
      "FROM ledger_entries WHERE date <= :through"));
  if (!status) {
    return Result<std::int64_t>::fail(status.error());
  }
  const qlonglong key = static_cast<qlonglong>(id.value);
  query.bind(QStringLiteral(":d"), key);
  query.bind(QStringLiteral(":c"), key);
  query.bind(QStringLiteral(":through"), through.toString(Qt::ISODate));
  status = query.exec();
  if (!status) {
    return Result<std::int64_t>::fail(status.error());
  }
  std::int64_t net = 0;
  if (query.next()) {
    net = query.value(0).toLongLong();
  }
  return Result<std::int64_t>::ok(net);
}

DocumentLine moveToResult(AccountId account, AccountId result, std::int64_t net,
                          const QString & text)
{
  DocumentLine line;
  line.description = text;
  if (net > 0) {
    line.debitAccountId = result;
    line.creditAccountId = account;
    line.amount = Money::fromCents(net);
  } else {
    line.debitAccountId = account;
    line.creditAccountId = result;
    line.amount = Money::fromCents(-net);
  }
  return line;
}

Status appendClose(std::vector<DocumentLine> & lines, AccountId account, AccountId result,
                   std::int64_t net, const QString & text, std::int64_t & resultNet)
{
  if (net == 0) {
    return Status::ok();
  }
  if (net == std::numeric_limits<std::int64_t>::min()) {
    return Status::fail(Error::overflow(QStringLiteral("переповнення фінрезультату")));
  }
  lines.push_back(moveToResult(account, result, net, text));
  // Moving net N from account to 79: 79 debit if N>0, 79 credit if N<0.
  return addTo(resultNet, net, QStringLiteral("рахунок 79"));
}

} // namespace

Result<std::vector<DocumentLine>> composePeriodResult(QSqlDatabase db, QDate from, QDate to)
{
  if (!from.isValid() || !to.isValid() || from > to) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::invalidArgument(QStringLiteral("некоректний місяць фінрезультату")));
  }
  AccountRepository accounts(db);
  const Result<Account> revenue = accounts.findByCode(QString::fromLatin1(chart::kRevenue));
  const Result<Account> services = accounts.findByCode(QString::fromLatin1(chart::kServiceRevenue));
  const Result<Account> cogs = accounts.findByCode(QString::fromLatin1(chart::kCogs));
  const Result<Account> prodCogs = accounts.findByCode(QString::fromLatin1(chart::kProductionCogs));
  const Result<Account> overhead =
      accounts.findByCode(QString::fromLatin1(chart::kManufacturingOverhead));
  const Result<Account> production = accounts.findByCode(QString::fromLatin1(chart::kProduction));
  const Result<Account> admin = accounts.findByCode(QString::fromLatin1(chart::kAdminExpense));
  const Result<Account> other = accounts.findByCode(QString::fromLatin1(chart::kOtherIncome));
  const Result<Account> gain = accounts.findByCode(QString::fromLatin1(chart::kExchangeGain));
  const Result<Account> loss = accounts.findByCode(QString::fromLatin1(chart::kExchangeLoss));
  const Result<Account> result = accounts.findByCode(QString::fromLatin1(chart::kFinancialResult));
  const Result<Account> retained =
      accounts.findByCode(QString::fromLatin1(chart::kRetainedEarnings));
  if (!revenue || !services || !cogs || !admin || !other || !result || !retained) {
    return Result<std::vector<DocumentLine>>::fail(Error::validation(
        QStringLiteral("у плані рахунків немає 701, 703, 902, 92, 71, 79 або 44")));
  }

  std::vector<DocumentLine> lines;
  std::int64_t gainNet = 0;
  std::int64_t lossNet = 0;
  if (gain) {
    const Result<std::int64_t> g = accountNetThrough(db, gain.value().id, to);
    if (g) {
      gainNet = g.value();
    }
  }
  if (loss) {
    const Result<std::int64_t> l = accountNetThrough(db, loss.value().id, to);
    if (l) {
      lossNet = l.value();
    }
  }

  const Result<std::vector<Account>> allAccounts = accounts.listPostable();
  if (allAccounts) {
    Status revalStatus = ExchangeDifferenceEngine(db).appendRevaluation(
        to, allAccounts.value(), lines, gainNet, lossNet);
    if (!revalStatus) {
      return Result<std::vector<DocumentLine>>::fail(revalStatus.error());
    }
  }

  // Розподіл загальновиробничих витрат (91 -> 231)
  if (overhead && production) {
    const Result<std::int64_t> ohNet = accountNetThrough(db, overhead.value().id, to);
    if (ohNet && ohNet.value() > 0) {
      DocumentLine ohLine;
      ohLine.debitAccountId = production.value().id;
      ohLine.creditAccountId = overhead.value().id;
      ohLine.amount = Money::fromCents(ohNet.value());
      ohLine.description = QStringLiteral("розподіл загальновиробничих витрат (91 -> 231)");
      lines.push_back(ohLine);
    }
  }

  const Result<std::int64_t> revenueNet = accountNetThrough(db, revenue.value().id, to);
  const Result<std::int64_t> serviceNet = accountNetThrough(db, services.value().id, to);
  const Result<std::int64_t> cogsNet = accountNetThrough(db, cogs.value().id, to);
  std::int64_t prodCogsNet = 0;
  if (prodCogs) {
    const Result<std::int64_t> pc = accountNetThrough(db, prodCogs.value().id, to);
    if (pc) {
      prodCogsNet = pc.value();
    }
  }
  const Result<std::int64_t> adminNet = accountNetThrough(db, admin.value().id, to);
  const Result<std::int64_t> otherNet = accountNetThrough(db, other.value().id, to);
  const Result<std::int64_t> resultNetExisting = accountNetThrough(db, result.value().id, to);
  if (!revenueNet) {
    return Result<std::vector<DocumentLine>>::fail(revenueNet.error());
  }
  if (!serviceNet) {
    return Result<std::vector<DocumentLine>>::fail(serviceNet.error());
  }
  if (!cogsNet) {
    return Result<std::vector<DocumentLine>>::fail(cogsNet.error());
  }
  if (!adminNet) {
    return Result<std::vector<DocumentLine>>::fail(adminNet.error());
  }
  if (!otherNet) {
    return Result<std::vector<DocumentLine>>::fail(otherNet.error());
  }
  if (!resultNetExisting) {
    return Result<std::vector<DocumentLine>>::fail(resultNetExisting.error());
  }

  std::int64_t resultNet = resultNetExisting.value();
  Status status = appendClose(lines, revenue.value().id, result.value().id, revenueNet.value(),
                              QStringLiteral("закриття 701"), resultNet);
  if (!status) {
    return Result<std::vector<DocumentLine>>::fail(status.error());
  }
  status = appendClose(lines, services.value().id, result.value().id, serviceNet.value(),
                       QStringLiteral("закриття 703"), resultNet);
  if (!status) {
    return Result<std::vector<DocumentLine>>::fail(status.error());
  }
  status = appendClose(lines, other.value().id, result.value().id, otherNet.value(),
                       QStringLiteral("закриття 71"), resultNet);
  if (!status) {
    return Result<std::vector<DocumentLine>>::fail(status.error());
  }
  if (gain && gainNet != 0) {
    status = appendClose(lines, gain.value().id, result.value().id, gainNet,
                         QStringLiteral("закриття 714 (курсові різниці)"), resultNet);
    if (!status) {
      return Result<std::vector<DocumentLine>>::fail(status.error());
    }
  }
  status = appendClose(lines, cogs.value().id, result.value().id, cogsNet.value(),
                       QStringLiteral("закриття 902"), resultNet);
  if (!status) {
    return Result<std::vector<DocumentLine>>::fail(status.error());
  }
  if (prodCogs && prodCogsNet != 0) {
    status = appendClose(lines, prodCogs.value().id, result.value().id, prodCogsNet,
                         QStringLiteral("закриття 901"), resultNet);
    if (!status) {
      return Result<std::vector<DocumentLine>>::fail(status.error());
    }
  }
  status = appendClose(lines, admin.value().id, result.value().id, adminNet.value(),
                       QStringLiteral("закриття 92"), resultNet);
  if (!status) {
    return Result<std::vector<DocumentLine>>::fail(status.error());
  }
  if (loss && lossNet != 0) {
    status = appendClose(lines, loss.value().id, result.value().id, lossNet,
                         QStringLiteral("закриття 944 (курсові різниці)"), resultNet);
    if (!status) {
      return Result<std::vector<DocumentLine>>::fail(status.error());
    }
  }

  if (resultNet != 0) {
    lines.push_back(moveToResult(result.value().id, retained.value().id, resultNet,
                                 resultNet > 0 ? QStringLiteral("збиток на 44")
                                               : QStringLiteral("прибуток на 44")));
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("рахунки доходів і витрат уже закриті")));
  }
  (void)from;
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Status requireIncomeExpenseClosed(QSqlDatabase db, QDate through)
{
  AccountRepository accounts(db);
  const char * codes[] = {
      chart::kRevenue,        chart::kServiceRevenue,        chart::kCogs,
      chart::kProductionCogs, chart::kManufacturingOverhead, chart::kAdminExpense,
      chart::kOtherIncome,    chart::kExchangeGain,          chart::kExchangeLoss,
      chart::kFinancialResult};
  for (const char * code : codes) {
    const Result<Account> account = accounts.findByCode(QString::fromLatin1(code));
    if (!account) {
      continue;
    }
    const Result<std::int64_t> net = accountNetThrough(db, account.value().id, through);
    if (!net) {
      return net.asStatus();
    }
    if (net.value() != 0) {
      return Status::fail(Error::conflict(QStringLiteral("спочатку сформуйте фінрезультат місяця "
                                                         "(рахунок %1 ще не закрито)")
                                              .arg(QString::fromLatin1(code))));
    }
  }
  return Status::ok();
}

Result<Document> buildPeriodResultDocument(QSqlDatabase db, int year, int month)
{
  if (year < 1991 || year > 2100 || month < 1 || month > 12) {
    return Result<Document>::fail(
        Error::validation(QStringLiteral("некоректний місяць фінрезультату")));
  }
  const QDate from(year, month, 1);
  const QDate to = from.addMonths(1).addDays(-1);
  DocumentRepository documents(db);
  const Result<std::vector<DocumentHeader>> existing =
      documents.list(from, to, DocumentType::PeriodResult);
  if (!existing) {
    return Result<Document>::fail(existing.error());
  }
  for (const DocumentHeader & header : existing.value()) {
    if (header.posted) {
      return Result<Document>::fail(
          Error::conflict(QStringLiteral("фінрезультат за %1 уже проведено (%2)")
                              .arg(to.toString(QStringLiteral("MM.yyyy")), header.number)));
    }
  }
  const Result<std::vector<DocumentLine>> lines = composePeriodResult(db, from, to);
  if (!lines) {
    return Result<Document>::fail(lines.error());
  }
  Document document;
  document.type = DocumentType::PeriodResult;
  document.date = to;
  document.comment =
      QStringLiteral("закриття доходів і витрат за %1").arg(to.toString(QStringLiteral("MM.yyyy")));
  document.lines = lines.value();
  if (!existing.value().empty()) {
    const Result<Document> draft = documents.findById(existing.value().front().id);
    if (!draft) {
      return Result<Document>::fail(draft.error());
    }
    Document updated = draft.value();
    updated.date = to;
    updated.comment = document.comment;
    updated.lines = document.lines;
    return Result<Document>::ok(std::move(updated));
  }
  const Result<QString> number = documents.allocateNumber(DocumentType::PeriodResult);
  if (!number) {
    return Result<Document>::fail(number.error());
  }
  document.number = number.value();
  return Result<Document>::ok(std::move(document));
}

} // namespace fin
