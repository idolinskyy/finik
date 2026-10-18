#include "fin/domain/DocumentBlueprint.h"

#include "fin/core/Checked.h"
#include "fin/core/Result.h"
#include "fin/db/BomRepository.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/Employee.h"
#include "fin/domain/FixedAsset.h"
#include "fin/domain/IntangibleAsset.h"
#include "fin/domain/LowValueItem.h"
#include "fin/domain/Vat.h"

#include <QDate>
#include <QLatin1String>
#include <QSqlDatabase>

namespace fin {
namespace {

DocumentLine posting(AccountId debit, AccountId credit, Money amount, const QString & description)
{
  DocumentLine line;
  line.debitAccountId = debit;
  line.creditAccountId = credit;
  line.amount = amount;
  line.description = description;
  return line;
}

void copyVat(DocumentLine & line, int rateBp, std::int64_t vatCents)
{
  line.vatRateBp = rateBp;
  line.vatCents = vatCents;
}

void copyVat(GoodsLine & row, const DocumentLine & line)
{
  row.vatRateBp = line.vatRateBp;
  row.vatCents = line.vatCents;
}

} // namespace

AccountId accountIdByCode(const std::vector<Account> & accounts, const char * code)
{
  for (const Account & account : accounts) {
    if (account.code.compare(QLatin1String(code), Qt::CaseInsensitive) == 0) {
      return account.id;
    }
  }
  return {};
}

bool accountHasCode(const std::vector<Account> & accounts, AccountId id, const char * code)
{
  for (const Account & account : accounts) {
    if (account.id == id) {
      return account.code.compare(QLatin1String(code), Qt::CaseInsensitive) == 0;
    }
  }
  return false;
}

bool usesGoodsForm(DocumentType type)
{
  return type == DocumentType::Purchase || type == DocumentType::Sale ||
         type == DocumentType::SaleReturn || type == DocumentType::PurchaseReturn ||
         type == DocumentType::WriteOff || type == DocumentType::Stocktake ||
         type == DocumentType::ServiceSale || type == DocumentType::CustomerInvoice ||
         type == DocumentType::StockTransfer || type == DocumentType::KitAssembly ||
         type == DocumentType::KitDisassembly || type == DocumentType::CustomerOrder ||
         type == DocumentType::SupplierOrder || type == DocumentType::MaterialIssue ||
         type == DocumentType::ProductionReport;
}

bool usesCashForm(DocumentType type)
{
  return type == DocumentType::CashIn || type == DocumentType::CashOut;
}

bool usesBankForm(DocumentType type)
{
  return type == DocumentType::BankPayment;
}

bool usesOpeningForm(DocumentType type)
{
  return type == DocumentType::OpeningBalance;
}

bool usesServicePurchaseForm(DocumentType type)
{
  return type == DocumentType::ServicePurchase;
}

bool usesExpenseReportForm(DocumentType type)
{
  return type == DocumentType::ExpenseReport;
}

DocumentEditorKind documentEditorKind(DocumentType type)
{
  // Порядок перевірок = DocumentDialog::openDocument. Окремі типи
  // (відпускні / лікарняні) мають іти ПЕРЕД родиною usesPayrollForm.
  if (usesGoodsForm(type)) {
    return DocumentEditorKind::Goods;
  }
  if (usesCashForm(type)) {
    return DocumentEditorKind::Cash;
  }
  if (usesBankForm(type)) {
    return DocumentEditorKind::Bank;
  }
  if (usesServicePurchaseForm(type)) {
    return DocumentEditorKind::ServicePurchase;
  }
  if (usesOpeningForm(type)) {
    return DocumentEditorKind::Opening;
  }
  if (usesFaCommissionForm(type)) {
    return DocumentEditorKind::FaCommission;
  }
  if (usesFaDepreciationForm(type)) {
    return DocumentEditorKind::FaDepreciation;
  }
  if (usesFaWriteOffForm(type)) {
    return DocumentEditorKind::FaWriteOff;
  }
  if (usesIaCommissionForm(type)) {
    return DocumentEditorKind::IaCommission;
  }
  if (usesIaDepreciationForm(type)) {
    return DocumentEditorKind::IaDepreciation;
  }
  if (usesIaWriteOffForm(type)) {
    return DocumentEditorKind::IaWriteOff;
  }
  if (usesMsForm(type)) {
    return DocumentEditorKind::Ms;
  }
  if (usesTaxInvoiceForm(type)) {
    return DocumentEditorKind::TaxInvoice;
  }
  if (type == DocumentType::VacationPayroll) {
    return DocumentEditorKind::VacationPayroll;
  }
  if (type == DocumentType::SickLeavePayroll) {
    return DocumentEditorKind::SickLeavePayroll;
  }
  if (usesPayrollForm(type)) {
    return DocumentEditorKind::Payroll;
  }
  if (usesExpenseReportForm(type)) {
    return DocumentEditorKind::ExpenseReport;
  }
  return DocumentEditorKind::Generic;
}

QString documentEditorKindLabel(DocumentEditorKind kind)
{
  switch (kind) {
  case DocumentEditorKind::Goods:
    return QStringLiteral("Goods");
  case DocumentEditorKind::Cash:
    return QStringLiteral("Cash");
  case DocumentEditorKind::Bank:
    return QStringLiteral("Bank");
  case DocumentEditorKind::ServicePurchase:
    return QStringLiteral("ServicePurchase");
  case DocumentEditorKind::Opening:
    return QStringLiteral("Opening");
  case DocumentEditorKind::FaCommission:
    return QStringLiteral("FaCommission");
  case DocumentEditorKind::FaDepreciation:
    return QStringLiteral("FaDepreciation");
  case DocumentEditorKind::FaWriteOff:
    return QStringLiteral("FaWriteOff");
  case DocumentEditorKind::IaCommission:
    return QStringLiteral("IaCommission");
  case DocumentEditorKind::IaDepreciation:
    return QStringLiteral("IaDepreciation");
  case DocumentEditorKind::IaWriteOff:
    return QStringLiteral("IaWriteOff");
  case DocumentEditorKind::Ms:
    return QStringLiteral("Ms");
  case DocumentEditorKind::TaxInvoice:
    return QStringLiteral("TaxInvoice");
  case DocumentEditorKind::VacationPayroll:
    return QStringLiteral("VacationPayroll");
  case DocumentEditorKind::SickLeavePayroll:
    return QStringLiteral("SickLeavePayroll");
  case DocumentEditorKind::Payroll:
    return QStringLiteral("Payroll");
  case DocumentEditorKind::ExpenseReport:
    return QStringLiteral("ExpenseReport");
  case DocumentEditorKind::Generic:
    return QStringLiteral("Generic");
  }
  return QStringLiteral("?");
}

bool isServicePurchaseDebitCode(const QString & code)
{
  return code.compare(QLatin1String(chart::kAdminExpense), Qt::CaseInsensitive) == 0 ||
         code.compare(QLatin1String(chart::kCapexFa), Qt::CaseInsensitive) == 0 ||
         code.compare(QLatin1String(chart::kCapexIa), Qt::CaseInsensitive) == 0 ||
         code.compare(QLatin1String(chart::kManufacturingOverhead), Qt::CaseInsensitive) == 0 ||
         code.compare(QLatin1String(chart::kProduction), Qt::CaseInsensitive) == 0;
}

bool canPrintCashReceipt(DocumentType type)
{
  return type == DocumentType::CashIn;
}

bool canPrintCashVoucher(DocumentType type)
{
  return type == DocumentType::CashOut;
}

bool canPrintCashOrder(DocumentType type)
{
  return canPrintCashReceipt(type) || canPrintCashVoucher(type);
}

bool canPrintDeliveryNote(DocumentType type)
{
  return type == DocumentType::Sale || type == DocumentType::SaleReturn ||
         type == DocumentType::StockTransfer || type == DocumentType::KitAssembly ||
         type == DocumentType::KitDisassembly || type == DocumentType::MaterialIssue ||
         type == DocumentType::ProductionReport;
}

bool canPrintPurchaseNote(DocumentType type)
{
  return type == DocumentType::Purchase || type == DocumentType::PurchaseReturn;
}

bool canPrintBankPayment(DocumentType type)
{
  return type == DocumentType::BankPayment;
}

bool canPrintServiceAct(DocumentType type)
{
  return type == DocumentType::ServiceSale;
}

bool canPrintSupplierServiceAct(DocumentType type)
{
  return type == DocumentType::ServicePurchase;
}

bool canPrintCustomerInvoice(DocumentType type)
{
  return type == DocumentType::CustomerInvoice;
}

bool canPrintExpenseReport(DocumentType type)
{
  return type == DocumentType::ExpenseReport;
}

bool canPrintDocument(DocumentType type)
{
  return canPrintCashOrder(type) || canPrintDeliveryNote(type) || canPrintPurchaseNote(type) ||
         canPrintBankPayment(type) || canPrintServiceAct(type) || canPrintCustomerInvoice(type) ||
         canPrintExpenseReport(type) || type == DocumentType::WriteOff ||
         type == DocumentType::Stocktake || type == DocumentType::FaCommission ||
         type == DocumentType::FaDepreciation || type == DocumentType::FaWriteOff ||
         type == DocumentType::MsReceipt || type == DocumentType::MsIssue ||
         type == DocumentType::IaCommission || type == DocumentType::IaDepreciation ||
         type == DocumentType::IaWriteOff || canPrintSupplierServiceAct(type) ||
         type == DocumentType::TaxInvoice || type == DocumentType::Payroll ||
         type == DocumentType::VacationPayroll || type == DocumentType::SickLeavePayroll ||
         type == DocumentType::StockTransfer || type == DocumentType::KitAssembly ||
         type == DocumentType::KitDisassembly || type == DocumentType::CustomerOrder ||
         type == DocumentType::SupplierOrder || type == DocumentType::MaterialIssue ||
         type == DocumentType::ProductionReport;
}

bool requiresLedgerPosting(DocumentType type)
{
  return type != DocumentType::CustomerInvoice && type != DocumentType::CustomerOrder &&
         type != DocumentType::SupplierOrder;
}

std::vector<DocumentType> basedOnTargets(DocumentType source)
{
  switch (source) {
  case DocumentType::CustomerOrder:
    return {DocumentType::Sale, DocumentType::CustomerInvoice, DocumentType::BankPayment,
            DocumentType::CashIn};
  case DocumentType::SupplierOrder:
    return {DocumentType::Purchase, DocumentType::BankPayment, DocumentType::CashOut};
  case DocumentType::CustomerInvoice:
    return {DocumentType::Sale, DocumentType::BankPayment, DocumentType::CashIn};
  case DocumentType::Sale:
    return {DocumentType::SaleReturn, DocumentType::BankPayment, DocumentType::CashIn,
            DocumentType::TaxInvoice};
  case DocumentType::ServiceSale:
    return {DocumentType::BankPayment, DocumentType::CashIn, DocumentType::TaxInvoice};
  case DocumentType::ServicePurchase:
    return {DocumentType::BankPayment, DocumentType::CashOut, DocumentType::TaxInvoice};
  case DocumentType::Purchase:
    return {DocumentType::PurchaseReturn, DocumentType::BankPayment, DocumentType::CashOut,
            DocumentType::TaxInvoice};
  case DocumentType::ProductionReport:
    return {DocumentType::MaterialIssue, DocumentType::Sale};
  case DocumentType::MaterialIssue:
    return {DocumentType::ProductionReport};
  case DocumentType::SaleReturn:
  case DocumentType::PurchaseReturn:
  case DocumentType::CashIn:
    return {DocumentType::TaxInvoice};
  case DocumentType::CashOut:
    return {DocumentType::TaxInvoice, DocumentType::ExpenseReport};
  case DocumentType::ExpenseReport:
    return {DocumentType::CashIn, DocumentType::CashOut, DocumentType::BankPayment};
  case DocumentType::BankPayment:
  case DocumentType::MsReceipt:
  case DocumentType::FaCommission:
  case DocumentType::IaCommission:
    return {DocumentType::TaxInvoice};
  case DocumentType::Payroll:
  case DocumentType::VacationPayroll:
  case DocumentType::SickLeavePayroll:
    return {DocumentType::BankPayment, DocumentType::CashOut};
  default:
    return {};
  }
}

namespace {

static Money settlementAmount(const Document & source, const std::vector<Account> & accounts)
{
  if (source.type == DocumentType::Sale || source.type == DocumentType::CustomerInvoice ||
      source.type == DocumentType::CustomerOrder) {
    return saleRevenueTotal(source.lines, accounts);
  }
  if (source.type == DocumentType::ServiceSale) {
    Money total = Money::zero();
    for (const DocumentLine & line : source.lines) {
      if (!accountHasCode(accounts, line.debitAccountId, chart::kCustomers) ||
          !accountHasCode(accounts, line.creditAccountId, chart::kServiceRevenue)) {
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
  if (source.type == DocumentType::Purchase || source.type == DocumentType::SupplierOrder) {
    return goodsLineTotal(extractGoodsLines(source.type, source.lines, accounts));
  }
  if (source.type == DocumentType::ServicePurchase) {
    Money total = Money::zero();
    for (const CashLine & row : extractServicePurchaseLines(source.lines, accounts)) {
      const Result<Money> next = total.plus(row.amount);
      if (!next) {
        return total;
      }
      total = next.value();
    }
    return total;
  }
  return Money::zero();
}

} // namespace

Result<Document> buildBasedOn(const Document & source, DocumentType target,
                              const std::vector<Account> & accounts, QSqlDatabase db)
{
  const std::vector<DocumentType> allowed = basedOnTargets(source.type);
  bool ok = false;
  for (DocumentType type : allowed) {
    if (type == target) {
      ok = true;
      break;
    }
  }
  if (!ok) {
    return Result<Document>::fail(
        Error::validation(QStringLiteral("з «%1» не можна ввести «%2»")
                              .arg(documentTypeLabelUk(source.type), documentTypeLabelUk(target))));
  }
  if (source.type == DocumentType::Payroll || source.type == DocumentType::VacationPayroll ||
      source.type == DocumentType::SickLeavePayroll) {
    const Money net = payrollNetPayable(source.lines, accounts);
    if (!net.isPositive()) {
      return Result<Document>::fail(
          Error::validation(QStringLiteral("немає суми до виплати по 661")));
    }
    const AccountId salary = accountIdByCode(accounts, chart::kSalaryPayable);
    if (!salary) {
      return Result<Document>::fail(
          Error::validation(QStringLiteral("у плані рахунків немає 661")));
    }
    Document document;
    document.type = target;
    document.date = QDate::currentDate();
    document.basedOnId = source.id;
    document.comment =
        QStringLiteral("На підставі %1 %2").arg(documentTypeLabelUk(source.type), source.number);
    if (target == DocumentType::BankPayment) {
      BankLine row;
      row.correspondenceId = salary;
      row.amount = net;
      row.description = document.comment;
      const Result<std::vector<DocumentLine>> lines = composeBank(accounts, false, {row});
      if (!lines) {
        return Result<Document>::fail(lines.error());
      }
      document.lines = lines.value();
      return Result<Document>::ok(std::move(document));
    }
    CashLine row;
    row.correspondenceId = salary;
    row.amount = net;
    row.description = document.comment;
    const Result<std::vector<DocumentLine>> lines = composeCashOut(accounts, {row});
    if (!lines) {
      return Result<Document>::fail(lines.error());
    }
    document.lines = lines.value();
    return Result<Document>::ok(std::move(document));
  }
  if (source.type == DocumentType::ExpenseReport) {
    const std::vector<ExpenseLine> expLines = extractExpenseLines(source.lines, accounts);
    const Money total = expenseLineTotal(expLines);
    if (!total.isPositive()) {
      return Result<Document>::fail(
          Error::validation(QStringLiteral("в авансовому звіті немає суми витрат")));
    }
    const AccountId accountableAcc = accountIdByCode(accounts, chart::kAccountablePersonsUah);
    if (!accountableAcc) {
      return Result<Document>::fail(
          Error::validation(QStringLiteral("у плані рахунків немає 3721")));
    }
    Document document;
    document.type = target;
    document.date = QDate::currentDate();
    document.basedOnId = source.id;
    document.employeeId = source.employeeId;
    document.comment =
        QStringLiteral("На підставі %1 %2").arg(documentTypeLabelUk(source.type), source.number);
    if (target == DocumentType::BankPayment) {
      BankLine row;
      row.correspondenceId = accountableAcc;
      row.amount = total;
      row.description = document.comment;
      const Result<std::vector<DocumentLine>> lines = composeBank(accounts, false, {row});
      if (!lines) {
        return Result<Document>::fail(lines.error());
      }
      document.lines = lines.value();
      return Result<Document>::ok(std::move(document));
    }
    if (target == DocumentType::CashIn) {
      CashLine row;
      row.correspondenceId = accountableAcc;
      row.amount = total;
      row.description = document.comment;
      const Result<std::vector<DocumentLine>> lines = composeCashIn(accounts, {row});
      if (!lines) {
        return Result<Document>::fail(lines.error());
      }
      document.lines = lines.value();
      return Result<Document>::ok(std::move(document));
    }
    if (target == DocumentType::CashOut) {
      CashLine row;
      row.correspondenceId = accountableAcc;
      row.amount = total;
      row.description = document.comment;
      const Result<std::vector<DocumentLine>> lines = composeCashOut(accounts, {row});
      if (!lines) {
        return Result<Document>::fail(lines.error());
      }
      document.lines = lines.value();
      return Result<Document>::ok(std::move(document));
    }
  }
  if (source.type == DocumentType::CashOut && target == DocumentType::ExpenseReport) {
    Document document;
    document.type = target;
    document.date = QDate::currentDate();
    document.basedOnId = source.id;
    document.employeeId = source.employeeId;
    document.comment =
        QStringLiteral("На підставі %1 %2").arg(documentTypeLabelUk(source.type), source.number);
    const AccountId adminExp = accountIdByCode(accounts, chart::kAdminExpense);
    const AccountId accountableAcc = accountIdByCode(accounts, chart::kAccountablePersonsUah);
    Money total = Money::zero();
    for (const auto & line : source.lines) {
      const auto next = total.plus(line.amount);
      if (next)
        total = next.value();
    }
    if (adminExp && total.isPositive()) {
      ExpenseLine el;
      el.expenseAccountId = adminExp;
      el.amount = total;
      el.description = document.comment;
      const Result<std::vector<DocumentLine>> lines =
          composeExpenseReport(accounts, {el}, accountableAcc);
      if (lines) {
        document.lines = lines.value();
      }
    }
    return Result<Document>::ok(std::move(document));
  }
  if (!source.counterpartyId &&
      (target == DocumentType::Sale || target == DocumentType::SaleReturn ||
       target == DocumentType::PurchaseReturn || target == DocumentType::ServiceSale ||
       target == DocumentType::ServicePurchase || target == DocumentType::BankPayment ||
       target == DocumentType::CashIn || target == DocumentType::CashOut ||
       target == DocumentType::Purchase || target == DocumentType::CustomerInvoice)) {
    return Result<Document>::fail(
        Error::validation(QStringLiteral("у вихідному документі немає контрагента")));
  }

  Document document;
  document.type = target;
  document.date = QDate::currentDate();
  document.counterpartyId = source.counterpartyId;
  document.basedOnId = source.id;
  document.contractId = source.contractId;
  document.warehouseId = source.warehouseId;
  document.comment =
      QStringLiteral("На підставі %1 %2").arg(documentTypeLabelUk(source.type), source.number);

  if (target == DocumentType::Sale) {
    const Result<std::vector<DocumentLine>> lines =
        composeSale(accounts, extractGoodsLines(source.type, source.lines, accounts));
    if (!lines) {
      return Result<Document>::fail(lines.error());
    }
    document.lines = lines.value();
    return Result<Document>::ok(std::move(document));
  }
  if (target == DocumentType::Purchase) {
    const Result<std::vector<DocumentLine>> lines =
        composePurchase(accounts, extractGoodsLines(source.type, source.lines, accounts));
    if (!lines) {
      return Result<Document>::fail(lines.error());
    }
    document.lines = lines.value();
    return Result<Document>::ok(std::move(document));
  }
  if (target == DocumentType::CustomerInvoice) {
    const Result<std::vector<DocumentLine>> lines =
        composeCustomerInvoice(accounts, extractGoodsLines(source.type, source.lines, accounts));
    if (!lines) {
      return Result<Document>::fail(lines.error());
    }
    document.lines = lines.value();
    return Result<Document>::ok(std::move(document));
  }
  if (target == DocumentType::SaleReturn) {
    const Result<std::vector<DocumentLine>> lines =
        composeSaleReturn(accounts, extractGoodsLines(source.type, source.lines, accounts));
    if (!lines) {
      return Result<Document>::fail(lines.error());
    }
    document.lines = lines.value();
    return Result<Document>::ok(std::move(document));
  }
  if (target == DocumentType::PurchaseReturn) {
    const Result<std::vector<DocumentLine>> lines =
        composePurchaseReturn(accounts, extractGoodsLines(source.type, source.lines, accounts));
    if (!lines) {
      return Result<Document>::fail(lines.error());
    }
    document.lines = lines.value();
    return Result<Document>::ok(std::move(document));
  }
  if (target == DocumentType::MaterialIssue) {
    document.type = DocumentType::MaterialIssue;
    document.basedOnId = source.id;
    document.bomId = source.bomId;
    document.warehouseId = source.warehouseId;
    document.comment =
        QStringLiteral("Вимога-накладна на підставі %1").arg(source.number);
    if (source.type == DocumentType::ProductionReport) {
      if (!source.bomId || !db.isValid()) {
        return Result<Document>::fail(Error::validation(
            QStringLiteral("для вимоги з випуску потрібна специфікація (BOM) у документі випуску; "
                           "створіть вимогу зі сторінки «Специфікації»")));
      }
      const Result<BillOfMaterials> bom = BomRepository(db).findById(source.bomId);
      if (!bom) {
        return Result<Document>::fail(bom.error());
      }
      std::vector<GoodsLine> goods;
      goods.reserve(bom.value().lines.size());
      for (const BomLine & line : bom.value().lines) {
        GoodsLine row;
        row.nomenclatureId = line.componentId;
        row.quantity = line.quantity;
        row.description = line.comment;
        goods.push_back(row);
      }
      const Result<std::vector<DocumentLine>> lines = composeMaterialIssue(accounts, goods);
      if (!lines) {
        return Result<Document>::fail(lines.error());
      }
      document.lines = lines.value();
      return Result<Document>::ok(std::move(document));
    }
    const Result<std::vector<DocumentLine>> lines =
        composeMaterialIssue(accounts, extractGoodsLines(source.type, source.lines, accounts));
    if (lines) {
      document.lines = lines.value();
    }
    return Result<Document>::ok(std::move(document));
  }
  if (target == DocumentType::ProductionReport) {
    document.type = DocumentType::ProductionReport;
    document.basedOnId = source.id;
    document.bomId = source.bomId;
    document.warehouseId = source.warehouseId;
    document.comment = QStringLiteral("Випуск продукції на підставі %1").arg(source.number);
    if (source.type == DocumentType::MaterialIssue) {
      if (source.bomId && db.isValid()) {
        const Result<BillOfMaterials> bom = BomRepository(db).findById(source.bomId);
        if (bom) {
          GoodsLine product;
          product.nomenclatureId = bom.value().productId;
          product.quantity = bom.value().outputQuantity;
          product.description = bom.value().name;
          const Result<std::vector<DocumentLine>> lines =
              composeProductionReport(accounts, {product});
          if (!lines) {
            return Result<Document>::fail(lines.error());
          }
          document.lines = lines.value();
          return Result<Document>::ok(std::move(document));
        }
      }
      // Без BOM — порожня чернетка: користувач додасть готовий виріб у формі.
      return Result<Document>::ok(std::move(document));
    }
    const Result<std::vector<DocumentLine>> lines =
        composeProductionReport(accounts, extractGoodsLines(source.type, source.lines, accounts));
    if (lines) {
      document.lines = lines.value();
    }
    return Result<Document>::ok(std::move(document));
  }
  if (target == DocumentType::Sale && source.type == DocumentType::ProductionReport) {
    document.type = DocumentType::Sale;
    document.comment = QStringLiteral("Реалізація на підставі %1").arg(source.number);
    const Result<std::vector<DocumentLine>> lines =
        composeSale(accounts, extractGoodsLines(source.type, source.lines, accounts));
    if (lines) {
      document.lines = lines.value();
    }
    return Result<Document>::ok(std::move(document));
  }

  const Money amount = settlementAmount(source, accounts);
  if (!amount.isPositive()) {
    return Result<Document>::fail(
        Error::validation(QStringLiteral("немає суми для введення на підставі")));
  }
  const bool fromSupplier = source.type == DocumentType::Purchase ||
                            source.type == DocumentType::ServicePurchase ||
                            source.type == DocumentType::SupplierOrder;
  const AccountId correspondence =
      accountIdByCode(accounts, fromSupplier ? chart::kSuppliers : chart::kCustomers);
  if (!correspondence) {
    return Result<Document>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 361 або 631")));
  }
  if (target == DocumentType::BankPayment) {
    BankLine row;
    row.correspondenceId = correspondence;
    row.amount = amount;
    row.description = document.comment;
    const Result<std::vector<DocumentLine>> lines = composeBank(accounts, !fromSupplier, {row});
    if (!lines) {
      return Result<Document>::fail(lines.error());
    }
    document.lines = lines.value();
    return Result<Document>::ok(std::move(document));
  }
  CashLine row;
  row.correspondenceId = correspondence;
  row.amount = amount;
  row.description = document.comment;
  const Result<std::vector<DocumentLine>> lines = target == DocumentType::CashIn
                                                      ? composeCashIn(accounts, {row})
                                                      : composeCashOut(accounts, {row});
  if (!lines) {
    return Result<Document>::fail(lines.error());
  }
  document.lines = lines.value();
  return Result<Document>::ok(std::move(document));
}

std::vector<DocumentLine> defaultLinesForType(DocumentType type,
                                              const std::vector<Account> & accounts)
{
  DocumentLine line;
  switch (type) {
  case DocumentType::OpeningBalance:
    line.creditAccountId = accountIdByCode(accounts, chart::kAuxiliary);
    return {line};
  case DocumentType::Manual:
    return {line};
  case DocumentType::CashIn:
    line.debitAccountId = accountIdByCode(accounts, chart::kCash);
    return {line};
  case DocumentType::CashOut:
    line.creditAccountId = accountIdByCode(accounts, chart::kCash);
    return {line};
  case DocumentType::Purchase:
    line.debitAccountId = accountIdByCode(accounts, chart::kGoods);
    line.creditAccountId = accountIdByCode(accounts, chart::kSuppliers);
    return {line};
  case DocumentType::Sale: {
    line.debitAccountId = accountIdByCode(accounts, chart::kCustomers);
    line.creditAccountId = accountIdByCode(accounts, chart::kRevenue);
    DocumentLine cogs;
    cogs.debitAccountId = accountIdByCode(accounts, chart::kCogs);
    cogs.creditAccountId = accountIdByCode(accounts, chart::kGoods);
    return {line, cogs};
  }
  case DocumentType::BankPayment:
    line.debitAccountId = accountIdByCode(accounts, chart::kBank);
    line.creditAccountId = accountIdByCode(accounts, chart::kCustomers);
    return {line};
  case DocumentType::SaleReturn: {
    line.debitAccountId = accountIdByCode(accounts, chart::kRevenue);
    line.creditAccountId = accountIdByCode(accounts, chart::kCustomers);
    DocumentLine stock;
    stock.debitAccountId = accountIdByCode(accounts, chart::kGoods);
    stock.creditAccountId = accountIdByCode(accounts, chart::kCogs);
    return {line, stock};
  }
  case DocumentType::PurchaseReturn:
    line.debitAccountId = accountIdByCode(accounts, chart::kSuppliers);
    line.creditAccountId = accountIdByCode(accounts, chart::kGoods);
    return {line};
  case DocumentType::WriteOff:
    line.debitAccountId = accountIdByCode(accounts, chart::kAdminExpense);
    line.creditAccountId = accountIdByCode(accounts, chart::kGoods);
    return {line};
  case DocumentType::Stocktake:
    line.debitAccountId = accountIdByCode(accounts, chart::kGoods);
    line.creditAccountId = accountIdByCode(accounts, chart::kOtherIncome);
    return {line};
  case DocumentType::PeriodResult:
    line.debitAccountId = accountIdByCode(accounts, chart::kRevenue);
    line.creditAccountId = accountIdByCode(accounts, chart::kFinancialResult);
    return {line};
  case DocumentType::ServiceSale:
    line.debitAccountId = accountIdByCode(accounts, chart::kCustomers);
    line.creditAccountId = accountIdByCode(accounts, chart::kServiceRevenue);
    return {line};
  case DocumentType::ServicePurchase:
    line.debitAccountId = accountIdByCode(accounts, chart::kAdminExpense);
    line.creditAccountId = accountIdByCode(accounts, chart::kSuppliers);
    return {line};
  case DocumentType::CustomerInvoice:
    line.debitAccountId = accountIdByCode(accounts, chart::kCustomers);
    line.creditAccountId = accountIdByCode(accounts, chart::kRevenue);
    return {line};
  case DocumentType::FaCommission:
    line.debitAccountId = accountIdByCode(accounts, chart::kMachinery);
    line.creditAccountId = accountIdByCode(accounts, chart::kSuppliers);
    return {line};
  case DocumentType::FaDepreciation:
    line.debitAccountId = accountIdByCode(accounts, chart::kAdminExpense);
    line.creditAccountId = accountIdByCode(accounts, chart::kFaDepreciation);
    return {line};
  case DocumentType::FaWriteOff:
    line.debitAccountId = accountIdByCode(accounts, chart::kFaDepreciation);
    line.creditAccountId = accountIdByCode(accounts, chart::kMachinery);
    return {line};
  case DocumentType::MsReceipt:
    line.debitAccountId = accountIdByCode(accounts, chart::kLowValue);
    line.creditAccountId = accountIdByCode(accounts, chart::kSuppliers);
    return {line};
  case DocumentType::MsIssue:
    line.debitAccountId = accountIdByCode(accounts, chart::kAdminExpense);
    line.creditAccountId = accountIdByCode(accounts, chart::kLowValue);
    return {line};
  case DocumentType::IaCommission:
    line.debitAccountId = accountIdByCode(accounts, chart::kSoftware);
    line.creditAccountId = accountIdByCode(accounts, chart::kSuppliers);
    return {line};
  case DocumentType::IaDepreciation:
    line.debitAccountId = accountIdByCode(accounts, chart::kAdminExpense);
    line.creditAccountId = accountIdByCode(accounts, chart::kIaAmortization);
    return {line};
  case DocumentType::IaWriteOff:
    line.debitAccountId = accountIdByCode(accounts, chart::kIaAmortization);
    line.creditAccountId = accountIdByCode(accounts, chart::kSoftware);
    return {line};
  case DocumentType::TaxInvoice:
    line.debitAccountId = accountIdByCode(accounts, chart::kCustomers);
    line.creditAccountId = accountIdByCode(accounts, chart::kVatLiability);
    return {line};
  case DocumentType::Payroll:
  case DocumentType::VacationPayroll:
  case DocumentType::SickLeavePayroll:
    line.debitAccountId = accountIdByCode(accounts, chart::kAdminExpense);
    line.creditAccountId = accountIdByCode(accounts, chart::kSalaryPayable);
    return {line};
  case DocumentType::StockTransfer:
  case DocumentType::KitAssembly:
  case DocumentType::KitDisassembly:
    line.debitAccountId = accountIdByCode(accounts, chart::kGoods);
    line.creditAccountId = accountIdByCode(accounts, chart::kGoods);
    return {line};
  case DocumentType::CustomerOrder:
    line.debitAccountId = accountIdByCode(accounts, chart::kCustomers);
    line.creditAccountId = accountIdByCode(accounts, chart::kRevenue);
    return {line};
  case DocumentType::SupplierOrder:
    line.debitAccountId = accountIdByCode(accounts, chart::kGoods);
    line.creditAccountId = accountIdByCode(accounts, chart::kSuppliers);
    return {line};
  case DocumentType::MaterialIssue:
    line.debitAccountId = accountIdByCode(accounts, chart::kProduction);
    line.creditAccountId = accountIdByCode(accounts, chart::kRawMaterials);
    return {line};
  case DocumentType::ProductionReport:
    line.debitAccountId = accountIdByCode(accounts, chart::kFinishedGoods);
    line.creditAccountId = accountIdByCode(accounts, chart::kProduction);
    return {line};
  case DocumentType::ExpenseReport:
    line.debitAccountId = accountIdByCode(accounts, chart::kAdminExpense);
    line.creditAccountId = accountIdByCode(accounts, chart::kAccountablePersonsUah);
    return {line};
  }
  return {line};
}

void accountLocksForType(DocumentType type, bool & lockDebit, bool & lockCredit)
{
  lockDebit = false;
  lockCredit = false;
  switch (type) {
  case DocumentType::CashIn:
    lockDebit = true;
    break;
  case DocumentType::CashOut:
    lockCredit = true;
    break;
  case DocumentType::ExpenseReport:
    lockCredit = true;
    break;
  case DocumentType::StockTransfer:
  case DocumentType::KitAssembly:
  case DocumentType::KitDisassembly:
    lockDebit = true;
    lockCredit = true;
    break;
  case DocumentType::MaterialIssue:
  case DocumentType::ProductionReport:
  case DocumentType::Purchase:
  case DocumentType::Sale:
  case DocumentType::BankPayment:
  case DocumentType::OpeningBalance:
  case DocumentType::Manual:
  case DocumentType::SaleReturn:
  case DocumentType::PurchaseReturn:
  case DocumentType::WriteOff:
  case DocumentType::Stocktake:
  case DocumentType::PeriodResult:
  case DocumentType::ServiceSale:
  case DocumentType::ServicePurchase:
  case DocumentType::CustomerInvoice:
  case DocumentType::CustomerOrder:
  case DocumentType::SupplierOrder:
  case DocumentType::FaCommission:
  case DocumentType::FaDepreciation:
  case DocumentType::FaWriteOff:
  case DocumentType::MsReceipt:
  case DocumentType::MsIssue:
  case DocumentType::IaCommission:
  case DocumentType::IaDepreciation:
  case DocumentType::IaWriteOff:
  case DocumentType::TaxInvoice:
  case DocumentType::Payroll:
  case DocumentType::VacationPayroll:
  case DocumentType::SickLeavePayroll:
    break;
  }
}

Result<std::vector<DocumentLine>> composePurchase(const std::vector<Account> & accounts,
                                                  const std::vector<GoodsLine> & rows)
{
  const AccountId goods = accountIdByCode(accounts, chart::kGoods);
  const AccountId suppliers = accountIdByCode(accounts, chart::kSuppliers);
  if (!goods || !suppliers) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 281 або 631")));
  }
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const GoodsLine & row : rows) {
    ++rowNo;
    if (!row.nomenclatureId && row.amount.isZero() && row.quantity.isZero()) {
      continue;
    }
    if (!row.nomenclatureId) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: оберіть номенклатуру").arg(rowNo)));
    }
    if (!row.quantity.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: вкажіть кількість").arg(rowNo)));
    }
    if (!row.amount.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: вкажіть суму").arg(rowNo)));
    }
    DocumentLine line = posting(goods, suppliers, row.amount, row.description);
    line.nomenclatureId = row.nomenclatureId;
    line.quantity = row.quantity;
    line.unitPrice = row.unitPrice;
    copyVat(line, row.vatRateBp, row.vatCents);
    lines.push_back(line);
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б один товар")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Result<std::vector<DocumentLine>> composeSale(const std::vector<Account> & accounts,
                                              const std::vector<GoodsLine> & rows)
{
  const AccountId customers = accountIdByCode(accounts, chart::kCustomers);
  const AccountId revenue = accountIdByCode(accounts, chart::kRevenue);
  const AccountId cogs = accountIdByCode(accounts, chart::kCogs);
  const AccountId goods = accountIdByCode(accounts, chart::kGoods);
  if (!customers || !revenue || !cogs || !goods) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 361, 701, 902 або 281")));
  }
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const GoodsLine & row : rows) {
    ++rowNo;
    if (!row.nomenclatureId && row.amount.isZero() && row.quantity.isZero()) {
      continue;
    }
    if (!row.nomenclatureId) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: оберіть номенклатуру").arg(rowNo)));
    }
    if (!row.quantity.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: вкажіть кількість").arg(rowNo)));
    }
    if (!row.amount.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: вкажіть суму продажу").arg(rowNo)));
    }
    DocumentLine income = posting(customers, revenue, row.amount, row.description);
    income.unitPrice = row.unitPrice;
    copyVat(income, row.vatRateBp, row.vatCents);
    lines.push_back(income);

