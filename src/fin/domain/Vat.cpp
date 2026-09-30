#include "fin/domain/Vat.h"

#include "fin/core/Checked.h"
#include "fin/core/Sql.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/DocumentBlueprint.h"

#include <QHash>
#include <QLatin1String>
#include <QVariant>

namespace fin {
namespace {

bool codeIs(const std::vector<Account> & accounts, AccountId id, const char * code)
{
  return accountHasCode(accounts, id, code);
}

const char * settlementCode(VatDirection direction, bool advance)
{
  if (direction == VatDirection::Output) {
    return advance ? chart::kAdvancesReceived : chart::kCustomers;
  }
  return advance ? chart::kAdvancesIssued : chart::kSuppliers;
}

Status requireRate(int rateBp)
{
  if (rateBp < 0 || rateBp > 10000) {
    return Status::fail(Error::validation(QStringLiteral("ставка ПДВ має бути від 0 до 100%")));
  }
  return Status::ok();
}

Result<Money> sourceVat(const Document & source, const std::vector<Account> & accounts, int rateBp)
{
  Status rate = requireRate(rateBp);
  if (!rate) {
    return Result<Money>::fail(rate.error());
  }

  std::int64_t stored = 0;
  std::int64_t net = 0;
  std::int64_t gross = 0;
  bool advanceGross = isVatAdvanceSource(source.type);

  for (const DocumentLine & line : source.lines) {
    if (isVatRegistrationLine(accounts, line) || isVatObligationLine(accounts, line)) {
      continue;
    }
    if (advanceGross) {
      const bool outputAdvance = codeIs(accounts, line.creditAccountId, chart::kAdvancesReceived);
      const bool inputAdvance = codeIs(accounts, line.debitAccountId, chart::kAdvancesIssued);
      if (!outputAdvance && !inputAdvance) {
        continue;
      }
      Status add = addTo(gross, line.amount.cents(), QStringLiteral("ПДВ авансу"));
      if (!add) {
        return Result<Money>::fail(add.error());
      }
      continue;
    }

    bool commercial = false;
    switch (source.type) {
    case DocumentType::Sale:
    case DocumentType::CustomerInvoice:
      commercial = codeIs(accounts, line.debitAccountId, chart::kCustomers) &&
                   codeIs(accounts, line.creditAccountId, chart::kRevenue);
      break;
    case DocumentType::ServiceSale:
      commercial = codeIs(accounts, line.debitAccountId, chart::kCustomers) &&
                   codeIs(accounts, line.creditAccountId, chart::kServiceRevenue);
      break;
    case DocumentType::Purchase:
    case DocumentType::MsReceipt:
      commercial = codeIs(accounts, line.creditAccountId, chart::kSuppliers);
      break;
    case DocumentType::ServicePurchase:
      commercial = codeIs(accounts, line.creditAccountId, chart::kSuppliers);
      break;
    case DocumentType::SaleReturn:
      commercial = codeIs(accounts, line.debitAccountId, chart::kRevenue) &&
                   codeIs(accounts, line.creditAccountId, chart::kCustomers);
      break;
    case DocumentType::PurchaseReturn:
      commercial = codeIs(accounts, line.debitAccountId, chart::kSuppliers) &&
                   codeIs(accounts, line.creditAccountId, chart::kGoods);
      break;
    case DocumentType::FaCommission:
    case DocumentType::IaCommission:
      commercial = codeIs(accounts, line.creditAccountId, chart::kSuppliers);
      break;
    default:
      break;
    }
    if (!commercial) {
      continue;
    }
    Status add = addTo(stored, line.vatCents, QStringLiteral("ПДВ рядка"));
    if (!add) {
      return Result<Money>::fail(add.error());
    }
    add = addTo(net, line.amount.cents(), QStringLiteral("база ПДВ"));
    if (!add) {
      return Result<Money>::fail(add.error());
    }
  }

  if (advanceGross) {
    if (gross <= 0) {
      return Result<Money>::fail(Error::validation(
          QStringLiteral("податкова накладна з оплати можлива лише для авансу 681 або 371")));
    }
    return vatIncludedInGross(Money::fromCents(gross), rateBp);
  }
  if (stored > 0) {
    return Result<Money>::ok(Money::fromCents(stored));
  }
  if (net <= 0) {
    return Result<Money>::fail(
        Error::validation(QStringLiteral("у вихідному документі немає бази для ПДВ")));
  }
  return vatOnNet(Money::fromCents(net), rateBp);
}

struct PostedPn
{
  bool advance = false;
  bool output = false;
  std::int64_t obligation = 0;
  std::int64_t offset = 0;
};

Result<std::vector<PostedPn>> loadPostedTaxInvoices(QSqlDatabase db, CounterpartyId party,
                                                    DocumentId excludeId)
{
  sql::Query query(db);
  Status status =
      query.prepare(QStringLiteral("SELECT d.id, d.vat_offset_cents, COALESCE(s.type, ''), "
                                   "l.amount_cents, da.code, ca.code "
                                   "FROM documents d "
                                   "JOIN document_lines l ON l.document_id = d.id "
                                   "JOIN accounts da ON da.id = l.debit_account_id "
                                   "JOIN accounts ca ON ca.id = l.credit_account_id "
                                   "LEFT JOIN documents s ON s.id = d.based_on_id "
                                   "WHERE d.type = 'tax_invoice' AND d.posted = 1 "
                                   "AND d.counterparty_id = :cp AND d.id != :ex"));
  if (!status) {
    return Result<std::vector<PostedPn>>::fail(status.error());
  }
  query.bind(QStringLiteral(":cp"), static_cast<qlonglong>(party.value));
  query.bind(QStringLiteral(":ex"), static_cast<qlonglong>(excludeId.value));
  status = query.exec();
  if (!status) {
    return Result<std::vector<PostedPn>>::fail(status.error());
  }

  std::vector<PostedPn> rows;
  QHash<qlonglong, int> index;
  while (query.next()) {
    const qlonglong id = query.value(0).toLongLong();
    const auto sourceType = documentTypeFromString(query.value(2).toString());
    const QString debit = query.value(4).toString();
    const QString credit = query.value(5).toString();
    const bool output =
        credit.compare(QLatin1String(chart::kVatLiability), Qt::CaseInsensitive) == 0 ||
        debit.compare(QLatin1String(chart::kVatLiability), Qt::CaseInsensitive) == 0;
    const bool input = debit.compare(QLatin1String(chart::kVatCredit), Qt::CaseInsensitive) == 0 ||
                       credit.compare(QLatin1String(chart::kVatCredit), Qt::CaseInsensitive) == 0;
    const bool budget = debit.compare(QLatin1String(chart::kVatBudget), Qt::CaseInsensitive) == 0 ||
                        credit.compare(QLatin1String(chart::kVatBudget), Qt::CaseInsensitive) == 0;
    if (budget || (!output && !input)) {
      continue;
    }
    auto it = index.find(id);
    if (it == index.end()) {
      PostedPn row;
      row.offset = query.value(1).toLongLong();
      row.advance = sourceType && isVatAdvanceSource(*sourceType);
      row.output = output;
      index.insert(id, static_cast<int>(rows.size()));
      rows.push_back(row);
      it = index.find(id);
    }
    Status add = addTo(rows[it.value()].obligation, query.value(3).toLongLong(),
                       QStringLiteral("ПДВ податкової"));
    if (!add) {
      return Result<std::vector<PostedPn>>::fail(add.error());
    }
  }
  return Result<std::vector<PostedPn>>::ok(std::move(rows));
}

Result<Money> firstEventVat(QSqlDatabase db, CounterpartyId party, DocumentId excludeId,
                            VatDirection direction, bool thisIsAdvance, Money sourceVatAmount)
{
  const Result<std::vector<PostedPn>> posted = loadPostedTaxInvoices(db, party, excludeId);
  if (!posted) {
    return Result<Money>::fail(posted.error());
  }
  std::int64_t advanceObligation = 0;
  std::int64_t shipmentObligation = 0;
  std::int64_t shipmentOffset = 0;
  std::int64_t advanceOffset = 0;
  const bool output = direction == VatDirection::Output;
  for (const PostedPn & row : posted.value()) {
    if (row.output != output) {
      continue;
    }
    if (row.advance) {
      Status add = addTo(advanceObligation, row.obligation, QStringLiteral("авансовий ПДВ"));
      if (!add) {
        return Result<Money>::fail(add.error());
      }
      add = addTo(advanceOffset, row.offset, QStringLiteral("залік авансового ПДВ"));
      if (!add) {
        return Result<Money>::fail(add.error());
      }
    } else {
      Status add = addTo(shipmentObligation, row.obligation, QStringLiteral("ПДВ відвантаження"));
      if (!add) {
        return Result<Money>::fail(add.error());
      }
      add = addTo(shipmentOffset, row.offset, QStringLiteral("залік ПДВ відвантаження"));
      if (!add) {
        return Result<Money>::fail(add.error());
      }
    }
  }

  std::int64_t unallocated = 0;
  if (thisIsAdvance) {
    unallocated = (shipmentObligation + shipmentOffset) - (advanceObligation + advanceOffset);
  } else {
    unallocated = advanceObligation - shipmentOffset;
  }
  if (unallocated < 0) {
    unallocated = 0;
  }
  const std::int64_t offset =
      sourceVatAmount.cents() < unallocated ? sourceVatAmount.cents() : unallocated;
  return Result<Money>::ok(Money::fromCents(sourceVatAmount.cents() - offset));
}

DocumentLine posting(AccountId debit, AccountId credit, Money amount, const QString & description)
{
  DocumentLine line;
  line.debitAccountId = debit;
  line.creditAccountId = credit;
  line.amount = amount;
  line.description = description;
  return line;
}

} // namespace

bool usesCommercialVat(DocumentType type)
{
  return type == DocumentType::Purchase || type == DocumentType::Sale ||
         type == DocumentType::SaleReturn || type == DocumentType::PurchaseReturn ||
         type == DocumentType::ServiceSale || type == DocumentType::ServicePurchase ||
         type == DocumentType::CustomerInvoice || type == DocumentType::MsReceipt ||
         type == DocumentType::FaCommission || type == DocumentType::IaCommission;
}

bool usesTaxInvoiceForm(DocumentType type)
{
  return type == DocumentType::TaxInvoice;
}

bool isVatAdvanceSource(DocumentType type)
{
  return type == DocumentType::CashIn || type == DocumentType::CashOut ||
         type == DocumentType::BankPayment;
}

bool isVatReturnSource(DocumentType type)
{
  return type == DocumentType::SaleReturn || type == DocumentType::PurchaseReturn;
}

std::optional<VatDirection> vatDirectionFromSource(DocumentType type)
{
  switch (type) {
  case DocumentType::Sale:
  case DocumentType::ServiceSale:
  case DocumentType::CustomerInvoice:
  case DocumentType::SaleReturn:
  case DocumentType::CashIn:
    return VatDirection::Output;
  case DocumentType::Purchase:
  case DocumentType::ServicePurchase:
  case DocumentType::PurchaseReturn:
  case DocumentType::MsReceipt:
  case DocumentType::FaCommission:
  case DocumentType::IaCommission:
  case DocumentType::CashOut:
    return VatDirection::Input;
  case DocumentType::BankPayment:
    return std::nullopt;
  default:
    return std::nullopt;
  }
}

bool isVatRegistrationLine(const std::vector<Account> & accounts, const DocumentLine & line)
{
  const bool debitBudget = codeIs(accounts, line.debitAccountId, chart::kVatBudget);
  const bool creditBudget = codeIs(accounts, line.creditAccountId, chart::kVatBudget);
  const bool debitLiab = codeIs(accounts, line.debitAccountId, chart::kVatLiability);
  const bool creditLiab = codeIs(accounts, line.creditAccountId, chart::kVatLiability);
  const bool debitCredit = codeIs(accounts, line.debitAccountId, chart::kVatCredit);
  const bool creditCredit = codeIs(accounts, line.creditAccountId, chart::kVatCredit);
  return (debitLiab && creditBudget) || (debitBudget && creditCredit) ||
         (debitBudget && creditLiab) || (debitCredit && creditBudget);
}

bool isVatObligationLine(const std::vector<Account> & accounts, const DocumentLine & line)
{
  const bool debitLiab = codeIs(accounts, line.debitAccountId, chart::kVatLiability);
  const bool creditLiab = codeIs(accounts, line.creditAccountId, chart::kVatLiability);
  const bool debitCredit = codeIs(accounts, line.debitAccountId, chart::kVatCredit);
  const bool creditCredit = codeIs(accounts, line.creditAccountId, chart::kVatCredit);
  if (isVatRegistrationLine(accounts, line)) {
    return false;
  }
  return debitLiab || creditLiab || debitCredit || creditCredit;
}

void stripVatRegistrationLines(const std::vector<Account> & accounts, Document & document)
{
  std::vector<DocumentLine> kept;
  kept.reserve(document.lines.size());
  for (const DocumentLine & line : document.lines) {
    if (!isVatRegistrationLine(accounts, line)) {
      kept.push_back(line);
    }
  }
  document.lines = std::move(kept);
}

Result<bool> hasPostedTaxInvoiceBasedOn(QSqlDatabase db, DocumentId sourceId, DocumentId excludeId)
{
  if (!sourceId) {
    return Result<bool>::ok(false);
  }
  sql::Query query(db);
  Status status = query.prepare(
      QStringLiteral("SELECT id FROM documents WHERE type = 'tax_invoice' AND posted = 1 "
                     "AND based_on_id = :src AND id != :ex LIMIT 1"));
  if (!status) {
    return Result<bool>::fail(status.error());
  }
  query.bind(QStringLiteral(":src"), static_cast<qlonglong>(sourceId.value));
  query.bind(QStringLiteral(":ex"), static_cast<qlonglong>(excludeId.value));
  status = query.exec();
  if (!status) {
    return Result<bool>::fail(status.error());
  }
  return Result<bool>::ok(query.next());
}

Result<Money> vatOnNet(Money net, int rateBp)
{
  Status rate = requireRate(rateBp);
  if (!rate) {
    return Result<Money>::fail(rate.error());
  }
  if (net.isNegative()) {
    return Result<Money>::fail(
        Error::invalidArgument(QStringLiteral("база ПДВ не може бути від'ємною")));
  }
  const Result<std::int64_t> cents = mulDivRound(net.cents(), rateBp, 10000);
  if (!cents) {
    return Result<Money>::fail(cents.error());
  }
  return Result<Money>::ok(Money::fromCents(cents.value()));
}

Result<Money> vatIncludedInGross(Money gross, int rateBp)
{
  Status rate = requireRate(rateBp);
  if (!rate) {
    return Result<Money>::fail(rate.error());
  }
  if (gross.isNegative()) {
    return Result<Money>::fail(
        Error::invalidArgument(QStringLiteral("сума з ПДВ не може бути від'ємною")));
  }
  if (rateBp == 0) {
    return Result<Money>::ok(Money::zero());
  }
  const Result<Money> net = netFromGross(gross, rateBp);
  if (!net) {
    return net;
  }
  return gross.minus(net.value());
}

Result<Money> netFromGross(Money gross, int rateBp)
{
  Status rate = requireRate(rateBp);
  if (!rate) {
    return Result<Money>::fail(rate.error());
  }
  if (gross.isNegative()) {
    return Result<Money>::fail(
        Error::invalidArgument(QStringLiteral("сума з ПДВ не може бути від'ємною")));
  }
  const Result<std::int64_t> cents =
      mulDivRound(gross.cents(), 10000, 10000 + static_cast<std::int64_t>(rateBp));
  if (!cents) {
    return Result<Money>::fail(cents.error());
  }
  return Result<Money>::ok(Money::fromCents(cents.value()));
}

Result<Money> grossFromNet(Money net, int rateBp)
{
  const Result<Money> vat = vatOnNet(net, rateBp);
  if (!vat) {
    return vat;
  }
  return net.plus(vat.value());
}

Status stampVatOnNet(DocumentLine & line, int rateBp)
{
  if (line.vatCents > 0 && line.vatRateBp == 0) {
    line.vatRateBp = rateBp;
  }
  if (line.vatCents > 0) {
    const Result<Money> expected = vatOnNet(line.amount, line.vatRateBp);
    if (!expected) {
      return expected.asStatus();
    }
    if (expected.value().cents() != line.vatCents) {
      return Status::fail(
          Error::validation(QStringLiteral("ПДВ рядка має дорівнювати сумі × ставці")));
    }
    return Status::ok();
  }
  const Result<Money> vat = vatOnNet(line.amount, rateBp);
  if (!vat) {
    return vat.asStatus();
  }
  line.vatRateBp = rateBp;
  line.vatCents = vat.value().cents();
  return Status::ok();
}

Status stampSupplierVat(std::vector<DocumentLine> & lines, const std::vector<Account> & accounts,
                        int rateBp)
{
  // Викликається лише для платника ПДВ: нульова ставка означає незаповнену
  // картку підприємства, а не операцію без податку.
  if (rateBp <= 0) {
    return Status::fail(
        Error::validation(QStringLiteral("у картці підприємства не задано базову ставку ПДВ")));
  }
  for (DocumentLine & line : lines) {
    if (!codeIs(accounts, line.creditAccountId, chart::kSuppliers)) {
      continue;
    }
    Status status = stampVatOnNet(line, rateBp);
    if (!status) {
      return status;
    }
  }
  return Status::ok();
}

Money goodsVatTotal(const std::vector<GoodsLine> & rows)
{
  Money acc = Money::zero();
  for (const GoodsLine & row : rows) {
    const Result<Money> next = acc.plus(Money::fromCents(row.vatCents));
    if (!next) {
      return acc;
    }
    acc = next.value();
  }
  return acc;
}

Money goodsGrossTotal(const std::vector<GoodsLine> & rows)
{
  Money acc = goodsLineTotal(rows);
  const Result<Money> next = acc.plus(goodsVatTotal(rows));
  return next ? next.value() : acc;
}

Result<Document> composeTaxInvoice(QSqlDatabase db, const std::vector<Account> & accounts,
                                   const Document & source, const CompanyProfile & company,
                                   QDate date, DocumentId excludeId)
{
  if (!company.vatPayer) {
    return Result<Document>::fail(
        Error::validation(QStringLiteral("податкову накладну виписує лише платник ПДВ")));
  }
  if (!source.id) {
    return Result<Document>::fail(
        Error::validation(QStringLiteral("спочатку збережіть вихідний документ")));
  }
  if (!source.posted) {
    return Result<Document>::fail(
        Error::validation(QStringLiteral("податкову накладну виписують з проведеного документа")));
  }
  const Result<bool> duplicate = hasPostedTaxInvoiceBasedOn(db, source.id, excludeId);
  if (!duplicate) {
    return Result<Document>::fail(duplicate.error());
  }
  if (duplicate.value()) {
    return Result<Document>::fail(
        Error::conflict(QStringLiteral("податкова накладна з цього документа вже проведена")));
  }
  if (!source.counterpartyId) {
    return Result<Document>::fail(
        Error::validation(QStringLiteral("у вихідному документі немає контрагента")));
  }

  std::optional<VatDirection> direction = vatDirectionFromSource(source.type);
  if (source.type == DocumentType::BankPayment) {
    const bool incoming = bankDocumentIncoming(source.lines, accounts);
    bool advance = false;
    for (const BankLine & row : extractBankLines(source.lines, accounts)) {
      if (incoming && codeIs(accounts, row.correspondenceId, chart::kAdvancesReceived)) {
        advance = true;
        direction = VatDirection::Output;
      }
      if (!incoming && codeIs(accounts, row.correspondenceId, chart::kAdvancesIssued)) {
        advance = true;
        direction = VatDirection::Input;
      }
    }
    if (!advance || !direction) {
      return Result<Document>::fail(Error::validation(
          QStringLiteral("з банківського платежу ПН виписують лише на аванс 681 або 371")));
    }
  }
  if (!direction) {
    return Result<Document>::fail(
        Error::validation(QStringLiteral("з «%1» не можна виписати податкову накладну")
                              .arg(documentTypeLabelUk(source.type))));
  }
  if (source.type == DocumentType::FaCommission || source.type == DocumentType::IaCommission) {
    bool fromSupplier = false;
    for (const DocumentLine & line : source.lines) {
      if (codeIs(accounts, line.creditAccountId, chart::kSuppliers)) {
        fromSupplier = true;
        break;
      }
    }
    if (!fromSupplier) {
      return Result<Document>::fail(
          Error::validation(QStringLiteral("ПН до введення ОЗ/НМА — лише з кореспонденцією 631")));
    }
  }

  const Result<Money> vat = sourceVat(source, accounts, company.vatRateBp);
  if (!vat) {
    return Result<Document>::fail(vat.error());
  }
  if (!vat.value().isPositive()) {
    return Result<Document>::fail(
        Error::validation(QStringLiteral("сума ПДВ має бути більшою за нуль")));
  }

  const bool advance = isVatAdvanceSource(source.type);
  const bool reversal = isVatReturnSource(source.type);
  Money remaining = vat.value();
  Money offset = Money::zero();
  if (!reversal) {
    const Result<Money> after =
        firstEventVat(db, source.counterpartyId, excludeId, *direction, advance, vat.value());
    if (!after) {
      return Result<Document>::fail(after.error());
    }
    remaining = after.value();
    const Result<Money> used = vat.value().minus(remaining);
    if (!used) {
      return Result<Document>::fail(used.error());
    }
    offset = used.value();
  }
  if (!remaining.isPositive()) {
    return Result<Document>::fail(
        Error::validation(QStringLiteral("ПДВ з цієї події вже нараховано (перша подія)")));
  }

  const AccountId settlement = accountIdByCode(accounts, settlementCode(*direction, advance));
  const AccountId liability = accountIdByCode(accounts, chart::kVatLiability);
  const AccountId credit = accountIdByCode(accounts, chart::kVatCredit);
  if (!settlement || !liability || !credit) {
    return Result<Document>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 361/631/371/681 або 643/644")));
  }

  DocumentLine obligation;
  if (*direction == VatDirection::Output) {
    if (reversal) {
      obligation = posting(liability, settlement, remaining, QStringLiteral("ПДВ, коригування"));
    } else {
      obligation =
          posting(settlement, liability, remaining, QStringLiteral("ПДВ, податкове зобов'язання"));
    }
  } else if (reversal) {
    obligation = posting(settlement, credit, remaining, QStringLiteral("ПДВ, коригування кредиту"));
  } else {
    obligation = posting(credit, settlement, remaining, QStringLiteral("ПДВ, податковий кредит"));
  }

  Document document;
  document.type = DocumentType::TaxInvoice;
  document.date = date.isValid() ? date : QDate::currentDate();
  document.counterpartyId = source.counterpartyId;
  document.basedOnId = source.id;
  document.vatOffsetCents = offset.cents();
  document.comment =
      QStringLiteral("На підставі %1 %2").arg(documentTypeLabelUk(source.type), source.number);
  document.lines = {obligation};
  return Result<Document>::ok(std::move(document));
}

Result<std::vector<DocumentLine>> vatRegistrationLines(const std::vector<Account> & accounts,
                                                       const Document & document)
{
  const AccountId budget = accountIdByCode(accounts, chart::kVatBudget);
  const AccountId liability = accountIdByCode(accounts, chart::kVatLiability);
  const AccountId credit = accountIdByCode(accounts, chart::kVatCredit);
  if (!budget || !liability || !credit) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 641, 643 або 644")));
  }
  std::vector<DocumentLine> extra;
  for (const DocumentLine & line : document.lines) {
    if (!isVatObligationLine(accounts, line)) {
      continue;
    }
    DocumentLine move;
    move.amount = line.amount;
    move.postingDate = document.vatRegistrationDate;
    move.description = QStringLiteral("Реєстрація ПН в ЄРПН");
    if (codeIs(accounts, line.creditAccountId, chart::kVatLiability)) {
      move.debitAccountId = liability;
      move.creditAccountId = budget;
    } else if (codeIs(accounts, line.debitAccountId, chart::kVatLiability)) {
      move.debitAccountId = budget;
      move.creditAccountId = liability;
    } else if (codeIs(accounts, line.debitAccountId, chart::kVatCredit)) {
      move.debitAccountId = budget;
      move.creditAccountId = credit;
    } else if (codeIs(accounts, line.creditAccountId, chart::kVatCredit)) {
      move.debitAccountId = credit;
      move.creditAccountId = budget;
    } else {
      continue;
    }
    extra.push_back(move);
  }
  if (extra.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("немає рядка ПДВ для реєстрації")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(extra));
}

} // namespace fin
