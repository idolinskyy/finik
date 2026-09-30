#pragma once

// Склад.

#include "fin/core/Ids.h"

#include <QString>

namespace fin {

struct Warehouse
{
  WarehouseId id;
  QString code; // Унікальний код складу (наприклад, "MAIN", "SHOP-1")
  QString name; // Найменування складу (наприклад, "Основний склад")
  QString address;      // Фактична адреса складу
  bool isActive = true; // Ознака активності (true = діючий склад)
};

} // namespace fin