    DocumentLine cost = posting(cogs, goods, Money::fromCents(1), row.description);
    cost.nomenclatureId = row.nomenclatureId;
    cost.quantity = row.quantity;
    lines.push_back(cost);
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б один товар")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Result<std::vector<DocumentLine>> composeCashIn(const std::vector<Account> & accounts,
                                                const std::vector<CashLine> & rows)
{
  const AccountId cash = accountIdByCode(accounts, chart::kCash);
  if (!cash) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 301")));
  }
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const CashLine & row : rows) {
    ++rowNo;
    if (!row.correspondenceId && row.amount.isZero()) {
      continue;
    }
    if (!row.correspondenceId) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: оберіть кореспонденцію").arg(rowNo)));
    }
    if (!row.amount.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: вкажіть суму").arg(rowNo)));
    }
    lines.push_back(posting(cash, row.correspondenceId, row.amount, row.description));
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б один рядок")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Result<std::vector<DocumentLine>> composeCashOut(const std::vector<Account> & accounts,
                                                 const std::vector<CashLine> & rows)
{
  const AccountId cash = accountIdByCode(accounts, chart::kCash);
  if (!cash) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 301")));
  }
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const CashLine & row : rows) {
    ++rowNo;
    if (!row.correspondenceId && row.amount.isZero()) {
      continue;
    }
    if (!row.correspondenceId) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: оберіть кореспонденцію").arg(rowNo)));
    }
    if (!row.amount.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: вкажіть суму").arg(rowNo)));
    }
    lines.push_back(posting(row.correspondenceId, cash, row.amount, row.description));
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б один рядок")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Result<std::vector<DocumentLine>> composeBank(const std::vector<Account> & accounts, bool incoming,
                                              const std::vector<BankLine> & rows)
{
  const AccountId bank = accountIdByCode(accounts, chart::kBank);
  const AccountId customers = accountIdByCode(accounts, chart::kCustomers);
  const AccountId suppliers = accountIdByCode(accounts, chart::kSuppliers);
  if (!bank) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 311")));
  }
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const BankLine & row : rows) {
    ++rowNo;
    if (row.amount.isZero() && row.description.trimmed().isEmpty() && !row.correspondenceId) {
      continue;
    }
    if (!row.amount.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: вкажіть суму").arg(rowNo)));
    }
    AccountId correspondence = row.correspondenceId;
    if (!correspondence) {
      correspondence = incoming ? customers : suppliers;
    }
    if (!correspondence) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: оберіть кореспонденцію").arg(rowNo)));
    }
    if (correspondence == bank) {
      return Result<std::vector<DocumentLine>>::fail(Error::validation(
          QStringLiteral("рядок %1: кореспонденція не може бути 311").arg(rowNo)));
    }
    if (incoming) {
      lines.push_back(posting(bank, correspondence, row.amount, row.description));
    } else {
      lines.push_back(posting(correspondence, bank, row.amount, row.description));
    }
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б один рядок")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Result<std::vector<DocumentLine>> composeOpening(const std::vector<Account> & accounts,
                                                 const std::vector<OpeningMoneyLine> & money,
                                                 const std::vector<GoodsLine> & goods)
{
  const AccountId aux = accountIdByCode(accounts, chart::kAuxiliary);
  const AccountId stock = accountIdByCode(accounts, chart::kGoods);
  if (!aux || !stock) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 00 або 281")));
  }
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const OpeningMoneyLine & row : money) {
    ++rowNo;
    if (!row.accountId && row.debit.isZero() && row.credit.isZero()) {
      continue;
    }
    if (!row.accountId) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: оберіть рахунок").arg(rowNo)));
    }
    if (row.accountId == aux) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: рахунок 00 підставляється сам").arg(rowNo)));
    }
    if (row.accountId == stock) {
      return Result<std::vector<DocumentLine>>::fail(Error::validation(
          QStringLiteral("рядок %1: залишок 281 вносьте в таблицю товарів").arg(rowNo)));
    }
    const bool hasDebit = row.debit.isPositive();
    const bool hasCredit = row.credit.isPositive();
    if (hasDebit == hasCredit) {
      return Result<std::vector<DocumentLine>>::fail(Error::validation(
          QStringLiteral("рядок %1: заповніть або дебет, або кредит").arg(rowNo)));
    }
    DocumentLine composed;
    if (hasDebit) {
      composed = posting(row.accountId, aux, row.debit, QString());
    } else {
      composed = posting(aux, row.accountId, row.credit, QString());
    }
    composed.counterpartyId = row.counterpartyId;
    composed.contractId = row.contractId;
    lines.push_back(composed);
  }
  rowNo = 0;
  for (const GoodsLine & row : goods) {
    ++rowNo;
    if (!row.nomenclatureId && row.quantity.isZero() && row.amount.isZero()) {
      continue;
    }
    if (!row.nomenclatureId) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("товар %1: оберіть номенклатуру").arg(rowNo)));
    }
    if (!row.quantity.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("товар %1: вкажіть кількість").arg(rowNo)));
    }
    if (!row.amount.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("товар %1: вкажіть суму").arg(rowNo)));
    }
    DocumentLine line = posting(stock, aux, row.amount, row.description);
    line.nomenclatureId = row.nomenclatureId;
    line.quantity = row.quantity;
    line.unitPrice = row.unitPrice;
    lines.push_back(line);
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("вкажіть хоча б один залишок")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Result<std::vector<DocumentLine>> composeSaleReturn(const std::vector<Account> & accounts,
                                                    const std::vector<GoodsLine> & rows)
{
  const AccountId customers = accountIdByCode(accounts, chart::kCustomers);
  const AccountId revenue = accountIdByCode(accounts, chart::kRevenue);
  const AccountId cogs = accountIdByCode(accounts, chart::kCogs);
  const AccountId goods = accountIdByCode(accounts, chart::kGoods);
  if (!customers || !revenue || !cogs || !goods) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 361, 701, 902 або 281")));
  }
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const GoodsLine & row : rows) {
    ++rowNo;
    if (!row.nomenclatureId && row.amount.isZero() && row.quantity.isZero()) {
      continue;
    }
    if (!row.nomenclatureId) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: оберіть номенклатуру").arg(rowNo)));
    }
    if (!row.quantity.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: вкажіть кількість").arg(rowNo)));
    }
    if (!row.amount.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: вкажіть суму повернення").arg(rowNo)));
    }
    DocumentLine income = posting(revenue, customers, row.amount, row.description);
    income.unitPrice = row.unitPrice;
    copyVat(income, row.vatRateBp, row.vatCents);
    lines.push_back(income);
    DocumentLine stock = posting(goods, cogs, Money::fromCents(1), row.description);
    stock.nomenclatureId = row.nomenclatureId;
    stock.quantity = row.quantity;
    lines.push_back(stock);
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б один товар")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Result<std::vector<DocumentLine>> composePurchaseReturn(const std::vector<Account> & accounts,
                                                        const std::vector<GoodsLine> & rows)
{
  const AccountId goods = accountIdByCode(accounts, chart::kGoods);
  const AccountId suppliers = accountIdByCode(accounts, chart::kSuppliers);
  if (!goods || !suppliers) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 281 або 631")));
  }
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const GoodsLine & row : rows) {
    ++rowNo;
    if (!row.nomenclatureId && row.amount.isZero() && row.quantity.isZero()) {
      continue;
    }
    if (!row.nomenclatureId) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: оберіть номенклатуру").arg(rowNo)));
    }
    if (!row.quantity.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: вкажіть кількість").arg(rowNo)));
    }
    DocumentLine line = posting(suppliers, goods, Money::fromCents(1), row.description);
    line.nomenclatureId = row.nomenclatureId;
    line.quantity = row.quantity;
    line.unitPrice = row.unitPrice;
    copyVat(line, row.vatRateBp, row.vatCents);
    lines.push_back(line);
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б один товар")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Result<std::vector<DocumentLine>> composeWriteOff(const std::vector<Account> & accounts,
                                                  const std::vector<GoodsLine> & rows)
{
  const AccountId goods = accountIdByCode(accounts, chart::kGoods);
  const AccountId expense = accountIdByCode(accounts, chart::kAdminExpense);
  if (!goods || !expense) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 281 або 92")));
  }
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const GoodsLine & row : rows) {
    ++rowNo;
    if (!row.nomenclatureId && row.amount.isZero() && row.quantity.isZero()) {
      continue;
    }
    if (!row.nomenclatureId) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: оберіть номенклатуру").arg(rowNo)));
    }
    if (!row.quantity.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: вкажіть кількість").arg(rowNo)));
    }
    DocumentLine line = posting(expense, goods, Money::fromCents(1), row.description);
    line.nomenclatureId = row.nomenclatureId;
    line.quantity = row.quantity;
    line.unitPrice = row.unitPrice;
    lines.push_back(line);
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б один товар")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Result<std::vector<DocumentLine>> composeStocktake(const std::vector<Account> & accounts,
                                                   const std::vector<GoodsLine> & counted,
                                                   const std::vector<StockBalance> & onHand)
{
  const AccountId goods = accountIdByCode(accounts, chart::kGoods);
  const AccountId income = accountIdByCode(accounts, chart::kOtherIncome);
  const AccountId expense = accountIdByCode(accounts, chart::kAdminExpense);
  if (!goods || !income || !expense) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 281, 71 або 92")));
  }
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const GoodsLine & row : counted) {
    ++rowNo;
    if (!row.nomenclatureId && row.quantity.isZero() && row.amount.isZero()) {
      continue;
    }
    if (!row.nomenclatureId) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: оберіть номенклатуру").arg(rowNo)));
    }
    if (row.quantity.isNegative()) {
      return Result<std::vector<DocumentLine>>::fail(Error::validation(
          QStringLiteral("рядок %1: кількість не може бути від'ємною").arg(rowNo)));
    }
    StockBalance book;
    for (const StockBalance & item : onHand) {
      if (item.nomenclatureId == row.nomenclatureId) {
        book = item;
        break;
      }
    }
    const std::int64_t diff = row.quantity.milli() - book.quantity.milli();
    if (diff == 0) {
      continue;
    }
    if (diff > 0) {
      Money amount = row.amount;
      if (!amount.isPositive()) {
        if (book.quantity.isPositive() && book.amount.isPositive()) {
          const Result<std::int64_t> allocated =
              mulDivRound(book.amount.cents(), diff, book.quantity.milli());
          if (!allocated || allocated.value() <= 0) {
            return Result<std::vector<DocumentLine>>::fail(Error::validation(
                QStringLiteral("рядок %1: не вдалося оцінити надлишок").arg(rowNo)));
          }
          amount = Money::fromCents(allocated.value());
        } else {
          return Result<std::vector<DocumentLine>>::fail(Error::validation(
              QStringLiteral("рядок %1: для надлишку без залишку вкажіть суму").arg(rowNo)));
        }
      }
      DocumentLine line = posting(goods, income, amount, row.description);
      line.nomenclatureId = row.nomenclatureId;
      line.quantity = Quantity::fromMilli(diff);
      line.unitPrice = row.unitPrice;
      lines.push_back(line);
      continue;
    }
    DocumentLine line = posting(expense, goods, Money::fromCents(1), row.description);
    line.nomenclatureId = row.nomenclatureId;
    line.quantity = Quantity::fromMilli(-diff);
    lines.push_back(line);
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("немає розбіжностей між обліком і переліком")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Result<std::vector<DocumentLine>> composeServiceSale(const std::vector<Account> & accounts,
                                                     const std::vector<GoodsLine> & rows)
{
  const AccountId customers = accountIdByCode(accounts, chart::kCustomers);
  const AccountId revenue = accountIdByCode(accounts, chart::kServiceRevenue);
  if (!customers || !revenue) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 361 або 703")));
  }
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const GoodsLine & row : rows) {
    ++rowNo;
    if (!row.nomenclatureId && row.amount.isZero() && row.quantity.isZero() &&
        row.description.trimmed().isEmpty()) {
      continue;
    }
    if (!row.amount.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: вкажіть суму послуги").arg(rowNo)));
    }
    DocumentLine line = posting(customers, revenue, row.amount, row.description);
    line.nomenclatureId = row.nomenclatureId;
    line.quantity = row.quantity;
    line.unitPrice = row.unitPrice;
    copyVat(line, row.vatRateBp, row.vatCents);
    lines.push_back(line);
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б одну послугу")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Result<std::vector<DocumentLine>> composeServicePurchase(const std::vector<Account> & accounts,
                                                         const std::vector<CashLine> & rows)
{
  const AccountId suppliers = accountIdByCode(accounts, chart::kSuppliers);
  if (!suppliers) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 631")));
  }
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const CashLine & row : rows) {
    ++rowNo;
    if (!row.correspondenceId && row.amount.isZero() && row.description.trimmed().isEmpty()) {
      continue;
    }
    if (!row.correspondenceId) {
      return Result<std::vector<DocumentLine>>::fail(Error::validation(
          QStringLiteral("рядок %1: оберіть рахунок витрат або капітальних інвестицій")
              .arg(rowNo)));
    }
    bool allowed = false;
    for (const Account & account : accounts) {
      if (account.id == row.correspondenceId) {
        allowed = isServicePurchaseDebitCode(account.code);
        break;
      }
    }
    if (!allowed) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: дебет має бути 92, 152 або 154").arg(rowNo)));
    }
    if (!row.amount.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: вкажіть суму послуги").arg(rowNo)));
    }
    lines.push_back(posting(row.correspondenceId, suppliers, row.amount, row.description));
    copyVat(lines.back(), row.vatRateBp, row.vatCents);
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б одну послугу")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Result<std::vector<DocumentLine>> composeCustomerInvoice(const std::vector<Account> & accounts,
                                                         const std::vector<GoodsLine> & rows)
{
  const AccountId customers = accountIdByCode(accounts, chart::kCustomers);
  const AccountId revenue = accountIdByCode(accounts, chart::kRevenue);
  if (!customers || !revenue) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 361 або 701")));
  }
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const GoodsLine & row : rows) {
    ++rowNo;
    if (!row.nomenclatureId && row.amount.isZero() && row.quantity.isZero()) {
      continue;
    }
    if (!row.nomenclatureId) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: оберіть номенклатуру").arg(rowNo)));
    }
    if (!row.quantity.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: вкажіть кількість").arg(rowNo)));
    }
    if (!row.amount.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: вкажіть суму").arg(rowNo)));
    }
    DocumentLine line = posting(customers, revenue, row.amount, row.description);
    line.nomenclatureId = row.nomenclatureId;
    line.quantity = row.quantity;
    line.unitPrice = row.unitPrice;
    copyVat(line, row.vatRateBp, row.vatCents);
    lines.push_back(line);
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б один товар")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Result<std::vector<DocumentLine>> composeStockTransfer(const std::vector<Account> & accounts,
                                                       const std::vector<GoodsLine> & rows)
{
  const AccountId goods = accountIdByCode(accounts, chart::kGoods);
  if (!goods) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 281")));
  }
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const GoodsLine & row : rows) {
    ++rowNo;
    if (!row.nomenclatureId && row.quantity.isZero()) {
      continue;
    }
    if (!row.nomenclatureId) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: оберіть номенклатуру").arg(rowNo)));
    }
    if (!row.quantity.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: вкажіть кількість").arg(rowNo)));
    }
    DocumentLine line = posting(
        goods, goods, row.amount.isPositive() ? row.amount : Money::fromCents(1), row.description);
    line.nomenclatureId = row.nomenclatureId;
    line.quantity = row.quantity;
    line.unitPrice = row.unitPrice;
    lines.push_back(line);
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б один товар")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

