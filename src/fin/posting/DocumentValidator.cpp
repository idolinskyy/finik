#include "fin/posting/DocumentValidator.h"

#include "fin/core/Sql.h"
#include "fin/db/AccountRepository.h"
#include "fin/db/CompanyRepository.h"
#include "fin/db/DocumentRepository.h"
#include "fin/db/EmployeeRepository.h"
#include "fin/db/FixedAssetRepository.h"
#include "fin/db/IntangibleAssetRepository.h"
#include "fin/db/LowValueRepository.h"
#include "fin/db/NomenclatureRepository.h"
#include "fin/db/SubcontoRepository.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/Subconto.h"
#include "fin/domain/Employee.h"
#include "fin/domain/FixedAsset.h"
#include "fin/domain/IntangibleAsset.h"
#include "fin/domain/LowValueItem.h"
#include "fin/domain/Vat.h"
#include "fin/posting/AdvanceOffset.h"

#include <QHash>
#include <QLatin1String>
#include <QSet>
#include <QSqlDatabase>

namespace fin {
namespace {

Status requireCounterparty(const Document & document)
{
  if (!document.counterpartyId) {
    return Status::fail(
        Error::validation(QStringLiteral("для цього документа потрібен контрагент")));
  }
  return Status::ok();
}

bool isCode(const Account & account, const char * code)
{
  return account.code.compare(QLatin1String(code), Qt::CaseInsensitive) == 0;
}

bool needsPartyAccount(const Account & account)
{
  return isCode(account, chart::kCustomers) || isCode(account, chart::kSuppliers) ||
         isCode(account, chart::kAdvancesIssued) || isCode(account, chart::kAdvancesReceived) ||
         isCode(account, chart::kForeignCustomers) || isCode(account, chart::kForeignSuppliers) ||
         isCode(account, chart::kForeignAdvancesIssued) ||
         isCode(account, chart::kForeignAdvancesReceived);
}

Result<Account> loadAccount(QSqlDatabase db, AccountId id)
{
  return AccountRepository(db).findById(id);
}

bool isStockAccount(const Account & account)
{
  return isCode(account, chart::kGoods) || isCode(account, chart::kRawMaterials) ||
         isCode(account, chart::kPurchasedComponents) || isCode(account, chart::kFuel) ||
         isCode(account, chart::kFinishedGoods);
}

Status validateGoodsLine(QSqlDatabase db, const Document & document, const DocumentLine & line,
                         int lineNo, const Account & debit, const Account & credit)
{
  const bool debitGoods = isStockAccount(debit);
  const bool creditGoods = isStockAccount(credit);
  if (!debitGoods && !creditGoods) {
    return Status::ok();
  }
  const bool allowSameStock = document.type == DocumentType::StockTransfer ||
                              document.type == DocumentType::KitAssembly ||
                              document.type == DocumentType::KitDisassembly;
  if (debitGoods && creditGoods && !allowSameStock) {
    return Status::fail(Error::validation(
        QStringLiteral(
            "рядок %1: дебет і кредит не можуть одночасно бути запасами (крім переміщення/"
            "комплектації)")
            .arg(lineNo)));
  }
  if (!line.nomenclatureId) {
    return Status::fail(Error::validation(
        QStringLiteral("рядок %1: для рахунку обліку запасів потрібна номенклатура").arg(lineNo)));
  }
  NomenclatureRepository items(db);
  Result<Nomenclature> item = items.findById(line.nomenclatureId);
  if (!item) {
    return Status::fail(
        Error::validation(QStringLiteral("рядок %1: номенклатуру не знайдено").arg(lineNo)));
  }
  if (!item.value().isActive) {
    return Status::fail(
        Error::validation(QStringLiteral("рядок %1: номенклатура неактивна").arg(lineNo)));
  }
  if (!line.quantity.isPositive()) {
    return Status::fail(Error::validation(
        QStringLiteral("рядок %1: кількість має бути більшою за нуль").arg(lineNo)));
  }
  if (line.unitPrice.isPositive() && !creditGoods && !allowSameStock) {
    const Result<Money> expected = line.quantity.times(line.unitPrice);
    if (!expected) {
      return expected.asStatus();
    }
    if (expected.value() != line.amount) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: сума має дорівнювати кількість × ціна").arg(lineNo)));
    }
  }
  return Status::ok();
}

Status validateAccountSubconto(QSqlDatabase db, AccountId accountId, const Document & document,
                               const DocumentLine & line, int lineNo, const QString & sideLabel)
{
  // Закриття місяця формує згорнуті проводки (79x / 44x) без аналітики; решта типів,
  // включно з ручною операцією та введенням залишків, заповнює обов'язкові субконто.
  if (document.type == DocumentType::PeriodResult) {
    return Status::ok();
  }
  SubcontoRepository repo(db);
  const auto listRes = repo.listForAccount(accountId);
  if (!listRes) {
    return listRes.asStatus();
  }
  for (const auto & setting : listRes.value()) {
    if (!setting.isRequired) {
      continue;
    }
    bool provided = false;
    if (setting.subcontoKindId == subconto_kinds::Counterparty) {
      // Як і в PostingEngine::extractSubcontoSet: контрагент рядка має перевагу
      // над контрагентом документа.
      provided = line.counterpartyId.isValid() || document.counterpartyId.isValid();
    } else if (setting.subcontoKindId == subconto_kinds::Nomenclature) {
      provided = line.nomenclatureId.isValid();
    } else if (setting.subcontoKindId == subconto_kinds::FixedAsset) {
      if (document.type == DocumentType::ServicePurchase ||
          document.type == DocumentType::ServiceSale ||
          document.type == DocumentType::BankPayment || document.type == DocumentType::CashIn ||
          document.type == DocumentType::CashOut) {
        continue;
      }
      provided = line.fixedAssetId.isValid();
    } else if (setting.subcontoKindId == subconto_kinds::IntangibleAsset) {
      if (document.type == DocumentType::ServicePurchase ||
          document.type == DocumentType::ServiceSale ||
          document.type == DocumentType::BankPayment || document.type == DocumentType::CashIn ||
          document.type == DocumentType::CashOut) {
        continue;
      }
      provided = line.intangibleId.isValid();
    } else if (setting.subcontoKindId == subconto_kinds::LowValue) {
      provided = line.lowValueId.isValid();
    } else if (setting.subcontoKindId == subconto_kinds::Employee) {
      if (document.type == DocumentType::BankPayment || document.type == DocumentType::CashOut ||
          document.type == DocumentType::CashIn) {
        continue;
      }
      provided = line.employeeId.isValid() || document.employeeId.isValid();
    } else if (setting.subcontoKindId == subconto_kinds::Contract) {
      provided = document.contractId.isValid();
    }
    if (!provided) {
      const auto kindRes = repo.findKind(setting.subcontoKindId);
      const QString kindName = kindRes ? kindRes.value().nameUa : setting.subcontoKindId;
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1 (%2): обов'язкове субконто '%3' не заповнено")
                                .arg(QString::number(lineNo), sideLabel, kindName)));
    }
  }
  return Status::ok();
}

} // namespace

DocumentValidator::DocumentValidator(QSqlDatabase db) : db_(std::move(db)) {}

Result<Account> DocumentValidator::requirePostable(AccountId id, const QString & role) const
{
  AccountRepository accounts(db_);
  Result<Account> account = accounts.findById(id);
  if (!account) {
    return Result<Account>::fail(
        Error::validation(QStringLiteral("%1: рахунок не знайдено").arg(role)));
  }
  if (!account.value().postable()) {
    return Result<Account>::fail(Error::validation(
        QStringLiteral("%1: рахунок %2 не приймає проводок").arg(role, account.value().code)));
  }
  return account;
}

