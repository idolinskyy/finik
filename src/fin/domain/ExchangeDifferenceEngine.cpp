#include "fin/domain/ExchangeDifferenceEngine.h"

#include "fin/core/Sql.h"
#include "fin/db/CurrencyRepository.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/Currency.h"

namespace fin {
namespace {

AccountId findAccount(const std::vector<Account> & accounts, const char * code)
{
  for (const Account & a : accounts) {
    if (a.code.compare(QLatin1String(code), Qt::CaseInsensitive) == 0) {
      return a.id;
    }
  }
  return {};
}

Status appendMoneyRevaluation(QSqlDatabase db, QDate through, AccountId accId,
                              AccountId gainAcc, AccountId lossAcc, const QString & label,
                              std::vector<DocumentLine> & lines, std::int64_t & exchangeGainNet,
                              std::int64_t & exchangeLossNet)
{
  if (!accId) {
    return Status::ok();
  }
  CurrencyRepository curRepo(db);
  sql::Query query(db);
  Status status = query.prepare(QStringLiteral(
      "SELECT currency_code, "
      "COALESCE(SUM(CASE WHEN debit_account_id = :a1 THEN currency_amount_cents ELSE 0 END), 0) "
      "- "
      "COALESCE(SUM(CASE WHEN credit_account_id = :a2 THEN currency_amount_cents ELSE 0 END), 0) "
      "AS foreign_net, "
      "COALESCE(SUM(CASE WHEN debit_account_id = :a3 THEN amount_cents ELSE 0 END), 0) - "
      "COALESCE(SUM(CASE WHEN credit_account_id = :a4 THEN amount_cents ELSE 0 END), 0) AS "
      "uah_net "
      "FROM ledger_entries "
      "WHERE date <= :through AND (debit_account_id = :a5 OR credit_account_id = :a6) "
      "  AND currency_code IS NOT NULL AND currency_code != '' AND currency_code != 'UAH' "
      "GROUP BY currency_code"));
  if (!status) {
    return status;
  }
  const qlonglong idVal = static_cast<qlonglong>(accId.value);
  query.bind(QStringLiteral(":a1"), idVal);
  query.bind(QStringLiteral(":a2"), idVal);
  query.bind(QStringLiteral(":a3"), idVal);
  query.bind(QStringLiteral(":a4"), idVal);
  query.bind(QStringLiteral(":a5"), idVal);
  query.bind(QStringLiteral(":a6"), idVal);
  query.bind(QStringLiteral(":through"), through.toString(Qt::ISODate));
  status = query.exec();
  if (!status) {
    return status;
  }
  while (query.next()) {
    const QString curCode = query.value(0).toString();
    const std::int64_t foreignNet = query.value(1).toLongLong();
    const std::int64_t uahNet = query.value(2).toLongLong();
    if (foreignNet == 0) {
      continue;
    }
    const Result<CurrencyRate> rateRes = curRepo.getRate(curCode, through);
    if (!rateRes) {
      return Status::fail(Error::validation(
          QStringLiteral("немає курсу НБУ для %1 на %2 — переоцінка %3 неможлива")
              .arg(curCode, through.toString(Qt::ISODate), label)));
    }
    const ExchangeRate rate(rateRes.value().rateMicros, rateRes.value().nominal);
    if (!rate.isValid()) {
      return Status::fail(Error::validation(
          QStringLiteral("некоректний курс НБУ для %1 на %2").arg(curCode,
                                                                  through.toString(Qt::ISODate))));
    }
    const Result<Money> targetUah = rate.toUah(Money::fromCents(foreignNet));
    if (!targetUah) {
      return targetUah.asStatus();
    }
    const std::int64_t diff = targetUah.value().cents() - uahNet;
    if (diff > 0) {
      DocumentLine line;
      line.debitAccountId = accId;
      line.creditAccountId = gainAcc;
      line.amount = Money::fromCents(diff);
      line.description =
          QStringLiteral("Курсова різниця по %1 (%2)").arg(label, curCode);
      lines.push_back(line);
      exchangeGainNet -= diff;
    } else if (diff < 0) {
      DocumentLine line;
      line.debitAccountId = lossAcc;
      line.creditAccountId = accId;
      line.amount = Money::fromCents(-diff);
      line.description =
          QStringLiteral("Курсова різниця по %1 (%2)").arg(label, curCode);
      lines.push_back(line);
      exchangeLossNet += -diff;
    }
  }
  return Status::ok();
}

// isAsset=true: 362 (дебіторка) — foreign_net = Dt − Ct валюти.
// isAsset=false: 632 (кредиторка) — foreign_net = Ct − Dt валюти.
Status appendSettlementRevaluation(QSqlDatabase db, QDate through, AccountId accId,
                                   AccountId gainAcc, AccountId lossAcc, bool isAsset,
                                   const QString & label, std::vector<DocumentLine> & lines,
                                   std::int64_t & exchangeGainNet, std::int64_t & exchangeLossNet)
{
  if (!accId) {
    return Status::ok();
  }
  CurrencyRepository curRepo(db);
  sql::Query query(db);
  const QString foreignExpr =
      isAsset
          ? QStringLiteral(
                "COALESCE(SUM(CASE WHEN debit_account_id = :a1 THEN currency_amount_cents ELSE 0 "
                "END), 0) - "
                "COALESCE(SUM(CASE WHEN credit_account_id = :a2 THEN currency_amount_cents ELSE 0 "
                "END), 0)")
          : QStringLiteral(
                "COALESCE(SUM(CASE WHEN credit_account_id = :a1 THEN currency_amount_cents ELSE 0 "
                "END), 0) - "
                "COALESCE(SUM(CASE WHEN debit_account_id = :a2 THEN currency_amount_cents ELSE 0 "
                "END), 0)");
  const QString uahExpr =
      isAsset ? QStringLiteral(
                    "COALESCE(SUM(CASE WHEN debit_account_id = :a3 THEN amount_cents ELSE 0 END), "
                    "0) - "
                    "COALESCE(SUM(CASE WHEN credit_account_id = :a4 THEN amount_cents ELSE 0 END), "
                    "0)")
              : QStringLiteral(
                    "COALESCE(SUM(CASE WHEN credit_account_id = :a3 THEN amount_cents ELSE 0 END), "
                    "0) - "
                    "COALESCE(SUM(CASE WHEN debit_account_id = :a4 THEN amount_cents ELSE 0 END), "
                    "0)");
  Status status = query.prepare(
      QStringLiteral("SELECT currency_code, counterparty_id, %1 AS foreign_net, %2 AS uah_net "
                     "FROM ledger_entries "
                     "WHERE date <= :through AND (debit_account_id = :a5 OR credit_account_id = :a6) "
                     "  AND currency_code IS NOT NULL AND currency_code != '' AND currency_code != "
                     "'UAH' "
                     "GROUP BY currency_code, counterparty_id")
          .arg(foreignExpr, uahExpr));
  if (!status) {
    return status;
  }
  const qlonglong idVal = static_cast<qlonglong>(accId.value);
  query.bind(QStringLiteral(":a1"), idVal);
  query.bind(QStringLiteral(":a2"), idVal);
  query.bind(QStringLiteral(":a3"), idVal);
  query.bind(QStringLiteral(":a4"), idVal);
  query.bind(QStringLiteral(":a5"), idVal);
  query.bind(QStringLiteral(":a6"), idVal);
  query.bind(QStringLiteral(":through"), through.toString(Qt::ISODate));
  status = query.exec();
  if (!status) {
    return status;
  }
  while (query.next()) {
    const QString curCode = query.value(0).toString();
    const std::int64_t cpRaw = query.value(1).toLongLong();
    const std::int64_t foreignNet = query.value(2).toLongLong();
    const std::int64_t uahNet = query.value(3).toLongLong();
    if (foreignNet <= 0) {
      continue;
    }
    if (cpRaw <= 0) {
      return Status::fail(Error::validation(
          QStringLiteral("валютний залишок %1 (%2) без контрагента — переоцінка неможлива")
              .arg(label, curCode)));
    }
    const CounterpartyId cp{cpRaw};
    const Result<CurrencyRate> rateRes = curRepo.getRate(curCode, through);
    if (!rateRes) {
      return Status::fail(Error::validation(
          QStringLiteral("немає курсу НБУ для %1 на %2 — переоцінка %3 неможлива")
              .arg(curCode, through.toString(Qt::ISODate), label)));
    }
    const ExchangeRate rate(rateRes.value().rateMicros, rateRes.value().nominal);
    if (!rate.isValid()) {
      return Status::fail(Error::validation(
          QStringLiteral("некоректний курс НБУ для %1 на %2").arg(curCode,
                                                                  through.toString(Qt::ISODate))));
    }
    const Result<Money> targetUah = rate.toUah(Money::fromCents(foreignNet));
    if (!targetUah) {
      return targetUah.asStatus();
    }
    const std::int64_t diff = targetUah.value().cents() - uahNet;
    if (isAsset) {
      if (diff > 0) {
        DocumentLine line;
        line.debitAccountId = accId;
        line.creditAccountId = gainAcc;
        line.amount = Money::fromCents(diff);
        line.counterpartyId = cp;
        line.description = QStringLiteral("Курсова різниця по %1 (%2)").arg(label, curCode);
        lines.push_back(line);
        exchangeGainNet -= diff;
      } else if (diff < 0) {
        DocumentLine line;
        line.debitAccountId = lossAcc;
        line.creditAccountId = accId;
        line.amount = Money::fromCents(-diff);
        line.counterpartyId = cp;
        line.description = QStringLiteral("Курсова різниця по %1 (%2)").arg(label, curCode);
        lines.push_back(line);
        exchangeLossNet += -diff;
      }
    } else {
      if (diff > 0) {
        DocumentLine line;
        line.debitAccountId = lossAcc;
        line.creditAccountId = accId;
        line.amount = Money::fromCents(diff);
        line.counterpartyId = cp;
        line.description = QStringLiteral("Курсова різниця по %1 (%2)").arg(label, curCode);
        lines.push_back(line);
        exchangeLossNet += diff;
      } else if (diff < 0) {
        DocumentLine line;
        line.debitAccountId = accId;
        line.creditAccountId = gainAcc;
        line.amount = Money::fromCents(-diff);
        line.counterpartyId = cp;
        line.description = QStringLiteral("Курсова різниця по %1 (%2)").arg(label, curCode);
        lines.push_back(line);
        exchangeGainNet -= -diff;
      }
    }
  }
  return Status::ok();
}

} // namespace

ExchangeDifferenceEngine::ExchangeDifferenceEngine(QSqlDatabase db) : db_(std::move(db)) {}

Status ExchangeDifferenceEngine::appendRevaluation(QDate through,
                                                   const std::vector<Account> & accounts,
                                                   std::vector<DocumentLine> & lines,
                                                   std::int64_t & exchangeGainNet,
                                                   std::int64_t & exchangeLossNet) const
{
  if (!through.isValid()) {
    return Status::fail(Error::invalidArgument(QStringLiteral("некоректна дата переоцінки")));
  }

  const AccountId gainAcc = findAccount(accounts, chart::kExchangeGain);
  const AccountId lossAcc = findAccount(accounts, chart::kExchangeLoss);
  if (!gainAcc || !lossAcc) {
    return Status::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 714 або 944 для курсових різниць")));
  }

  Status status = appendMoneyRevaluation(db_, through, findAccount(accounts, chart::kForeignBank),
                                         gainAcc, lossAcc, QStringLiteral("грошових коштах 312"),
                                         lines, exchangeGainNet, exchangeLossNet);
  if (!status) {
    return status;
  }
  status = appendMoneyRevaluation(db_, through, findAccount(accounts, chart::kForeignCash), gainAcc,
                                  lossAcc, QStringLiteral("грошових коштах 302"), lines,
                                  exchangeGainNet, exchangeLossNet);
  if (!status) {
    return status;
  }
  status = appendMoneyRevaluation(db_, through, findAccount(accounts, chart::kForeignTransit),
                                  gainAcc, lossAcc, QStringLiteral("коштах у дорозі 334"), lines,
                                  exchangeGainNet, exchangeLossNet);
  if (!status) {
    return status;
  }

  status = appendSettlementRevaluation(db_, through, findAccount(accounts, chart::kForeignCustomers),
                                       gainAcc, lossAcc, true, QStringLiteral("покупцях 362"), lines,
                                       exchangeGainNet, exchangeLossNet);
  if (!status) {
    return status;
  }
  return appendSettlementRevaluation(db_, through, findAccount(accounts, chart::kForeignSuppliers),
                                     gainAcc, lossAcc, false, QStringLiteral("постачальниках 632"),
                                     lines, exchangeGainNet, exchangeLossNet);
}

} // namespace fin