namespace {

Result<std::vector<DocumentLine>> composeKitLines(const std::vector<Account> & accounts,
                                                  const std::vector<GoodsLine> & rows,
                                                  const QString & emptyMsg)
{
  const AccountId goods = accountIdByCode(accounts, chart::kGoods);
  if (!goods) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 281")));
  }
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const GoodsLine & row : rows) {
    ++rowNo;
    if (!row.nomenclatureId && row.quantity.isZero()) {
      continue;
    }
    if (!row.nomenclatureId) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: оберіть номенклатуру").arg(rowNo)));
    }
    if (!row.quantity.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: вкажіть кількість").arg(rowNo)));
    }
    DocumentLine line = posting(
        goods, goods, row.amount.isPositive() ? row.amount : Money::fromCents(1), row.description);
    line.nomenclatureId = row.nomenclatureId;
    line.quantity = row.quantity;
    line.unitPrice = row.unitPrice;
    lines.push_back(line);
  }
  if (lines.size() < 2) {
    return Result<std::vector<DocumentLine>>::fail(Error::validation(emptyMsg));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

} // namespace

Result<std::vector<DocumentLine>> composeKitAssembly(const std::vector<Account> & accounts,
                                                     const std::vector<GoodsLine> & rows)
{
  return composeKitLines(
      accounts, rows,
      QStringLiteral("комплектація: рядок 1 — комплект, далі — хоча б один компонент"));
}