Status DocumentValidator::validate(const Document & document) const
{
  if (!document.date.isValid() || document.date.year() < 1991 || document.date.year() > 2100) {
    return Status::fail(Error::validation(QStringLiteral("некоректна дата документа")));
  }
  if (document.lines.empty()) {
    return Status::fail(Error::validation(QStringLiteral("документ не містить рядків")));
  }
  const QString currency = document.currencyCode.trimmed().toUpper();
  if (!currency.isEmpty() && currency != QLatin1String("UAH")) {
    if (document.currencyRateMicros <= 0 || document.currencyNominal <= 0) {
      return Status::fail(Error::validation(
          QStringLiteral("для валютного документа (%1) потрібні курс і номінал").arg(currency)));
    }
  }

  const Result<std::vector<Account>> accounts = AccountRepository(db_).listPostable();
  if (!accounts) {
    return accounts.asStatus();
  }

  int lineNo = 0;
  for (const DocumentLine & line : document.lines) {
    ++lineNo;
    if (line.amount.cents() <= 0) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: сума має бути більшою за нуль").arg(lineNo)));
    }
    if (line.debitAccountId == line.creditAccountId &&
        document.type != DocumentType::StockTransfer &&
        document.type != DocumentType::KitAssembly &&
        document.type != DocumentType::KitDisassembly) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: дебет і кредит мають відрізнятися").arg(lineNo)));
    }
    const Result<Account> debit =
        requirePostable(line.debitAccountId, QStringLiteral("рядок %1, дебет").arg(lineNo));
    if (!debit) {
      return debit.asStatus();
    }
    const Result<Account> credit =
        requirePostable(line.creditAccountId, QStringLiteral("рядок %1, кредит").arg(lineNo));
    if (!credit) {
      return credit.asStatus();
    }
    if (document.type != DocumentType::OpeningBalance &&
        document.type != DocumentType::FaCommission &&
        document.type != DocumentType::IaCommission) {
      if (isCode(debit.value(), chart::kAuxiliary) || isCode(credit.value(), chart::kAuxiliary)) {
        return Status::fail(Error::validation(
            QStringLiteral("рахунок 00 дозволений лише у введенні залишків і введенні ОЗ/НМА")));
      }
    }
    if (isAdvanceOffsetLine(accounts.value(), document.type, line)) {
      continue;
    }
    if (document.type == DocumentType::TaxInvoice &&
        isVatRegistrationLine(accounts.value(), line)) {
      continue;
    }
    const Status goods =
        validateGoodsLine(db_, document, line, lineNo, debit.value(), credit.value());
    if (!goods) {
      return goods;
    }
    const Status dtSubconto = validateAccountSubconto(db_, debit.value().id, document, line, lineNo,
                                                      QStringLiteral("дебет"));
    if (!dtSubconto) {
      return dtSubconto;
    }
    const Status ctSubconto = validateAccountSubconto(db_, credit.value().id, document, line,
                                                      lineNo, QStringLiteral("кредит"));
    if (!ctSubconto) {
      return ctSubconto;
    }
    if (line.vatCents < 0 || line.vatRateBp < 0 || line.vatRateBp > 10000) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: некоректні поля ПДВ").arg(lineNo)));
    }
    if (line.vatCents > 0 && line.vatRateBp > 0 && document.type != DocumentType::PurchaseReturn &&
        document.type != DocumentType::WriteOff && !isCode(debit.value(), chart::kCogs)) {
      const Result<Money> expected = vatOnNet(line.amount, line.vatRateBp);
      if (!expected) {
        return expected.asStatus();
      }
      if (expected.value().cents() != line.vatCents) {
        return Status::fail(Error::validation(
            QStringLiteral("рядок %1: ПДВ має дорівнювати сумі × ставці").arg(lineNo)));
      }
    }
  }

  switch (document.type) {
  case DocumentType::OpeningBalance:
    return validateOpening(document);
  case DocumentType::Manual:
    return Status::ok();
  case DocumentType::CashIn:
    return validateCashIn(document);
  case DocumentType::CashOut:
    return validateCashOut(document);
  case DocumentType::Purchase:
    return validatePurchase(document);
  case DocumentType::Sale:
    return validateSale(document);
  case DocumentType::BankPayment:
    return validateBank(document);
  case DocumentType::SaleReturn:
    return validateSaleReturn(document);
  case DocumentType::PurchaseReturn:
    return validatePurchaseReturn(document);
  case DocumentType::WriteOff:
    return validateWriteOff(document);
  case DocumentType::Stocktake:
    return validateStocktake(document);
  case DocumentType::PeriodResult:
    return validatePeriodResult(document);
  case DocumentType::ServiceSale:
    return validateServiceSale(document);
  case DocumentType::ServicePurchase:
    return validateServicePurchase(document);
  case DocumentType::CustomerInvoice:
    return validateCustomerInvoice(document);
  case DocumentType::StockTransfer:
    return validateStockTransfer(document);
  case DocumentType::KitAssembly:
  case DocumentType::KitDisassembly:
    return validateKitDocument(document);
  case DocumentType::CustomerOrder:
  case DocumentType::SupplierOrder:
    return Status::ok();
  case DocumentType::FaCommission:
    return validateFaCommission(document);
  case DocumentType::FaDepreciation:
    return validateFaDepreciation(document);
  case DocumentType::FaWriteOff:
    return validateFaWriteOff(document);
  case DocumentType::IaCommission:
    return validateIaCommission(document);
  case DocumentType::IaDepreciation:
    return validateIaDepreciation(document);
  case DocumentType::IaWriteOff:
    return validateIaWriteOff(document);
  case DocumentType::MsReceipt:
    return validateMsReceipt(document);
  case DocumentType::MsIssue:
    return validateMsIssue(document);
  case DocumentType::MaterialIssue:
    return validateMaterialIssue(document);
  case DocumentType::ProductionReport:
    return validateProductionReport(document);
  case DocumentType::TaxInvoice:
    return validateTaxInvoice(document);
  case DocumentType::Payroll:
  case DocumentType::VacationPayroll:
  case DocumentType::SickLeavePayroll:
    return validatePayroll(document);
  case DocumentType::ExpenseReport:
    return validateExpenseReport(document);
  }
  return Status::ok();
}

Status DocumentValidator::validateStockTransfer(const Document & document) const
{
  if (!document.warehouseId) {
    return Status::fail(
        Error::validation(QStringLiteral("для переміщення товарів вкажіть склад-відправник")));
  }
  if (!document.targetWarehouseId) {
    return Status::fail(
        Error::validation(QStringLiteral("для переміщення товарів вкажіть склад-одержувач")));
  }
  if (document.warehouseId == document.targetWarehouseId) {
    return Status::fail(Error::validation(
        QStringLiteral("склад-відправник та склад-одержувач не можуть збігатися")));
  }
  for (const DocumentLine & line : document.lines) {
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit || !credit) {
      return Status::fail(Error::validation(QStringLiteral("не вдалося визначити рахунки рядка")));
    }
    if (!isCode(debit.value(), chart::kGoods) || !isCode(credit.value(), chart::kGoods)) {
      return Status::fail(Error::validation(
          QStringLiteral("переміщення товарів можливе лише між субрахунками 281")));
    }
  }
  return Status::ok();
}

