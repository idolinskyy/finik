#pragma once

// Специфікація виробу (BOM).

#include "fin/core/Ids.h"
#include "fin/core/Quantity.h"

#include <vector>

#include <QString>

namespace fin {

// Рядок специфікації: матеріал або комплектуючий виріб та його кількість
struct BomLine
{
  int lineNo = 0;
  NomenclatureId componentId; // сировина / матеріал (рах. 201, 202, 203, 22, 281)
  Quantity quantity = Quantity::fromMilli(1000); // нормативна кількість на випуск outputQuantity
  QString comment;
};

// Заголовок специфікації готового виробу
struct BillOfMaterials
{
  BomId id;
  NomenclatureId productId; // готова продукція (рах. 26)
  QString name;             // назва специфікації (напр., «Основна»)
  Quantity outputQuantity =
      Quantity::fromMilli(1000); // обсяг випуску за нормою (за замовчуванням 1 шт = 1000 міліод.)
  bool isDefault = true; // чи є основною специфікацією для виробу
  bool isActive = true;  // статус активності
  QString comment;
  std::vector<BomLine> lines; // перелік компонентів
};

} // namespace fin
