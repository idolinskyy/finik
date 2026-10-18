#pragma once

// Складські рухи та залишки.

#include "fin/core/Ids.h"
#include "fin/core/Money.h"
#include "fin/core/Quantity.h"

#include <QDate>
#include <QString>

namespace fin {

struct InventoryMovement
{
  DocumentId documentId; // первинний документ-підстава
  QDate date;            // дата операції
  WarehouseId warehouseId = WarehouseId{1}; // склад обліку (за замовчуванням Основний склад = 1)
  NomenclatureId nomenclatureId;        // номенклатурна позиція товару
  Quantity quantity = Quantity::zero(); // зміна кількості (знакова)
  Money amount = Money::zero(); // зміна вартості за середньою (знакова)
  QString description;          // коментар або назва операції
};

} // namespace fin