Status DocumentValidator::validateKitDocument(const Document & document) const
{
  if (!document.warehouseId) {
    return Status::fail(
        Error::validation(QStringLiteral("для комплектації/розукомплектації вкажіть склад")));
  }
  if (document.lines.size() < 2) {
    return Status::fail(Error::validation(
        QStringLiteral("рядок 1 — комплект, далі потрібен хоча б один компонент")));
  }
  const NomenclatureId kitId = document.lines.front().nomenclatureId;
  if (!kitId) {
    return Status::fail(
        Error::validation(QStringLiteral("рядок 1: оберіть номенклатуру комплекту")));
  }
  for (std::size_t i = 1; i < document.lines.size(); ++i) {
    const DocumentLine & line = document.lines[i];
    if (!line.nomenclatureId) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: оберіть компонент").arg(static_cast<int>(i) + 1)));
    }
    if (line.nomenclatureId == kitId) {
      return Status::fail(
          Error::validation(QStringLiteral("компонент не може збігатися з комплектом (рядок %1)")
                                .arg(static_cast<int>(i) + 1)));
    }
  }
  for (const DocumentLine & line : document.lines) {
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit || !credit) {
      return Status::fail(Error::validation(QStringLiteral("не вдалося визначити рахунки рядка")));
    }
    if (!isCode(debit.value(), chart::kGoods) || !isCode(credit.value(), chart::kGoods)) {
      return Status::fail(Error::validation(
          QStringLiteral("комплектація/розукомплектація можлива лише по рахунку 281")));
    }
  }
  return Status::ok();
}

Status DocumentValidator::validateOpening(const Document & document) const
{
  for (const DocumentLine & line : document.lines) {
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    const bool debitAux = isCode(debit.value(), chart::kAuxiliary);
    const bool creditAux = isCode(credit.value(), chart::kAuxiliary);
    if (debitAux == creditAux) {
      return Status::fail(Error::validation(
          QStringLiteral("введення залишків: один бік проводки має бути рахунок 00")));
    }
  }
  return Status::ok();
}

Status DocumentValidator::validateCashIn(const Document & document) const
{
  bool needsParty = false;
  for (const DocumentLine & line : document.lines) {
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    if (!isCode(debit.value(), chart::kCash) && !isCode(debit.value(), chart::kForeignCash)) {
      return Status::fail(
          Error::validation(QStringLiteral("ПКО: дебет має бути рахунок 301 або 302")));
    }
    if (needsPartyAccount(credit.value())) {
      needsParty = true;
    }
  }
  if (needsParty) {
    return requireCounterparty(document);
  }
  return Status::ok();
}

Status DocumentValidator::validateCashOut(const Document & document) const
{
  bool needsParty = false;
  for (const DocumentLine & line : document.lines) {
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    if (!isCode(credit.value(), chart::kCash) && !isCode(credit.value(), chart::kForeignCash)) {
      return Status::fail(
          Error::validation(QStringLiteral("ВКО: кредит має бути рахунок 301 або 302")));
    }
    if (needsPartyAccount(debit.value())) {
      needsParty = true;
    }
  }
  if (needsParty) {
    return requireCounterparty(document);
  }
  return Status::ok();
}

Status DocumentValidator::validatePurchase(const Document & document) const
{
  Status cp = requireCounterparty(document);
  if (!cp) {
    return cp;
  }
  for (const DocumentLine & line : document.lines) {
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    const bool isStock = isStockAccount(debit.value());
    if (isStock && (isCode(credit.value(), chart::kSuppliers) ||
                    isCode(credit.value(), chart::kForeignSuppliers))) {
      continue;
    }
    if ((isCode(debit.value(), chart::kSuppliers) ||
         isCode(debit.value(), chart::kForeignSuppliers)) &&
        (isCode(credit.value(), chart::kAdvancesIssued) ||
         isCode(credit.value(), chart::kForeignAdvancesIssued))) {
      continue;
    }
    return Status::fail(Error::validation(
        QStringLiteral("надходження: проводка має бути Дт 281/201/202/203 Кт 631/632 "
                       "(залік авансу — Дт 631/632 Кт 371/3712)")));
  }
  return Status::ok();
}

Status DocumentValidator::validateSale(const Document & document) const
{
  Status cp = requireCounterparty(document);
  if (!cp) {
    return cp;
  }
  bool hasRevenue = false;
  for (const DocumentLine & line : document.lines) {
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    const bool revenue = (isCode(debit.value(), chart::kCustomers) ||
                          isCode(debit.value(), chart::kForeignCustomers)) &&
                         isCode(credit.value(), chart::kRevenue);
    const bool cogs =
        (isCode(debit.value(), chart::kCogs) && isCode(credit.value(), chart::kGoods)) ||
        (isCode(debit.value(), chart::kProductionCogs) &&
         isCode(credit.value(), chart::kFinishedGoods));
    const bool offset = (isCode(debit.value(), chart::kAdvancesReceived) ||
                         isCode(debit.value(), chart::kForeignAdvancesReceived)) &&
                        (isCode(credit.value(), chart::kCustomers) ||
                         isCode(credit.value(), chart::kForeignCustomers));
    if (!revenue && !cogs && !offset) {
      return Status::fail(Error::validation(QStringLiteral(
          "реалізація: дозволені проводки Дт 361/362 Кт 701, Дт 902 Кт 281, Дт 901 Кт 26 "
          "та залік авансу Дт 681/6812 Кт 361/362")));
    }
    if (revenue) {
      hasRevenue = true;
    }
  }
  if (!hasRevenue) {
    return Status::fail(
        Error::validation(QStringLiteral("реалізація: потрібен рядок доходу Дт 361/362 Кт 701")));
  }
  return Status::ok();
}

Status DocumentValidator::validateBank(const Document & document) const
{
  bool incoming = false;
  bool outgoing = false;
  bool needsParty = false;
  for (const DocumentLine & line : document.lines) {
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    const bool debitBank = isCode(debit.value(), chart::kBank) ||
                           isCode(debit.value(), chart::kForeignBank) ||
                           isCode(debit.value(), chart::kForeignTransit);
    const bool creditBank = isCode(credit.value(), chart::kBank) ||
                            isCode(credit.value(), chart::kForeignBank) ||
                            isCode(credit.value(), chart::kForeignTransit);
    if (!debitBank && !creditBank) {
      return Status::fail(Error::validation(QStringLiteral(
          "банк: хоча б один бік проводки має бути банківський рахунок (311, 312, 334)")));
    }
    if (debitBank && creditBank && debit.value().id == credit.value().id) {
      return Status::fail(Error::validation(
          QStringLiteral("банк: дебет і кредит не можуть бути одним і тим самим рахунком")));
    }
    const Account & correspondence = debitBank ? credit.value() : debit.value();
    if (isCode(correspondence, chart::kAuxiliary)) {
      return Status::fail(
          Error::validation(QStringLiteral("банк: кореспонденція не може бути рахунок 00")));
    }
    if (!(debitBank && creditBank)) {
      incoming = incoming || debitBank;
      outgoing = outgoing || creditBank;
    }
    if (needsPartyAccount(correspondence)) {
      needsParty = true;
    }
  }
  if (incoming && outgoing) {
    return Status::fail(Error::validation(
        QStringLiteral("банк: надходження і списання не можна змішувати в одному документі")));
  }
  if (needsParty) {
    return requireCounterparty(document);
  }
  return Status::ok();
}