Result<std::vector<DocumentLine>> composeKitDisassembly(const std::vector<Account> & accounts,
                                                        const std::vector<GoodsLine> & rows)
{
  return composeKitLines(
      accounts, rows,
      QStringLiteral("розукомплектація: рядок 1 — комплект, далі — хоча б один компонент"));
}

Result<std::vector<DocumentLine>> composeCustomerOrder(const std::vector<Account> & accounts,
                                                       const std::vector<GoodsLine> & rows)
{
  return composeCustomerInvoice(accounts, rows);
}

Result<std::vector<DocumentLine>> composeSupplierOrder(const std::vector<Account> & accounts,
                                                       const std::vector<GoodsLine> & rows)
{
  const AccountId goods = accountIdByCode(accounts, chart::kGoods);
  const AccountId suppliers = accountIdByCode(accounts, chart::kSuppliers);
  if (!goods || !suppliers) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 281 або 631")));
  }
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const GoodsLine & row : rows) {
    ++rowNo;
    if (!row.nomenclatureId && row.amount.isZero() && row.quantity.isZero()) {
      continue;
    }
    if (!row.nomenclatureId) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: оберіть номенклатуру").arg(rowNo)));
    }
    if (!row.quantity.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: вкажіть кількість").arg(rowNo)));
    }
    if (!row.amount.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: вкажіть суму").arg(rowNo)));
    }
    DocumentLine line = posting(goods, suppliers, row.amount, row.description);
    line.nomenclatureId = row.nomenclatureId;
    line.quantity = row.quantity;
    line.unitPrice = row.unitPrice;
    copyVat(line, row.vatRateBp, row.vatCents);
    lines.push_back(line);
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б один товар")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Result<std::vector<DocumentLine>> composeMaterialIssue(const std::vector<Account> & accounts,
                                                       const std::vector<GoodsLine> & rows)
{
  const AccountId production = accountIdByCode(accounts, chart::kProduction);
  const AccountId rawMaterials = accountIdByCode(accounts, chart::kRawMaterials);
  if (!production || !rawMaterials) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 231 або 201")));
  }
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const GoodsLine & row : rows) {
    ++rowNo;
    if (!row.nomenclatureId && row.amount.isZero() && row.quantity.isZero()) {
      continue;
    }
    if (!row.nomenclatureId) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: оберіть матеріал").arg(rowNo)));
    }
    if (!row.quantity.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: вкажіть кількість").arg(rowNo)));
    }
    DocumentLine line = posting(production, rawMaterials, row.amount, row.description);
    line.nomenclatureId = row.nomenclatureId;
    line.quantity = row.quantity;
    line.unitPrice = row.unitPrice;
    lines.push_back(line);
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б один матеріал")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Result<std::vector<DocumentLine>> composeProductionReport(const std::vector<Account> & accounts,
                                                          const std::vector<GoodsLine> & rows)
{
  const AccountId finishedGoods = accountIdByCode(accounts, chart::kFinishedGoods);
  const AccountId production = accountIdByCode(accounts, chart::kProduction);
  if (!finishedGoods || !production) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 26 або 231")));
  }
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const GoodsLine & row : rows) {
    ++rowNo;
    if (!row.nomenclatureId && row.amount.isZero() && row.quantity.isZero()) {
      continue;
    }
    if (!row.nomenclatureId) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: оберіть продукцію").arg(rowNo)));
    }
    if (!row.quantity.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: вкажіть кількість").arg(rowNo)));
    }
    DocumentLine line = posting(finishedGoods, production, row.amount, row.description);
    line.nomenclatureId = row.nomenclatureId;
    line.quantity = row.quantity;
    line.unitPrice = row.unitPrice;
    lines.push_back(line);
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б один виріб")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

