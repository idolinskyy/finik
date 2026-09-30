#pragma once

// МШП (рахунок 22).

#include "fin/core/Ids.h"
#include "fin/core/Money.h"
#include "fin/core/Quantity.h"
#include "fin/core/Result.h"
#include "fin/domain/Account.h"
#include "fin/domain/Document.h"

#include <vector>

#include <QString>

namespace fin {

struct LowValueItem
{
  LowValueId id;
  QString inventoryNo;                  // номенклатурний номер
  QString name;                         // найменування предмета МШП
  QString unit = QStringLiteral("шт");  // одиниця виміру
  Quantity quantity = Quantity::zero(); // залишок на складі у штуках
  Money amount = Money::zero(); // загальна вартість залишку на складі
  bool isActive = true;
};

// Рядок документа операцій з МШП
struct MsLine
{
  LowValueId itemId;
  Quantity quantity = Quantity::zero();
  Money unitPrice = Money::zero();
  Money amount = Money::zero();
  int vatRateBp = 0;
  std::int64_t vatCents = 0;
  QString description;
};

bool isLowValueAccountCode(const QString & code);
bool usesMsReceiptForm(DocumentType type);
bool usesMsIssueForm(DocumentType type);
bool usesMsForm(DocumentType type);

// Розрахунок середньозваженої вартості списання партії МШП (з вивантаженням залишку копійок на останній одиниці)
Result<Money> lowValueIssueCost(const LowValueItem & item, Quantity quantity);

// Формування проводок оприбуткування МШП (Дт 22 Кт 631)
Result<std::vector<DocumentLine>> composeMsReceipt(const std::vector<Account> & accounts,
                                                   const std::vector<MsLine> & rows);

// Формування проводок списання МШП в експлуатацію (Дт 92 Кт 22)
Result<std::vector<DocumentLine>> composeMsIssue(const std::vector<Account> & accounts,
                                                 const std::vector<LowValueItem> & items,
                                                 const std::vector<MsLine> & rows);

} // namespace fin