Status DocumentValidator::validateSaleReturn(const Document & document) const
{
  Status cp = requireCounterparty(document);
  if (!cp) {
    return cp;
  }
  bool hasRevenue = false;
  for (const DocumentLine & line : document.lines) {
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    const bool revenue = isCode(debit.value(), chart::kRevenue) &&
                         (isCode(credit.value(), chart::kCustomers) ||
                          isCode(credit.value(), chart::kForeignCustomers));
    const bool stock =
        (isCode(debit.value(), chart::kGoods) && isCode(credit.value(), chart::kCogs)) ||
        (isCode(debit.value(), chart::kFinishedGoods) &&
         isCode(credit.value(), chart::kProductionCogs));
    if (!revenue && !stock) {
      return Status::fail(Error::validation(
          QStringLiteral("повернення від покупця: Дт 701 Кт 361/362 та Дт 281/26 Кт 902/901")));
    }
    if (revenue) {
      hasRevenue = true;
    }
  }
  if (!hasRevenue) {
    return Status::fail(Error::validation(
        QStringLiteral("повернення від покупця: потрібен рядок Дт 701 Кт 361/362")));
  }
  return Status::ok();
}

Status DocumentValidator::validatePurchaseReturn(const Document & document) const
{
  Status cp = requireCounterparty(document);
  if (!cp) {
    return cp;
  }
  for (const DocumentLine & line : document.lines) {
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    if ((!isCode(debit.value(), chart::kSuppliers) &&
         !isCode(debit.value(), chart::kForeignSuppliers)) ||
        !isStockAccount(credit.value())) {
      return Status::fail(Error::validation(QStringLiteral(
          "повернення постачальнику: проводка має бути Дт 631/632 Кт 281/201/202/203")));
    }
  }
  return Status::ok();
}

Status DocumentValidator::validateWriteOff(const Document & document) const
{
  for (const DocumentLine & line : document.lines) {
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    if (!isCode(debit.value(), chart::kAdminExpense) || !isStockAccount(credit.value())) {
      return Status::fail(Error::validation(
          QStringLiteral("списання: проводка має бути Дт 92 Кт 281/201/202/203/26")));
    }
  }
  return Status::ok();
}

Status DocumentValidator::validateStocktake(const Document & document) const
{
  for (const DocumentLine & line : document.lines) {
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    const bool surplus =
        isStockAccount(debit.value()) && isCode(credit.value(), chart::kOtherIncome);
    const bool shortage =
        isCode(debit.value(), chart::kAdminExpense) && isStockAccount(credit.value());
    if (!surplus && !shortage) {
      return Status::fail(Error::validation(QStringLiteral(
          "інвентаризація: Дт 281/201/26 Кт 71 (надлишок) або Дт 92 Кт 281/201/26 (нестача)")));
    }
  }
  return Status::ok();
}

Status DocumentValidator::validatePeriodResult(const Document & document) const
{
  for (const DocumentLine & line : document.lines) {
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    const bool touchesResult = isCode(debit.value(), chart::kFinancialResult) ||
                               isCode(credit.value(), chart::kFinancialResult);
    const bool isRevaluation = (isCode(debit.value(), chart::kExchangeGain) ||
                                isCode(credit.value(), chart::kExchangeGain) ||
                                isCode(debit.value(), chart::kExchangeLoss) ||
                                isCode(credit.value(), chart::kExchangeLoss)) &&
                               (isCode(debit.value(), chart::kForeignBank) ||
                                isCode(credit.value(), chart::kForeignBank) ||
                                isCode(debit.value(), chart::kForeignCash) ||
                                isCode(credit.value(), chart::kForeignCash) ||
                                isCode(debit.value(), chart::kForeignTransit) ||
                                isCode(credit.value(), chart::kForeignTransit) ||
                                isCode(debit.value(), chart::kForeignCustomers) ||
                                isCode(credit.value(), chart::kForeignCustomers) ||
                                isCode(debit.value(), chart::kForeignSuppliers) ||
                                isCode(credit.value(), chart::kForeignSuppliers));
    const bool isOverheadAllocation = (isCode(debit.value(), chart::kProduction) ||
                                       isCode(debit.value(), chart::kProductionCogs)) &&
                                      isCode(credit.value(), chart::kManufacturingOverhead);
    if (!touchesResult && !isRevaluation && !isOverheadAllocation) {
      return Status::fail(Error::validation(
          QStringLiteral("фінрезультат: кожна проводка має зачіпати рахунок 79, бути курсовою "
                         "переоцінкою (714/944) або розподілом ЗВВ (Дт 231/901 Кт 91)")));
    }
    const auto allowed = [&](const Account & account) {
      return isCode(account, chart::kRevenue) || isCode(account, chart::kServiceRevenue) ||
             isCode(account, chart::kOtherIncome) || isCode(account, chart::kExchangeGain) ||
             isCode(account, chart::kExchangeLoss) || isCode(account, chart::kForeignBank) ||
             isCode(account, chart::kForeignCash) || isCode(account, chart::kForeignTransit) ||
             isCode(account, chart::kForeignCustomers) ||
             isCode(account, chart::kForeignSuppliers) || isCode(account, chart::kCogs) ||
             isCode(account, chart::kProductionCogs) ||
             isCode(account, chart::kManufacturingOverhead) ||
             isCode(account, chart::kProduction) || isCode(account, chart::kAdminExpense) ||
             isCode(account, chart::kFinancialResult) || isCode(account, chart::kRetainedEarnings);
    };
    if (!allowed(debit.value()) || !allowed(credit.value())) {
      return Status::fail(
          Error::validation(QStringLiteral("фінрезультат: дозволені лише 701, 703, 71, 714, 901, "
                                           "902, 91, 231, 92, 944, 79, 44 та переоцінка валют "
                                           "(312/302/334/362/632)")));
    }
  }
  return Status::ok();
}

Status DocumentValidator::validateServiceSale(const Document & document) const
{
  Status cp = requireCounterparty(document);
  if (!cp) {
    return cp;
  }
  for (const DocumentLine & line : document.lines) {
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    if ((isCode(debit.value(), chart::kCustomers) ||
         isCode(debit.value(), chart::kForeignCustomers)) &&
        isCode(credit.value(), chart::kServiceRevenue)) {
      continue;
    }
    if ((isCode(debit.value(), chart::kAdvancesReceived) ||
         isCode(debit.value(), chart::kForeignAdvancesReceived)) &&
        (isCode(credit.value(), chart::kCustomers) ||
         isCode(credit.value(), chart::kForeignCustomers))) {
      continue;
    }
    return Status::fail(
        Error::validation(QStringLiteral("реалізація послуг: проводка має бути Дт 361/362 Кт 703 "
                                         "(залік авансу — Дт 681/6812 Кт 361/362)")));
  }
  return Status::ok();
}

Status DocumentValidator::validateServicePurchase(const Document & document) const
{
  Status cp = requireCounterparty(document);
  if (!cp) {
    return cp;
  }
  for (const DocumentLine & line : document.lines) {
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    if ((isCode(debit.value(), chart::kSuppliers) ||
         isCode(debit.value(), chart::kForeignSuppliers)) &&
        (isCode(credit.value(), chart::kAdvancesIssued) ||
         isCode(credit.value(), chart::kForeignAdvancesIssued))) {
      continue;
    }
    if ((isCode(credit.value(), chart::kSuppliers) ||
         isCode(credit.value(), chart::kForeignSuppliers)) &&
        isServicePurchaseDebitCode(debit.value().code)) {
      continue;
    }
    return Status::fail(
        Error::validation(QStringLiteral("надходження послуг: проводка має бути Дт 92, 152 або 154 "
                                         "Кт 631/632 (залік авансу — Дт 631/632 Кт 371/3712)")));
  }
  return Status::ok();
}