std::vector<GoodsLine> extractGoodsLines(DocumentType type, const std::vector<DocumentLine> & lines,
                                         const std::vector<Account> & accounts)
{
  std::vector<GoodsLine> rows;
  if (type == DocumentType::MaterialIssue || type == DocumentType::ProductionReport) {
    for (const DocumentLine & line : lines) {
      if (!line.nomenclatureId && line.quantity.isZero() && line.amount.isZero()) {
        continue;
      }
      GoodsLine row;
      row.nomenclatureId = line.nomenclatureId;
      row.quantity = line.quantity;
      row.unitPrice = line.unitPrice;
      row.amount = line.amount;
      row.description = line.description;
      copyVat(row, line);
      rows.push_back(row);
    }
    return rows;
  }
  if (type == DocumentType::ServiceSale || type == DocumentType::CustomerInvoice ||
      type == DocumentType::CustomerOrder) {
    const char * revenueCode =
        type == DocumentType::ServiceSale ? chart::kServiceRevenue : chart::kRevenue;
    for (const DocumentLine & line : lines) {
      if (!accountHasCode(accounts, line.debitAccountId, chart::kCustomers) ||
          !accountHasCode(accounts, line.creditAccountId, revenueCode)) {
        continue;
      }
      GoodsLine row;
      row.nomenclatureId = line.nomenclatureId;
      row.quantity = line.quantity;
      row.unitPrice = line.unitPrice;
      row.amount = line.amount;
      row.description = line.description;
      copyVat(row, line);
      rows.push_back(row);
    }
    return rows;
  }
  if (type == DocumentType::Purchase || type == DocumentType::OpeningBalance ||
      type == DocumentType::Stocktake || type == DocumentType::StockTransfer ||
      type == DocumentType::KitAssembly || type == DocumentType::KitDisassembly ||
      type == DocumentType::SupplierOrder) {
    for (const DocumentLine & line : lines) {
      const bool isStock =
          accountHasCode(accounts, line.debitAccountId, chart::kGoods) ||
          accountHasCode(accounts, line.debitAccountId, chart::kRawMaterials) ||
          accountHasCode(accounts, line.debitAccountId, chart::kPurchasedComponents) ||
          accountHasCode(accounts, line.debitAccountId, chart::kFuel) ||
          accountHasCode(accounts, line.debitAccountId, chart::kFinishedGoods) ||
          accountHasCode(accounts, line.creditAccountId, chart::kGoods) ||
          accountHasCode(accounts, line.creditAccountId, chart::kRawMaterials) ||
          accountHasCode(accounts, line.creditAccountId, chart::kPurchasedComponents) ||
          accountHasCode(accounts, line.creditAccountId, chart::kFuel) ||
          accountHasCode(accounts, line.creditAccountId, chart::kFinishedGoods);
      if (!isStock) {
        continue;
      }
      GoodsLine row;
      row.nomenclatureId = line.nomenclatureId;
      row.quantity = line.quantity;
      row.unitPrice = line.unitPrice;
      row.amount = line.amount;
      row.description = line.description;
      copyVat(row, line);
      rows.push_back(row);
    }
    return rows;
  }
  if (type == DocumentType::PurchaseReturn || type == DocumentType::WriteOff) {
    for (const DocumentLine & line : lines) {
      const bool isStock =
          accountHasCode(accounts, line.creditAccountId, chart::kGoods) ||
          accountHasCode(accounts, line.creditAccountId, chart::kRawMaterials) ||
          accountHasCode(accounts, line.creditAccountId, chart::kPurchasedComponents) ||
          accountHasCode(accounts, line.creditAccountId, chart::kFuel) ||
          accountHasCode(accounts, line.creditAccountId, chart::kFinishedGoods);
      if (!isStock) {
        continue;
      }
      GoodsLine row;
      row.nomenclatureId = line.nomenclatureId;
      row.quantity = line.quantity;
      row.unitPrice = line.unitPrice;
      row.amount = line.amount;
      row.description = line.description;
      copyVat(row, line);
      rows.push_back(row);
    }
    return rows;
  }
  if (type != DocumentType::Sale && type != DocumentType::SaleReturn) {
    return rows;
  }
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const DocumentLine & line = lines[i];
    const bool revenue =
        type == DocumentType::Sale
            ? accountHasCode(accounts, line.debitAccountId, chart::kCustomers) &&
                  accountHasCode(accounts, line.creditAccountId, chart::kRevenue)
            : accountHasCode(accounts, line.debitAccountId, chart::kRevenue) &&
                  accountHasCode(accounts, line.creditAccountId, chart::kCustomers);
    if (!revenue) {
      continue;
    }
    GoodsLine row;
    row.amount = line.amount;
    row.unitPrice = line.unitPrice;
    row.description = line.description;
    copyVat(row, line);
    if (i + 1 < lines.size()) {
      const DocumentLine & stock = lines[i + 1];
      const bool isStock =
          type == DocumentType::Sale
              ? (accountHasCode(accounts, stock.debitAccountId, chart::kCogs) &&
                 accountHasCode(accounts, stock.creditAccountId, chart::kGoods)) ||
                    (accountHasCode(accounts, stock.debitAccountId, chart::kProductionCogs) &&
                     accountHasCode(accounts, stock.creditAccountId, chart::kFinishedGoods))
              : (accountHasCode(accounts, stock.debitAccountId, chart::kGoods) &&
                 accountHasCode(accounts, stock.creditAccountId, chart::kCogs)) ||
                    (accountHasCode(accounts, stock.debitAccountId, chart::kFinishedGoods) &&
                     accountHasCode(accounts, stock.creditAccountId, chart::kProductionCogs));
      if (isStock) {
        row.nomenclatureId = stock.nomenclatureId;
        row.quantity = stock.quantity;
        ++i;
      }
    }
    rows.push_back(row);
  }
  return rows;
}

