#pragma once

// Рахунок плану рахунків.

#include "fin/core/Ids.h"

#include <optional>

#include <QString>

namespace fin {

// Звичайна ознака залишку рахунку за стандартом бухобліку
enum class NormalSide
{
  Debit,
  Credit
};

struct Account
{
  AccountId id;
  QString code;         // номер субрахунку (наприклад, «281», «631»)
  QString name;         // назва (наприклад, «Товари на складі»)
  AccountId parentId;   // батьківська папка/група
  int accountClass = 0; // клас рахунку (1-9 або 0 для позабалансових)
  NormalSide normalSide = NormalSide::Debit; // нормальне сальдо (дебет / кредит)
  bool hasCounterparty = false; // чи вимагає рахунок обов'язкової аналітики за контрагентом
  bool isFolder = false; // true = група/папка (проведення заблоковані)
  bool isActive = true; // доступність для вибору в нових документах

  // Чи дозволено проводити операції за цим рахунком у Головну книгу
  bool postable() const { return isActive && !isFolder; }
};

inline QString normalSideToString(NormalSide side)
{
  return side == NormalSide::Debit ? QStringLiteral("debit") : QStringLiteral("credit");
}

inline std::optional<NormalSide> normalSideFromString(const QString & text)
{
  if (text == QStringLiteral("debit")) {
    return NormalSide::Debit;
  }
  if (text == QStringLiteral("credit")) {
    return NormalSide::Credit;
  }
  return std::nullopt;
}

} // namespace fin