Status DocumentValidator::validateCustomerInvoice(const Document & document) const
{
  Status cp = requireCounterparty(document);
  if (!cp) {
    return cp;
  }
  for (const DocumentLine & line : document.lines) {
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    if (!isCode(debit.value(), chart::kCustomers) || !isCode(credit.value(), chart::kRevenue)) {
      return Status::fail(
          Error::validation(QStringLiteral("рахунок: проводка має бути Дт 361 Кт 701")));
    }
  }
  return Status::ok();
}

Status DocumentValidator::validateFaCommission(const Document & document) const
{
  FixedAssetRepository assets(db_);
  AccountId correspondence;
  bool seenCost = false;
  bool needsSupplier = false;
  QSet<qlonglong> seen;
  QHash<qlonglong, Money> opening;
  QHash<qlonglong, Money> cost;
  int lineNo = 0;
  for (const DocumentLine & line : document.lines) {
    ++lineNo;
    if (!line.fixedAssetId) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: потрібна картка основного засобу").arg(lineNo)));
    }
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    if (isFaDepreciationAccountCode(credit.value().code)) {
      if (!isCode(debit.value(), chart::kAuxiliary)) {
        return Status::fail(Error::validation(
            QStringLiteral("рядок %1: знос на початок — Дт 00 Кт 131").arg(lineNo)));
      }
      if (opening.contains(line.fixedAssetId.value)) {
        return Status::fail(
            Error::validation(QStringLiteral("рядок %1: повторний знос на початок").arg(lineNo)));
      }
      opening.insert(line.fixedAssetId.value, line.amount);
      continue;
    }
    if (!isFixedAssetAccountCode(debit.value().code)) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: дебет має бути 104, 105 або 109").arg(lineNo)));
    }
    if (!isFaCorrespondenceCode(credit.value().code)) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: кредит введення — 152, 631 або 00").arg(lineNo)));
    }
    if (!seenCost) {
      correspondence = line.creditAccountId;
      seenCost = true;
    } else if (correspondence != line.creditAccountId) {
      return Status::fail(
          Error::validation(QStringLiteral("усі рядки введення мають мати одну кореспонденцію")));
    }
    if (isCode(credit.value(), chart::kSuppliers)) {
      needsSupplier = true;
    }
    if (seen.contains(line.fixedAssetId.value)) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: основний засіб повторено").arg(lineNo)));
    }
    seen.insert(line.fixedAssetId.value);
    cost.insert(line.fixedAssetId.value, line.amount);
    const Result<FixedAsset> asset = assets.findById(line.fixedAssetId);
    if (!asset) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: картку ОЗ не знайдено").arg(lineNo)));
    }
    if (asset.value().status != FixedAssetStatus::Draft) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: %2 уже в експлуатації")
                                .arg(QString::number(lineNo), asset.value().inventoryNo)));
    }
    if (asset.value().assetAccountId != line.debitAccountId) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: рахунок обліку не збігається з карткою").arg(lineNo)));
    }
    if (asset.value().original != line.amount) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: сума має дорівнювати первісній вартості картки").arg(lineNo)));
    }
  }
  if (!seenCost) {
    return Status::fail(Error::validation(QStringLiteral("немає рядків введення в експлуатацію")));
  }
  for (auto it = opening.cbegin(); it != opening.cend(); ++it) {
    if (!cost.contains(it.key())) {
      return Status::fail(
          Error::validation(QStringLiteral("знос на початок без рядка введення картки")));
    }
    const Result<FixedAsset> asset = assets.findById(FixedAssetId{it.key()});
    if (!asset) {
      return asset.asStatus();
    }
    if (asset.value().openingAccumulated != it.value()) {
      return Status::fail(
          Error::validation(QStringLiteral("%1: знос на початок не збігається з карткою")
                                .arg(asset.value().inventoryNo)));
    }
  }
  for (auto it = cost.cbegin(); it != cost.cend(); ++it) {
    const Result<FixedAsset> asset = assets.findById(FixedAssetId{it.key()});
    if (!asset) {
      return asset.asStatus();
    }
    if (asset.value().openingAccumulated.isPositive() && !opening.contains(it.key())) {
      return Status::fail(Error::validation(
          QStringLiteral("%1: на картці є знос на початок — потрібен рядок Дт 00 Кт 131")
              .arg(asset.value().inventoryNo)));
    }
  }
  if (needsSupplier) {
    return requireCounterparty(document);
  }
  return Status::ok();
}

Status DocumentValidator::validateFaDepreciation(const Document & document) const
{
  if (!document.date.isValid() || document.date.day() != document.date.daysInMonth()) {
    return Status::fail(
        Error::validation(QStringLiteral("амортизацію проводять останнім днем місяця")));
  }
  if (document.lines.empty()) {
    return Status::fail(Error::validation(QStringLiteral("немає рядків амортизації")));
  }
  const Result<std::vector<DocumentHeader>> existing =
      DocumentRepository(db_).list(QDate(document.date.year(), document.date.month(), 1),
                                   document.date, DocumentType::FaDepreciation);
  if (!existing) {
    return existing.asStatus();
  }
  for (const DocumentHeader & header : existing.value()) {
    if (header.posted && header.id != document.id) {
      return Status::fail(Error::conflict(
          QStringLiteral("амортизацію за %1 уже проведено (%2)")
              .arg(document.date.toString(QStringLiteral("MM.yyyy")), header.number)));
    }
  }
  FixedAssetRepository assets(db_);
  int lineNo = 0;
  for (const DocumentLine & line : document.lines) {
    ++lineNo;
    if (!line.fixedAssetId) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: потрібна картка основного засобу").arg(lineNo)));
    }
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    if (!isFaDepreciationAccountCode(credit.value().code)) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: кредит амортизації — 131").arg(lineNo)));
    }
    const Result<FixedAsset> asset = assets.findById(line.fixedAssetId);
    if (!asset) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: картку ОЗ не знайдено").arg(lineNo)));
    }
    if (asset.value().status != FixedAssetStatus::InUse) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: %2 не введено в експлуатацію")
                                .arg(QString::number(lineNo), asset.value().inventoryNo)));
    }
    if (asset.value().expenseAccountId != line.debitAccountId) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: рахунок витрат не збігається з карткою").arg(lineNo)));
    }
    const Result<Money> charge =
        depreciationCharge(asset.value(), document.date.year(), document.date.month());
    if (!charge) {
      return charge.asStatus();
    }
    if (charge.value() != line.amount) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: сума амортизації %2 має бути %3")
              .arg(QString::number(lineNo), line.amount.toDisplay(), charge.value().toDisplay())));
    }
  }
  return Status::ok();
}