std::vector<GoodsLine> extractStocktakeCounted(const std::vector<DocumentLine> & lines,
                                               const std::vector<Account> & accounts,
                                               const std::vector<StockBalance> & onHand)
{
  std::vector<GoodsLine> rows;
  for (const StockBalance & book : onHand) {
    GoodsLine row;
    row.nomenclatureId = book.nomenclatureId;
    row.quantity = book.quantity;
    row.amount = Money::zero();
    rows.push_back(row);
  }
  auto findRow = [&rows](NomenclatureId id) -> GoodsLine * {
    for (GoodsLine & row : rows) {
      if (row.nomenclatureId == id) {
        return &row;
      }
    }
    return nullptr;
  };
  for (const DocumentLine & line : lines) {
    if (!line.nomenclatureId) {
      continue;
    }
    GoodsLine * row = findRow(line.nomenclatureId);
    if (row == nullptr) {
      GoodsLine created;
      created.nomenclatureId = line.nomenclatureId;
      rows.push_back(created);
      row = &rows.back();
    }
    row->description = line.description;
    if (accountHasCode(accounts, line.debitAccountId, chart::kGoods) &&
        accountHasCode(accounts, line.creditAccountId, chart::kOtherIncome)) {
      const Result<Quantity> counted = row->quantity.plus(line.quantity);
      if (counted) {
        row->quantity = counted.value();
      }
      row->amount = line.amount;
      row->unitPrice = line.unitPrice;
    } else if (accountHasCode(accounts, line.creditAccountId, chart::kGoods)) {
      const Result<Quantity> negated = line.quantity.negated();
      if (negated) {
        const Result<Quantity> counted = row->quantity.plus(negated.value());
        if (counted && !counted.value().isNegative()) {
          row->quantity = counted.value();
        }
      }
    }
  }
  return rows;
}

