#pragma once

// Номенклатура.

#include "fin/core/Ids.h"

#include <QString>

namespace fin {

struct Nomenclature
{
  NomenclatureId id;
  QString name;                        // найменування товару/послуги
  QString sku;                         // артикул / внутрішній код
  QString unit = QStringLiteral("шт"); // базова одиниця виміру (шт, кг, л, послуга)
  bool isActive = true; // прапорець активності (не відображається у виборі при false)
};

} // namespace fin