Status DocumentValidator::validateFaWriteOff(const Document & document) const
{
  if (document.lines.empty()) {
    return Status::fail(Error::validation(QStringLiteral("немає рядків списання ОЗ")));
  }
  FixedAssetRepository assets(db_);
  const Result<std::vector<Account>> chart = AccountRepository(db_).listPostable();
  if (!chart) {
    return chart.asStatus();
  }
  QSet<qlonglong> seen;
  std::vector<FixedAsset> rows;
  int lineNo = 0;
  for (const DocumentLine & line : document.lines) {
    ++lineNo;
    if (!line.fixedAssetId) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: потрібна картка основного засобу").arg(lineNo)));
    }
    if (seen.contains(line.fixedAssetId.value)) {
      continue;
    }
    seen.insert(line.fixedAssetId.value);
    const Result<FixedAsset> asset = assets.findById(line.fixedAssetId);
    if (!asset) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: картку ОЗ не знайдено").arg(lineNo)));
    }
    if (asset.value().status != FixedAssetStatus::InUse) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: %2 не в експлуатації")
                                .arg(QString::number(lineNo), asset.value().inventoryNo)));
    }
    const Result<QDate> lastAmort = assets.lastPostedDepreciationDate(line.fixedAssetId);
    if (!lastAmort) {
      return lastAmort.asStatus();
    }
    if (lastAmort.value().isValid() && document.date < lastAmort.value()) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: дата списання раніше останньої амортизації (%2)")
              .arg(QString::number(lineNo),
                   lastAmort.value().toString(QStringLiteral("dd.MM.yyyy")))));
    }
    rows.push_back(asset.value());
  }
  const Result<std::vector<DocumentLine>> expected =
      composeFaWriteOff(db_, chart.value(), rows, document.date);
  if (!expected) {
    return expected.asStatus();
  }
  if (expected.value().size() != document.lines.size()) {
    return Status::fail(
        Error::validation(QStringLiteral("рядки списання ОЗ не збігаються з розрахунком рушія")));
  }
  for (std::size_t i = 0; i < expected.value().size(); ++i) {
    const DocumentLine & want = expected.value()[i];
    const DocumentLine & got = document.lines[i];
    if (want.fixedAssetId != got.fixedAssetId || want.debitAccountId != got.debitAccountId ||
        want.creditAccountId != got.creditAccountId || want.amount != got.amount) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: сума або рахунки списання ОЗ мають бути з картки")
              .arg(static_cast<int>(i) + 1)));
    }
  }
  return Status::ok();
}

Status DocumentValidator::validateIaCommission(const Document & document) const
{
  IntangibleAssetRepository assets(db_);
  AccountId correspondence;
  bool seenCost = false;
  bool needsSupplier = false;
  QSet<qlonglong> seen;
  QHash<qlonglong, Money> opening;
  QHash<qlonglong, Money> cost;
  int lineNo = 0;
  for (const DocumentLine & line : document.lines) {
    ++lineNo;
    if (!line.intangibleId) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: потрібна картка нематеріального активу").arg(lineNo)));
    }
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    if (isIaAmortizationAccountCode(credit.value().code)) {
      if (!isCode(debit.value(), chart::kAuxiliary)) {
        return Status::fail(Error::validation(
            QStringLiteral("рядок %1: знос на початок — Дт 00 Кт 133").arg(lineNo)));
      }
      if (opening.contains(line.intangibleId.value)) {
        return Status::fail(
            Error::validation(QStringLiteral("рядок %1: повторний знос на початок").arg(lineNo)));
      }
      opening.insert(line.intangibleId.value, line.amount);
      continue;
    }
    if (!isIntangibleAccountCode(debit.value().code)) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: дебет має бути 125 або 127").arg(lineNo)));
    }
    if (!isIaCorrespondenceCode(credit.value().code)) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: кредит введення — 154, 631 або 00").arg(lineNo)));
    }
    if (!seenCost) {
      correspondence = line.creditAccountId;
      seenCost = true;
    } else if (correspondence != line.creditAccountId) {
      return Status::fail(
          Error::validation(QStringLiteral("усі рядки введення мають мати одну кореспонденцію")));
    }
    if (isCode(credit.value(), chart::kSuppliers)) {
      needsSupplier = true;
    }
    if (seen.contains(line.intangibleId.value)) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: нематеріальний актив повторено").arg(lineNo)));
    }
    seen.insert(line.intangibleId.value);
    cost.insert(line.intangibleId.value, line.amount);
    const Result<IntangibleAsset> asset = assets.findById(line.intangibleId);
    if (!asset) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: картку НМА не знайдено").arg(lineNo)));
    }
    if (asset.value().status != IntangibleStatus::Draft) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: %2 уже в експлуатації")
                                .arg(QString::number(lineNo), asset.value().inventoryNo)));
    }
    if (asset.value().assetAccountId != line.debitAccountId) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: рахунок обліку не збігається з карткою").arg(lineNo)));
    }
    if (asset.value().original != line.amount) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: сума має дорівнювати первісній вартості картки").arg(lineNo)));
    }
  }
  if (!seenCost) {
    return Status::fail(Error::validation(QStringLiteral("немає рядків введення в експлуатацію")));
  }
  for (auto it = opening.cbegin(); it != opening.cend(); ++it) {
    if (!cost.contains(it.key())) {
      return Status::fail(
          Error::validation(QStringLiteral("знос на початок без рядка введення картки")));
    }
    const Result<IntangibleAsset> asset = assets.findById(IntangibleId{it.key()});
    if (!asset) {
      return asset.asStatus();
    }
    if (asset.value().openingAccumulated != it.value()) {
      return Status::fail(
          Error::validation(QStringLiteral("%1: знос на початок не збігається з карткою")
                                .arg(asset.value().inventoryNo)));
    }
  }
  for (auto it = cost.cbegin(); it != cost.cend(); ++it) {
    const Result<IntangibleAsset> asset = assets.findById(IntangibleId{it.key()});
    if (!asset) {
      return asset.asStatus();
    }
    if (asset.value().openingAccumulated.isPositive() && !opening.contains(it.key())) {
      return Status::fail(Error::validation(
          QStringLiteral("%1: на картці є знос на початок — потрібен рядок Дт 00 Кт 133")
              .arg(asset.value().inventoryNo)));
    }
  }
  if (needsSupplier) {
    return requireCounterparty(document);
  }
  return Status::ok();
}

Status DocumentValidator::validateIaDepreciation(const Document & document) const
{
  if (!document.date.isValid() || document.date.day() != document.date.daysInMonth()) {
    return Status::fail(
        Error::validation(QStringLiteral("амортизацію проводять останнім днем місяця")));
  }
  if (document.lines.empty()) {
    return Status::fail(Error::validation(QStringLiteral("немає рядків амортизації")));
  }
  const Result<std::vector<DocumentHeader>> existing =
      DocumentRepository(db_).list(QDate(document.date.year(), document.date.month(), 1),
                                   document.date, DocumentType::IaDepreciation);
  if (!existing) {
    return existing.asStatus();
  }
  for (const DocumentHeader & header : existing.value()) {
    if (header.posted && header.id != document.id) {
      return Status::fail(Error::conflict(
          QStringLiteral("амортизацію за %1 уже проведено (%2)")
              .arg(document.date.toString(QStringLiteral("MM.yyyy")), header.number)));
    }
  }
  IntangibleAssetRepository assets(db_);
  int lineNo = 0;
  for (const DocumentLine & line : document.lines) {
    ++lineNo;
    if (!line.intangibleId) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: потрібна картка нематеріального активу").arg(lineNo)));
    }
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    if (!isIaAmortizationAccountCode(credit.value().code)) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: кредит амортизації — 133").arg(lineNo)));
    }
    const Result<IntangibleAsset> asset = assets.findById(line.intangibleId);
    if (!asset) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: картку НМА не знайдено").arg(lineNo)));
    }
    if (asset.value().status != IntangibleStatus::InUse) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: %2 не введено в експлуатацію")
                                .arg(QString::number(lineNo), asset.value().inventoryNo)));
    }
    if (asset.value().expenseAccountId != line.debitAccountId) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: рахунок витрат не збігається з карткою").arg(lineNo)));
    }
    const Result<Money> charge =
        iaAmortizationCharge(asset.value(), document.date.year(), document.date.month());
    if (!charge) {
      return charge.asStatus();
    }
    if (charge.value() != line.amount) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: сума амортизації %2 має бути %3")
              .arg(QString::number(lineNo), line.amount.toDisplay(), charge.value().toDisplay())));
    }
  }
  return Status::ok();
}