std::vector<CashLine> extractCashLines(DocumentType type, const std::vector<DocumentLine> & lines,
                                       const std::vector<Account> & accounts)
{
  std::vector<CashLine> rows;
  for (const DocumentLine & line : lines) {
    CashLine row;
    row.amount = line.amount;
    row.description = line.description;
    if (type == DocumentType::CashIn &&
        accountHasCode(accounts, line.debitAccountId, chart::kCash)) {
      row.correspondenceId = line.creditAccountId;
      rows.push_back(row);
    } else if (type == DocumentType::CashOut &&
               accountHasCode(accounts, line.creditAccountId, chart::kCash)) {
      row.correspondenceId = line.debitAccountId;
      rows.push_back(row);
    }
  }
  return rows;
}

std::vector<CashLine> extractServicePurchaseLines(const std::vector<DocumentLine> & lines,
                                                  const std::vector<Account> & accounts)
{
  std::vector<CashLine> rows;
  for (const DocumentLine & line : lines) {
    if (!accountHasCode(accounts, line.creditAccountId, chart::kSuppliers)) {
      continue;
    }
    QString debitCode;
    for (const Account & account : accounts) {
      if (account.id == line.debitAccountId) {
        debitCode = account.code;
        break;
      }
    }
    if (!isServicePurchaseDebitCode(debitCode)) {
      continue;
    }
    CashLine row;
    row.correspondenceId = line.debitAccountId;
    row.amount = line.amount;
    row.description = line.description;
    row.vatRateBp = line.vatRateBp;
    row.vatCents = line.vatCents;
    rows.push_back(row);
  }
  return rows;
}

