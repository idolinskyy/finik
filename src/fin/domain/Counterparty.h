#pragma once

// Контрагент.

#include "fin/core/Ids.h"

#include <QString>

namespace fin {

struct Counterparty
{
  CounterpartyId id;
  QString name;   // найменування контрагента
  QString edrpou; // код ЄДРПОУ / РНОКПП
  QString iban; // розрахунковий рахунок у форматі UA... (29 символів)
  QString bankName;       // найменування обслуговуючого банку
  bool isCustomer = true; // прапорець покупця
  bool isSupplier = true; // прапорець постачальника
  QString notes;          // додаткова інформація, договір тощо
};

} // namespace fin