Status DocumentValidator::validateIaWriteOff(const Document & document) const
{
  if (document.lines.empty()) {
    return Status::fail(Error::validation(QStringLiteral("немає рядків списання НМА")));
  }
  IntangibleAssetRepository assets(db_);
  const Result<std::vector<Account>> chart = AccountRepository(db_).listPostable();
  if (!chart) {
    return chart.asStatus();
  }
  QSet<qlonglong> seen;
  std::vector<IntangibleAsset> rows;
  int lineNo = 0;
  for (const DocumentLine & line : document.lines) {
    ++lineNo;
    if (!line.intangibleId) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: потрібна картка нематеріального активу").arg(lineNo)));
    }
    if (seen.contains(line.intangibleId.value)) {
      continue;
    }
    seen.insert(line.intangibleId.value);
    const Result<IntangibleAsset> asset = assets.findById(line.intangibleId);
    if (!asset) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: картку НМА не знайдено").arg(lineNo)));
    }
    if (asset.value().status != IntangibleStatus::InUse) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: %2 не в експлуатації")
                                .arg(QString::number(lineNo), asset.value().inventoryNo)));
    }
    const Result<QDate> lastAmort = assets.lastPostedDepreciationDate(line.intangibleId);
    if (!lastAmort) {
      return lastAmort.asStatus();
    }
    if (lastAmort.value().isValid() && document.date < lastAmort.value()) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: дата списання раніше останньої амортизації (%2)")
              .arg(QString::number(lineNo),
                   lastAmort.value().toString(QStringLiteral("dd.MM.yyyy")))));
    }
    rows.push_back(asset.value());
  }
  const Result<std::vector<DocumentLine>> expected =
      composeIaWriteOff(db_, chart.value(), rows, document.date);
  if (!expected) {
    return expected.asStatus();
  }
  if (expected.value().size() != document.lines.size()) {
    return Status::fail(
        Error::validation(QStringLiteral("рядки списання НМА не збігаються з розрахунком рушія")));
  }
  for (std::size_t i = 0; i < expected.value().size(); ++i) {
    const DocumentLine & want = expected.value()[i];
    const DocumentLine & got = document.lines[i];
    if (want.intangibleId != got.intangibleId || want.debitAccountId != got.debitAccountId ||
        want.creditAccountId != got.creditAccountId || want.amount != got.amount) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: сума або рахунки списання НМА мають бути з картки")
              .arg(static_cast<int>(i) + 1)));
    }
  }
  return Status::ok();
}

Status DocumentValidator::validateMsReceipt(const Document & document) const
{
  Status party = requireCounterparty(document);
  if (!party) {
    return party;
  }
  LowValueRepository items(db_);
  int lineNo = 0;
  for (const DocumentLine & line : document.lines) {
    ++lineNo;
    if (!line.lowValueId) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: потрібна картка МШП").arg(lineNo)));
    }
    if (!line.quantity.isPositive()) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: кількість МШП має бути більшою за нуль").arg(lineNo)));
    }
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    if (!isLowValueAccountCode(debit.value().code) || !isCode(credit.value(), chart::kSuppliers)) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: надходження МШП — Дт 22 Кт 631").arg(lineNo)));
    }
    const Result<LowValueItem> item = items.findById(line.lowValueId);
    if (!item) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: картку МШП не знайдено").arg(lineNo)));
    }
  }
  return Status::ok();
}

Status DocumentValidator::validateMsIssue(const Document & document) const
{
  LowValueRepository items(db_);
  int lineNo = 0;
  for (const DocumentLine & line : document.lines) {
    ++lineNo;
    if (!line.lowValueId) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: потрібна картка МШП").arg(lineNo)));
    }
    if (!line.quantity.isPositive()) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: кількість МШП має бути більшою за нуль").arg(lineNo)));
    }
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    if (!isCode(debit.value(), chart::kAdminExpense) ||
        !isLowValueAccountCode(credit.value().code)) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: передача МШП — Дт 92 Кт 22").arg(lineNo)));
    }
    const Result<LowValueItem> item = items.findById(line.lowValueId);
    if (!item) {
      return Status::fail(
          Error::validation(QStringLiteral("рядок %1: картку МШП не знайдено").arg(lineNo)));
    }
    const Result<Money> cost = lowValueIssueCost(item.value(), line.quantity);
    if (!cost) {
      return cost.asStatus();
    }
    if (cost.value() != line.amount) {
      return Status::fail(Error::validation(
          QStringLiteral("рядок %1: сума МШП %2 має бути %3")
              .arg(QString::number(lineNo), line.amount.toDisplay(), cost.value().toDisplay())));
    }
  }
  return Status::ok();
}

Status DocumentValidator::validateTaxInvoice(const Document & document) const
{
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return company.asStatus();
  }
  if (!company.value().vatPayer) {
    return Status::fail(
        Error::validation(QStringLiteral("податкову накладну виписує лише платник ПДВ")));
  }
  const Status party = requireCounterparty(document);
  if (!party) {
    return party;
  }
  if (document.vatRegistrationDate.isValid()) {
    if (document.vatRegistrationDate < document.date) {
      return Status::fail(
          Error::validation(QStringLiteral("дата реєстрації не може бути раніше дати ПН")));
    }
  }
  const Result<std::vector<Account>> accounts = AccountRepository(db_).listPostable();
  if (!accounts) {
    return accounts.asStatus();
  }
  Money obligation = Money::zero();
  int obligations = 0;
  for (const DocumentLine & line : document.lines) {
    if (isVatRegistrationLine(accounts.value(), line)) {
      continue;
    }
    if (!isVatObligationLine(accounts.value(), line)) {
      return Status::fail(
          Error::validation(QStringLiteral("податкова накладна проводить лише 643/644, не 641")));
    }
    const Result<Money> next = obligation.plus(line.amount);
    if (!next) {
      return next.asStatus();
    }
    obligation = next.value();
    ++obligations;
  }
  if (obligations < 1) {
    return Status::fail(
        Error::validation(QStringLiteral("податкова накладна не містить суми ПДВ")));
  }
  if (document.edoImported) {
    return Status::ok();
  }
  if (!document.basedOnId) {
    return Status::fail(
        Error::validation(QStringLiteral("податкову накладну вводять на підставі реалізації, "
                                         "надходження або авансу")));
  }
  const Result<Document> source = DocumentRepository(db_).findById(document.basedOnId);
  if (!source) {
    return Status::fail(
        Error::validation(QStringLiteral("вихідний документ податкової накладної не знайдено")));
  }
  if (source.value().counterpartyId != document.counterpartyId) {
    return Status::fail(
        Error::validation(QStringLiteral("контрагент ПН має збігатися з вихідним документом")));
  }
  if (!source.value().posted) {
    return Status::fail(
        Error::validation(QStringLiteral("податкову накладну виписують з проведеного документа")));
  }
  const Result<Document> expected = composeTaxInvoice(db_, accounts.value(), source.value(),
                                                      company.value(), document.date, document.id);
  if (!expected) {
    return expected.asStatus();
  }
  Money expectedAmount = Money::zero();
  for (const DocumentLine & line : expected.value().lines) {
    const Result<Money> next = expectedAmount.plus(line.amount);
    if (!next) {
      return next.asStatus();
    }
    expectedAmount = next.value();
  }
  if (obligation != expectedAmount) {
    return Status::fail(Error::validation(
        QStringLiteral("сума ПДВ не збігається з першою подією; переформуйте ПН")));
  }
  if (document.vatOffsetCents != expected.value().vatOffsetCents) {
    return Status::fail(
        Error::validation(QStringLiteral("залік ПДВ першої події не збігається; переформуйте ПН")));
  }
  return Status::ok();
}

