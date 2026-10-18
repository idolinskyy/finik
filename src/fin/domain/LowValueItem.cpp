#include "fin/domain/LowValueItem.h"

#include "fin/core/Checked.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/DocumentBlueprint.h"

#include <QLatin1String>

namespace fin {

bool isLowValueAccountCode(const QString & code)
{
  return code.compare(QLatin1String(chart::kLowValue), Qt::CaseInsensitive) == 0;
}

bool usesMsReceiptForm(DocumentType type)
{
  return type == DocumentType::MsReceipt;
}

bool usesMsIssueForm(DocumentType type)
{
  return type == DocumentType::MsIssue;
}

bool usesMsForm(DocumentType type)
{
  return usesMsReceiptForm(type) || usesMsIssueForm(type);
}

Result<Money> lowValueIssueCost(const LowValueItem & item, Quantity quantity)
{
  if (!quantity.isPositive()) {
    return Result<Money>::fail(
        Error::validation(QStringLiteral("кількість списання МШП має бути більшою за нуль")));
  }
  if (item.quantity.milli() < quantity.milli()) {
    return Result<Money>::fail(
        Error::conflict(QStringLiteral("недостатньо залишку МШП «%1»: потрібно %2, є %3")
                            .arg(item.inventoryNo + QStringLiteral(" ") + item.name,
                                 quantity.toDisplay(), item.quantity.toDisplay())));
  }
  if (item.quantity == quantity) {
    if (!item.amount.isPositive()) {
      return Result<Money>::fail(Error::validation(
          QStringLiteral("%1: на залишку немає собівартості").arg(item.inventoryNo)));
    }
    return Result<Money>::ok(item.amount);
  }
  const Result<std::int64_t> allocated =
      mulDivRound(item.amount.cents(), quantity.milli(), item.quantity.milli());
  if (!allocated) {
    return Result<Money>::fail(allocated.error());
  }
  if (allocated.value() <= 0) {
    return Result<Money>::fail(Error::validation(
        QStringLiteral("%1: розрахована собівартість нульова").arg(item.inventoryNo)));
  }
  return Result<Money>::ok(Money::fromCents(allocated.value()));
}

Result<std::vector<DocumentLine>> composeMsReceipt(const std::vector<Account> & accounts,
                                                   const std::vector<MsLine> & rows)
{
  const AccountId stock = accountIdByCode(accounts, chart::kLowValue);
  const AccountId suppliers = accountIdByCode(accounts, chart::kSuppliers);
  if (!stock || !suppliers) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 22 або 631")));
  }
  if (rows.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б один рядок МШП")));
  }
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const MsLine & row : rows) {
    ++rowNo;
    if (!row.itemId) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: оберіть картку МШП").arg(rowNo)));
    }
    if (!row.quantity.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(Error::validation(
          QStringLiteral("рядок %1: кількість має бути більшою за нуль").arg(rowNo)));
    }
    if (!row.amount.isPositive()) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: сума має бути більшою за нуль").arg(rowNo)));
    }
    DocumentLine line;
    line.debitAccountId = stock;
    line.creditAccountId = suppliers;
    line.amount = row.amount;
    line.lowValueId = row.itemId;
    line.quantity = row.quantity;
    line.unitPrice = row.unitPrice;
    line.description = row.description;
    line.vatRateBp = row.vatRateBp;
    line.vatCents = row.vatCents;
    lines.push_back(line);
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

Result<std::vector<DocumentLine>> composeMsIssue(const std::vector<Account> & accounts,
                                                 const std::vector<LowValueItem> & items,
                                                 const std::vector<MsLine> & rows)
{
  const AccountId stock = accountIdByCode(accounts, chart::kLowValue);
  const AccountId expense = accountIdByCode(accounts, chart::kAdminExpense);
  if (!stock || !expense) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("у плані рахунків немає 22 або 92")));
  }
  if (rows.empty()) {
    return Result<std::vector<DocumentLine>>::fail(
        Error::validation(QStringLiteral("додайте хоча б один рядок МШП")));
  }
  std::vector<DocumentLine> lines;
  int rowNo = 0;
  for (const MsLine & row : rows) {
    ++rowNo;
    if (!row.itemId) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: оберіть картку МШП").arg(rowNo)));
    }
    const LowValueItem * item = nullptr;
    for (const LowValueItem & candidate : items) {
      if (candidate.id == row.itemId) {
        item = &candidate;
        break;
      }
    }
    if (item == nullptr) {
      return Result<std::vector<DocumentLine>>::fail(
          Error::validation(QStringLiteral("рядок %1: картку МШП не знайдено").arg(rowNo)));
    }
    const Result<Money> cost = lowValueIssueCost(*item, row.quantity);
    if (!cost) {
      return Result<std::vector<DocumentLine>>::fail(cost.error());
    }
    DocumentLine line;
    line.debitAccountId = expense;
    line.creditAccountId = stock;
    line.amount = cost.value();
    line.lowValueId = row.itemId;
    line.quantity = row.quantity;
    line.description = row.description.isEmpty()
                           ? item->inventoryNo + QStringLiteral(" ") + item->name
                           : row.description;
    lines.push_back(line);
  }
  return Result<std::vector<DocumentLine>>::ok(std::move(lines));
}

} // namespace fin
