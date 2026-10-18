#include "fin/posting/AdvanceOffset.h"

#include "fin/core/Sql.h"
#include "fin/db/AccountRepository.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/Currency.h"
#include "fin/domain/DocumentBlueprint.h"

#include <QDate>

namespace fin {
namespace {

bool isCode(const std::vector<Account> & accounts, AccountId id, const char * code)
{
  return accountHasCode(accounts, id, code);
}

DocumentLine offsetPosting(AccountId debit, AccountId credit, Money amount)
{
  DocumentLine line;
  line.debitAccountId = debit;
  line.creditAccountId = credit;
  line.amount = amount;
  line.description = QStringLiteral("залік авансу");
  return line;
}

Result<std::int64_t> netDebit(QSqlDatabase db, AccountId accountId, CounterpartyId counterparty,
                              QDate date)
{
  sql::Query query(db);
  Status status = query.prepare(QStringLiteral(
      "SELECT "
      "COALESCE(SUM(CASE WHEN debit_account_id = :a1 THEN amount_cents ELSE 0 END), 0) - "
      "COALESCE(SUM(CASE WHEN credit_account_id = :a2 THEN amount_cents ELSE 0 END), 0) "
      "FROM ledger_entries "
      "WHERE counterparty_id = :cp AND date <= :date "
      "AND (debit_account_id = :a3 OR credit_account_id = :a4)"));
  if (!status) {
    return Result<std::int64_t>::fail(status.error());
  }
  const qlonglong id = static_cast<qlonglong>(accountId.value);
  query.bind(QStringLiteral(":a1"), id);
  query.bind(QStringLiteral(":a2"), id);
  query.bind(QStringLiteral(":a3"), id);
  query.bind(QStringLiteral(":a4"), id);
  query.bind(QStringLiteral(":cp"), static_cast<qlonglong>(counterparty.value));
  query.bind(QStringLiteral(":date"), date.toString(Qt::ISODate));
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

Money receivableOf(const std::vector<Account> & accounts, DocumentType type,
                   const std::vector<DocumentLine> & lines)
{
  Money total = Money::zero();
  const char * revenue =
      type == DocumentType::ServiceSale ? chart::kServiceRevenue : chart::kRevenue;
  for (const DocumentLine & line : lines) {
    if (!isCode(accounts, line.debitAccountId, chart::kCustomers) &&
        !isCode(accounts, line.debitAccountId, chart::kForeignCustomers)) {
      continue;
    }
    if (!isCode(accounts, line.creditAccountId, revenue)) {
      continue;
    }
    const Result<Money> next = total.plus(line.amount);
    if (!next) {
      return total;
    }
    total = next.value();
  }
  return total;
}

Money payableOf(const std::vector<Account> & accounts, DocumentType type,
                const std::vector<DocumentLine> & lines)
{
  Money total = Money::zero();
  for (const DocumentLine & line : lines) {
    if (!isCode(accounts, line.creditAccountId, chart::kSuppliers) &&
        !isCode(accounts, line.creditAccountId, chart::kForeignSuppliers)) {
      continue;
    }
    const bool stockDebit = isCode(accounts, line.debitAccountId, chart::kGoods) ||
                            isCode(accounts, line.debitAccountId, chart::kRawMaterials) ||
                            isCode(accounts, line.debitAccountId, chart::kPurchasedComponents) ||
                            isCode(accounts, line.debitAccountId, chart::kFuel);
    bool serviceDebit = false;
    if (type == DocumentType::ServicePurchase) {
      for (const Account & account : accounts) {
        if (account.id == line.debitAccountId) {
          serviceDebit = isServicePurchaseDebitCode(account.code);
          break;
        }
      }
    }
    if ((type == DocumentType::Purchase && !stockDebit) ||
        (type == DocumentType::ServicePurchase && !serviceDebit)) {
      continue;
    }
    if (type != DocumentType::Purchase && type != DocumentType::ServicePurchase) {
      continue;
    }
    const Result<Money> next = total.plus(line.amount);
    if (!next) {
      return total;
    }
    total = next.value();
  }
  return total;
}

// Порівнює сальдо авансу в ГЛ (завжди UAH) з сумою документа (валюта документа)
// і повертає суму заліку у валюті рядків документа.
Result<Money> offsetInDocumentCurrency(Money documentAmount, std::int64_t balanceUahCents,
                                       bool isForeign, const ExchangeRate & rate)
{
  if (!documentAmount.isPositive() || balanceUahCents <= 0) {
    return Result<Money>::ok(Money::zero());
  }
  if (!isForeign) {
    const std::int64_t offset =
        balanceUahCents < documentAmount.cents() ? balanceUahCents : documentAmount.cents();
    return Result<Money>::ok(Money::fromCents(offset));
  }
  if (!rate.isValid()) {
    return Result<Money>::fail(
        Error::validation(QStringLiteral("для валютного заліку авансу потрібен курс документа")));
  }
  const Result<Money> docUah = rate.toUah(documentAmount);
  if (!docUah) {
    return Result<Money>::fail(docUah.error());
  }
  const std::int64_t offsetUah =
      balanceUahCents < docUah.value().cents() ? balanceUahCents : docUah.value().cents();
  if (offsetUah <= 0) {
    return Result<Money>::ok(Money::zero());
  }
  return rate.toForeign(Money::fromCents(offsetUah));
}

} // namespace

bool isAdvanceOffsetLine(const std::vector<Account> & accounts, DocumentType type,
                         const DocumentLine & line)
{
  if (type == DocumentType::Sale || type == DocumentType::ServiceSale) {
    const bool uah = isCode(accounts, line.debitAccountId, chart::kAdvancesReceived) &&
                     isCode(accounts, line.creditAccountId, chart::kCustomers);
    const bool foreign = isCode(accounts, line.debitAccountId, chart::kForeignAdvancesReceived) &&
                         isCode(accounts, line.creditAccountId, chart::kForeignCustomers);
    return uah || foreign;
  }
  if (type == DocumentType::Purchase || type == DocumentType::ServicePurchase) {
    const bool uah = isCode(accounts, line.debitAccountId, chart::kSuppliers) &&
                     isCode(accounts, line.creditAccountId, chart::kAdvancesIssued);
    const bool foreign = isCode(accounts, line.debitAccountId, chart::kForeignSuppliers) &&
                         isCode(accounts, line.creditAccountId, chart::kForeignAdvancesIssued);
    return uah || foreign;
  }
  return false;
}

Money advanceOffsetAmount(const std::vector<Account> & accounts, DocumentType type,
                          const std::vector<DocumentLine> & lines)
{
  Money total = Money::zero();
  for (const DocumentLine & line : lines) {
    if (!isAdvanceOffsetLine(accounts, type, line)) {
      continue;
    }
    const Result<Money> next = total.plus(line.amount);
    if (!next) {
      return total;
    }
    total = next.value();
  }
  return total;
}

void stripAdvanceOffsetLines(const std::vector<Account> & accounts, Document & document)
{
  std::vector<DocumentLine> kept;
  kept.reserve(document.lines.size());
  for (const DocumentLine & line : document.lines) {
    if (!isAdvanceOffsetLine(accounts, document.type, line)) {
      kept.push_back(line);
    }
  }
  document.lines = std::move(kept);
}

Status applyAdvanceOffset(QSqlDatabase db, const std::vector<Account> & accounts,
                          Document & document)
{
  stripAdvanceOffsetLines(accounts, document);
  if (!document.counterpartyId) {
    return Status::ok();
  }
  if (document.type != DocumentType::Sale && document.type != DocumentType::ServiceSale &&
      document.type != DocumentType::Purchase && document.type != DocumentType::ServicePurchase) {
    return Status::ok();
  }

  const bool isForeign =
      !document.currencyCode.isEmpty() && document.currencyCode != QLatin1String("UAH");
  ExchangeRate docRate;
  if (isForeign) {
    if (document.currencyRateMicros <= 0) {
      return Status::fail(
          Error::validation(QStringLiteral("для валютного заліку авансу потрібен курс документа")));
    }
    docRate = ExchangeRate(document.currencyRateMicros, document.currencyNominal);
  }

  AccountRepository repo(db);
  if (document.type == DocumentType::Sale || document.type == DocumentType::ServiceSale) {
    const char * advCode = isForeign ? chart::kForeignAdvancesReceived : chart::kAdvancesReceived;
    const char * custCode = isForeign ? chart::kForeignCustomers : chart::kCustomers;
    const Result<Account> received = repo.findByCode(QString::fromLatin1(advCode));
    if (!received) {
      return Status::ok();
    }
    const Money receivable = receivableOf(accounts, document.type, document.lines);
    if (!receivable.isPositive()) {
      return Status::ok();
    }
    const Result<std::int64_t> net =
        netDebit(db, received.value().id, document.counterpartyId, document.date);
    if (!net) {
      return net.asStatus();
    }
    const std::int64_t creditBalance = -net.value();
    const Result<Money> offset =
        offsetInDocumentCurrency(receivable, creditBalance, isForeign, docRate);
    if (!offset) {
      return offset.asStatus();
    }
    if (!offset.value().isPositive()) {
      return Status::ok();
    }
    const AccountId customers = accountIdByCode(accounts, custCode);
    if (!customers) {
      return Status::ok();
    }
    document.lines.push_back(offsetPosting(received.value().id, customers, offset.value()));
    return Status::ok();
  }

  const char * advCode = isForeign ? chart::kForeignAdvancesIssued : chart::kAdvancesIssued;
  const char * suppCode = isForeign ? chart::kForeignSuppliers : chart::kSuppliers;
  const Result<Account> issued = repo.findByCode(QString::fromLatin1(advCode));
  if (!issued) {
    return Status::ok();
  }
  const Money payable = payableOf(accounts, document.type, document.lines);
  if (!payable.isPositive()) {
    return Status::ok();
  }
  const Result<std::int64_t> net =
      netDebit(db, issued.value().id, document.counterpartyId, document.date);
  if (!net) {
    return net.asStatus();
  }
  const Result<Money> offset = offsetInDocumentCurrency(payable, net.value(), isForeign, docRate);
  if (!offset) {
    return offset.asStatus();
  }
  if (!offset.value().isPositive()) {
    return Status::ok();
  }
  const AccountId suppliers = accountIdByCode(accounts, suppCode);
  if (!suppliers) {
    return Status::ok();
  }
  document.lines.push_back(offsetPosting(suppliers, issued.value().id, offset.value()));
  return Status::ok();
}

} // namespace fin