Status DocumentValidator::validatePayroll(const Document & document) const
{
  if (document.counterpartyId) {
    return Status::fail(
        Error::validation(QStringLiteral("нарахування зарплати не має контрагента")));
  }
  const Result<std::vector<Account>> accounts = AccountRepository(db_).listPostable();
  if (!accounts) {
    return accounts.asStatus();
  }
  if (document.type == DocumentType::Payroll) {
    sql::Query duplicate(db_);
    Status status = duplicate.prepare(
        QStringLiteral("SELECT id FROM documents WHERE type = 'payroll' AND posted = 1 "
                       "AND strftime('%Y-%m', date) = strftime('%Y-%m', :d) AND id != :ex "
                       "LIMIT 1"));
    if (!status) {
      return status;
    }
    duplicate.bind(QStringLiteral(":d"), document.date.toString(Qt::ISODate));
    duplicate.bind(QStringLiteral(":ex"), static_cast<qlonglong>(document.id.value));
    status = duplicate.exec();
    if (!status) {
      return status;
    }
    if (duplicate.next()) {
      return Status::fail(
          Error::conflict(QStringLiteral("за цей місяць уже є проведене нарахування зарплати")));
    }
  }
  const std::vector<PayrollLine> rows = extractPayrollLines(document.lines, accounts.value());
  if (rows.empty()) {
    return Status::fail(Error::validation(QStringLiteral("нарахування не містить працівників")));
  }
  EmployeeRepository staff(db_);
  QSet<qlonglong> seen;
  const QDate monthEnd(document.date.year(), document.date.month(), document.date.daysInMonth());
  for (const PayrollLine & row : rows) {
    if (!row.employeeId) {
      return Status::fail(Error::validation(QStringLiteral("рядок зарплати без працівника")));
    }
    if (seen.contains(row.employeeId.value)) {
      return Status::fail(
          Error::validation(QStringLiteral("працівник не може повторюватись у нарахуванні")));
    }
    seen.insert(row.employeeId.value);
    const Result<Employee> employee = staff.findById(row.employeeId);
    if (!employee) {
      return Status::fail(Error::validation(QStringLiteral("працівника в довіднику не знайдено")));
    }
    if (document.type == DocumentType::Payroll) {
      const Result<PayrollLine> expected = payrollLineFor(employee.value(), monthEnd);
      if (!expected) {
        return expected.asStatus();
      }
      if (row.gross != expected.value().gross || row.pit != expected.value().pit ||
          row.military != expected.value().military || row.ssc != expected.value().ssc) {
        return Status::fail(
            Error::validation(QStringLiteral("%1: суми не збігаються з окладом і ставками 2026")
                                  .arg(employee.value().name)));
      }
    } else if (!row.gross.isPositive()) {
      return Status::fail(
          Error::validation(QStringLiteral("%1: сума нарахування має бути більшою за нуль")
                                .arg(employee.value().name)));
    }
  }
  if (document.type == DocumentType::Payroll) {
    if (static_cast<int>(document.lines.size()) != static_cast<int>(rows.size()) * 4) {
      return Status::fail(
          Error::validation(QStringLiteral("нарахування має чотири проводки на кожного працівника "
                                           "(92/661, 92/651, 661/642, 661/645)")));
    }
  } else {
    for (const DocumentLine & line : document.lines) {
      if (!line.employeeId) {
        return Status::fail(Error::validation(
            QStringLiteral("усі рядки відпускних/лікарняних мають мати працівника")));
      }
    }
    if (static_cast<int>(document.lines.size()) < static_cast<int>(rows.size())) {
      return Status::fail(Error::validation(
          QStringLiteral("нарахування має містити хоча б одну проводку на працівника")));
    }
  }
  return Status::ok();
}

Status DocumentValidator::validateMaterialIssue(const Document & document) const
{
  for (const DocumentLine & line : document.lines) {
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    const bool validDebit = isCode(debit.value(), chart::kProduction) ||
                            isCode(debit.value(), chart::kManufacturingOverhead);
    const bool validCredit = isCode(credit.value(), chart::kRawMaterials) ||
                             isCode(credit.value(), chart::kPurchasedComponents) ||
                             isCode(credit.value(), chart::kFuel) ||
                             isCode(credit.value(), chart::kGoods) ||
                             isCode(credit.value(), chart::kLowValue);
    if (!validDebit || !validCredit) {
      return Status::fail(Error::validation(
          QStringLiteral("вимога-накладна: дозволені проводки Дт 231/91 Кт 201/202/203/281/22")));
    }
    if (isCode(credit.value(), chart::kLowValue)) {
      if (!line.lowValueId) {
        return Status::fail(Error::validation(
            QStringLiteral("вимога-накладна: для списання з рахунку 22 потрібна картка МШП")));
      }
    } else {
      if (!line.nomenclatureId) {
        return Status::fail(Error::validation(
            QStringLiteral("вимога-накладна: для списання матеріалів потрібна номенклатура")));
      }
    }
    if (!line.quantity.isPositive()) {
      return Status::fail(
          Error::validation(QStringLiteral("вимога-накладна: кількість має бути більшою за нуль")));
    }
  }
  return Status::ok();
}

Status DocumentValidator::validateProductionReport(const Document & document) const
{
  for (const DocumentLine & line : document.lines) {
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    if (!isCode(debit.value(), chart::kFinishedGoods) ||
        !isCode(credit.value(), chart::kProduction)) {
      return Status::fail(
          Error::validation(QStringLiteral("випуск продукції: проводка має бути Дт 26 Кт 231")));
    }
    if (!line.nomenclatureId) {
      return Status::fail(Error::validation(
          QStringLiteral("випуск продукції: потрібна номенклатура готової продукції")));
    }
    if (!line.quantity.isPositive()) {
      return Status::fail(Error::validation(
          QStringLiteral("випуск продукції: кількість має бути більшою за нуль")));
    }
  }
  return Status::ok();
}

Status DocumentValidator::validateExpenseReport(const Document & document) const
{
  if (!document.employeeId) {
    return Status::fail(Error::validation(
        QStringLiteral("авансовий звіт: необхідно вказати підзвітну особу (працівника)")));
  }
  EmployeeRepository employees(db_);
  Result<Employee> emp = employees.findById(document.employeeId);
  if (!emp) {
    return Status::fail(
        Error::validation(QStringLiteral("авансовий звіт: підзвітну особу не знайдено")));
  }
  if (!emp.value().isActive) {
    return Status::fail(
        Error::validation(QStringLiteral("авансовий звіт: обраний працівник неактивний")));
  }
  if (document.lines.empty()) {
    return Status::fail(Error::validation(
        QStringLiteral("авансовий звіт повинен містити хоча б один рядок витрат")));
  }
  for (const DocumentLine & line : document.lines) {
    const Result<Account> debit = loadAccount(db_, line.debitAccountId);
    const Result<Account> credit = loadAccount(db_, line.creditAccountId);
    if (!debit) {
      return debit.asStatus();
    }
    if (!credit) {
      return credit.asStatus();
    }
    if (!isCode(credit.value(), chart::kAccountablePersons) &&
        !isCode(credit.value(), chart::kAccountablePersonsUah) &&
        !isCode(credit.value(), chart::kAccountablePersonsVal)) {
      return Status::fail(Error::validation(QStringLiteral(
          "авансовий звіт: кредит має бути рахунком підзвітних осіб (372 / 3721 / 3722)")));
    }
    if (!line.amount.isPositive()) {
      return Status::fail(
          Error::validation(QStringLiteral("авансовий звіт: сума рядка має бути додатною")));
    }
  }
  return Status::ok();
}

} // namespace fin