Money saleRevenueTotal(const std::vector<DocumentLine> & lines,
                       const std::vector<Account> & accounts)
{
  Money total = Money::zero();
  for (const DocumentLine & line : lines) {
    if (!accountHasCode(accounts, line.debitAccountId, chart::kCustomers) ||
        !accountHasCode(accounts, line.creditAccountId, chart::kRevenue)) {
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

std::vector<BankLine> extractBankLines(const std::vector<DocumentLine> & lines,
                                       const std::vector<Account> & accounts)
{
  std::vector<BankLine> rows;
  for (const DocumentLine & line : lines) {
    if (!accountHasCode(accounts, line.debitAccountId, chart::kBank) &&
        !accountHasCode(accounts, line.creditAccountId, chart::kBank)) {
      continue;
    }
    BankLine row;
    row.amount = line.amount;
    row.description = line.description;
    row.correspondenceId = accountHasCode(accounts, line.debitAccountId, chart::kBank)
                               ? line.creditAccountId
                               : line.debitAccountId;
    rows.push_back(row);
  }
  return rows;
}

bool bankDocumentIncoming(const std::vector<DocumentLine> & lines,
                          const std::vector<Account> & accounts)
{
  for (const DocumentLine & line : lines) {
    if (accountHasCode(accounts, line.debitAccountId, chart::kBank)) {
      return true;
    }
    if (accountHasCode(accounts, line.creditAccountId, chart::kBank)) {
      return false;
    }
  }
  return true;
}

std::vector<OpeningMoneyLine> extractOpeningMoneyLines(const std::vector<DocumentLine> & lines,
                                                       const std::vector<Account> & accounts)
{
  std::vector<OpeningMoneyLine> rows;
  const AccountId aux = accountIdByCode(accounts, chart::kAuxiliary);
  for (const DocumentLine & line : lines) {
    if (accountHasCode(accounts, line.debitAccountId, chart::kGoods) ||
        accountHasCode(accounts, line.creditAccountId, chart::kGoods)) {
      continue;
    }
    OpeningMoneyLine row;
    if (line.creditAccountId == aux) {
      row.accountId = line.debitAccountId;
      row.debit = line.amount;
    } else if (line.debitAccountId == aux) {
      row.accountId = line.creditAccountId;
      row.credit = line.amount;
    } else {
      continue;
    }
    row.counterpartyId = line.counterpartyId;
    row.contractId = line.contractId;
    rows.push_back(row);
  }
  return rows;
}

Money goodsLineTotal(const std::vector<GoodsLine> & rows)
{
  Money total = Money::zero();
  for (const GoodsLine & row : rows) {
    const Result<Money> next = total.plus(row.amount);
    if (!next) {
      return total;
    }
    total = next.value();
  }
  return total;
}

Result<std::vector<DocumentLine>> composeExpenseReport(const std::vector<Account> & accounts,
                                                       const std::vector<ExpenseLine> & rows,
                                                       AccountId creditAccountId)
{
  AccountId credit = creditAccountId;
  if (!credit) {
    credit = accountIdByCode(accounts, chart::kAccountablePersonsUah);
  }
  if (!credit) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 3721")));
  }
  const AccountId vatCreditCheques = accountIdByCode(accounts, chart::kTaxCreditCheques);
  const AccountId vatCreditGeneral = accountIdByCode(accounts, chart::kVatCredit);
  const AccountId vatCreditAccount = vatCreditCheques ? vatCreditCheques : vatCreditGeneral;

  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const ExpenseLine & row : rows) {
    ++rowNo;
    if (!row.expenseAccountId && row.amount.isZero() && row.description.trimmed().isEmpty() &&
        row.documentRef.trimmed().isEmpty()) {
      continue;
    }
    if (!row.expenseAccountId) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: оберіть рахунок витрат/ТМЦ").arg(rowNo)));
    }
    if (!row.amount.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: сума витрат має бути додатною").arg(rowNo)));
    }
    QString desc = row.description;
    if (!row.documentRef.trimmed().isEmpty()) {
      if (desc.isEmpty()) {
        desc = row.documentRef.trimmed();
      } else {
        desc = QStringLiteral("[%1] %2").arg(row.documentRef.trimmed(), desc);
      }
    }
    DocumentLine line = posting(row.expenseAccountId, credit, row.amount, desc);
    line.nomenclatureId = row.nomenclatureId;
    line.quantity = row.quantity;
    line.unitPrice = row.unitPrice;
    copyVat(line, row.vatRateBp, row.vatCents);
    lines.push_back(line);

    if (row.vatCents > 0) {
      if (!vatCreditAccount) {
        return Result<std::vector<DocumentLine>>::fail(Error::validation(
            QStringLiteral("у плані рахунків немає 6442 або 644 для обліку ПДВ")));
      }
      DocumentLine vatLine = posting(vatCreditAccount, credit, Money::fromCents(row.vatCents),
                                     QStringLiteral("ПДВ: %1").arg(desc));
      lines.push_back(vatLine);
    }
  }
  if (lines.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б один рядок витрат")));
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

std::vector<ExpenseLine> extractExpenseLines(const std::vector<DocumentLine> & lines,
                                             const std::vector<Account> & accounts)
{
  std::vector<ExpenseLine> rows;
  for (const DocumentLine & line : lines) {
    if (accountHasCode(accounts, line.debitAccountId, chart::kTaxCreditCheques) ||
        accountHasCode(accounts, line.debitAccountId, chart::kVatCredit)) {
      continue;
    }
    if (!accountHasCode(accounts, line.creditAccountId, chart::kAccountablePersons) &&
        !accountHasCode(accounts, line.creditAccountId, chart::kAccountablePersonsUah) &&
        !accountHasCode(accounts, line.creditAccountId, chart::kAccountablePersonsVal)) {
      continue;
    }
    ExpenseLine row;
    row.expenseAccountId = line.debitAccountId;
    row.nomenclatureId = line.nomenclatureId;
    row.quantity = line.quantity;
    row.unitPrice = line.unitPrice;
    row.amount = line.amount;
    row.vatRateBp = line.vatRateBp;
    row.vatCents = line.vatCents;

    QString desc = line.description;
    if (desc.startsWith(QLatin1Char('['))) {
      const qsizetype closeIdx = desc.indexOf(QLatin1Char(']'));
      if (closeIdx > 0) {
        row.documentRef = desc.mid(1, closeIdx - 1).trimmed();
        row.description = desc.mid(closeIdx + 1).trimmed();
      } else {
        row.description = desc;
      }
    } else {
      row.description = desc;
    }
    rows.push_back(row);
  }
  return rows;
}

Money expenseLineTotal(const std::vector<ExpenseLine> & rows)
{
  Money total = Money::zero();
  for (const ExpenseLine & row : rows) {
    const Result<Money> next = total.plus(row.amount);
    if (next) {
      total = next.value();
    }
    if (row.vatCents > 0) {
      const Result<Money> withVat = total.plus(Money::fromCents(row.vatCents));
      if (withVat) {
        total = withVat.value();
      }
    }
  }
  return total;
}

} // namespace fin
