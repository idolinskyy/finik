#include "fin/domain/Document.h"

namespace fin {
namespace {

struct TypeMeta
{
  DocumentType type;
  const char * id;
  const char * labelUk;
  const char * prefix;
};

constexpr TypeMeta kTypes[] = {
    {DocumentType::OpeningBalance, "opening_balance", "Введення залишків", "ЗС"},
    {DocumentType::Manual, "manual", "Бухгалтерська операція", "БО"},
    {DocumentType::CashIn, "cash_in", "Прибутковий касовий ордер", "ПКО"},
    {DocumentType::CashOut, "cash_out", "Видатковий касовий ордер", "ВКО"},
    {DocumentType::Purchase, "purchase", "Надходження від постачальника", "НП"},
    {DocumentType::Sale, "sale", "Реалізація", "РТ"},
    {DocumentType::BankPayment, "bank_payment", "Банківський платіж", "РР"},
    {DocumentType::SaleReturn, "sale_return", "Повернення від покупця", "ПВ"},
    {DocumentType::PurchaseReturn, "purchase_return", "Повернення постачальнику", "ПП"},
    {DocumentType::WriteOff, "write_off", "Списання товарів", "СП"},
    {DocumentType::Stocktake, "stocktake", "Інвентаризація", "ІН"},
    {DocumentType::PeriodResult, "period_result", "Фінансовий результат", "ФР"},
    {DocumentType::ServiceSale, "service_sale", "Реалізація послуг", "РП"},
    {DocumentType::ServicePurchase, "service_purchase", "Надходження послуг", "ПУ"},
    {DocumentType::CustomerInvoice, "customer_invoice", "Рахунок на оплату", "РХ"},
    {DocumentType::FaCommission, "fa_commission", "Введення в експлуатацію", "ОЗ"},
    {DocumentType::FaDepreciation, "fa_depreciation", "Амортизація ОЗ", "АМ"},
    {DocumentType::FaWriteOff, "fa_write_off", "Списання ОЗ", "СЗ"},
    {DocumentType::MsReceipt, "ms_receipt", "Надходження МШП", "МШ"},
    {DocumentType::MsIssue, "ms_issue", "Передача МШП в експлуатацію", "МЕ"},
    {DocumentType::IaCommission, "ia_commission", "Введення НМА", "НМ"},
    {DocumentType::IaDepreciation, "ia_depreciation", "Амортизація НМА", "АН"},
    {DocumentType::IaWriteOff, "ia_write_off", "Списання НМА", "СН"},
    {DocumentType::TaxInvoice, "tax_invoice", "Податкова накладна", "ПН"},
    {DocumentType::Payroll, "payroll", "Нарахування зарплати", "ЗП"},
    {DocumentType::StockTransfer, "stock_transfer", "Переміщення товарів", "ПМ"},
    {DocumentType::KitAssembly, "kit_assembly", "Комплектація номенклатури", "КМ"},
    {DocumentType::KitDisassembly, "kit_disassembly", "Розукомплектація номенклатури", "РК"},
    {DocumentType::CustomerOrder, "customer_order", "Замовлення покупця", "ЗМ"},
    {DocumentType::SupplierOrder, "supplier_order", "Замовлення постачальнику", "ЗПС"},
    {DocumentType::MaterialIssue, "material_issue", "Вимога-накладна (списання матеріалів)", "ВН"},
    {DocumentType::ProductionReport, "production_report", "Звіт виробництва (випуск продукції)",
     "ЗВ"},
    {DocumentType::ExpenseReport, "expense_report", "Авансовий звіт", "АЗ"},
    {DocumentType::VacationPayroll, "vacation_payroll", "Нарахування відпускних", "ВД"},
    {DocumentType::SickLeavePayroll, "sick_leave_payroll", "Нарахування лікарняних", "ЛН"},
};

const TypeMeta * findMeta(DocumentType type)
{
  for (const TypeMeta & meta : kTypes) {
    if (meta.type == type) {
      return &meta;
    }
  }
  return nullptr;
}

} // namespace

std::optional<DocumentType> documentTypeFromString(const QString & text)
{
  for (const TypeMeta & meta : kTypes) {
    if (text == QLatin1String(meta.id)) {
      return meta.type;
    }
  }
  return std::nullopt;
}

QString documentTypeToString(DocumentType type)
{
  const TypeMeta * meta = findMeta(type);
  return meta ? QString::fromLatin1(meta->id) : QString();
}

QString documentTypeLabelUk(DocumentType type)
{
  const TypeMeta * meta = findMeta(type);
  return meta ? QString::fromUtf8(meta->labelUk) : QString();
}

QString documentTypePrefix(DocumentType type)
{
  const TypeMeta * meta = findMeta(type);
  return meta ? QString::fromUtf8(meta->prefix) : QString();
}

std::vector<DocumentType> allDocumentTypes()
{
  std::vector<DocumentType> out;
  out.reserve(sizeof(kTypes) / sizeof(kTypes[0]));
  for (const TypeMeta & meta : kTypes) {
    out.push_back(meta.type);
  }
  return out;
}

Money Document::total() const
{
  Money acc = Money::zero();
  for (const DocumentLine & line : lines) {
    const Result<Money> next = acc.plus(line.amount);
    if (!next) {
      return acc;
    }
    acc = next.value();
  }
  return acc;
}

} // namespace fin
