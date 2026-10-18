#pragma once

// Договір з контрагентом.

#include "fin/core/Ids.h"

#include <QDate>
#include <QString>

namespace fin {

enum class ContractType
{
  WithCustomer, // З покупцем (продаж, виставлення рахунків, замовлення покупця)
  WithSupplier, // З постачальником (надходження, замовлення постачальнику)
  Other         // Інше (оренда, комісія, позика тощо)
};

inline QString contractTypeToString(ContractType type)
{
  switch (type) {
  case ContractType::WithCustomer:
    return QStringLiteral("with_customer");
  case ContractType::WithSupplier:
    return QStringLiteral("with_supplier");
  case ContractType::Other:
    return QStringLiteral("other");
  }
  return QStringLiteral("with_customer");
}

inline ContractType contractTypeFromString(const QString & text)
{
  if (text.compare(QStringLiteral("with_supplier"), Qt::CaseInsensitive) == 0) {
    return ContractType::WithSupplier;
  }
  if (text.compare(QStringLiteral("other"), Qt::CaseInsensitive) == 0) {
    return ContractType::Other;
  }
  return ContractType::WithCustomer;
}

inline QString contractTypeLabelUk(ContractType type)
{
  switch (type) {
  case ContractType::WithCustomer:
    return QStringLiteral("З покупцем");
  case ContractType::WithSupplier:
    return QStringLiteral("З постачальником");
  case ContractType::Other:
    return QStringLiteral("Інше");
  }
  return QStringLiteral("З покупцем");
}

struct Contract
{
  ContractId id;
  CounterpartyId counterpartyId; // Власник договору
  QString number;                // Номер договору (наприклад, "12-П/2026")
  QString name; // Найменування договору (наприклад, "Основний договір поставки")
  QDate date;   // Дата підписання
  ContractType contractType = ContractType::WithCustomer;
  QString currency = QStringLiteral("UAH"); // Валюта взаєморозрахунків
  bool isActive = true;                     // Ознака дії договору
};

} // namespace fin
