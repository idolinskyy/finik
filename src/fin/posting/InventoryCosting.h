#pragma once

// Середньозважена собівартість запасів при проведенні.

#include "fin/core/Ids.h"
#include "fin/core/Money.h"
#include "fin/core/Quantity.h"
#include "fin/core/Result.h"

#include <unordered_map>

#include <QSqlDatabase>

namespace fin {

// Поточний залишок позиції номенклатури на складі:
// кількість у міліодиницях та балансова вартість у копійках.
struct StockOnHand
{
  Quantity quantity = Quantity::zero();
  Money amount = Money::zero();
};

class InventoryCosting
{
 public:
  explicit InventoryCosting(QSqlDatabase db);

  // Фіксує надходження: додає до залишку кількість і вартість для конкретного складу.
  Status receive(WarehouseId warehouseId, NomenclatureId nomenclatureId, Quantity quantity,
                 Money amount);
  Status receive(NomenclatureId nomenclatureId, Quantity quantity, Money amount)
  {
    return receive(WarehouseId{1}, nomenclatureId, quantity, amount);
  }

  // Вилучає зі складу warehouseId кількість quantity за середньозваженою
  // собівартістю і повертає розраховану суму списання.
  Result<Money> takeOutgoingCost(WarehouseId warehouseId, NomenclatureId nomenclatureId,
                                 Quantity quantity);
  Result<Money> takeOutgoingCost(NomenclatureId nomenclatureId, Quantity quantity)
  {
    return takeOutgoingCost(WarehouseId{1}, nomenclatureId, quantity);
  }

  // Обчислює собівартість вибуття без зміни стеку залишку.
  Result<Money> peekCost(WarehouseId warehouseId, NomenclatureId nomenclatureId, Quantity quantity);
  Result<Money> peekCost(NomenclatureId nomenclatureId, Quantity quantity)
  {
    return peekCost(WarehouseId{1}, nomenclatureId, quantity);
  }

 private:
  // Завантажує залишок з БД при першому зверненні, потім повертає з кешу.
  Result<StockOnHand *> loaded(WarehouseId warehouseId, NomenclatureId nomenclatureId);

  QSqlDatabase db_;
  // Кеш залишків за складом та номенклатурою.
  // Ключ — пара (warehouseId, nomenclatureId).
  struct CacheKey
  {
    std::int64_t warehouseId = 1;
    std::int64_t nomenclatureId = 0;
    bool operator==(const CacheKey & o) const
    {
      return warehouseId == o.warehouseId && nomenclatureId == o.nomenclatureId;
    }
  };
  struct CacheKeyHash
  {
    std::size_t operator()(const CacheKey & k) const noexcept
    {
      return std::hash<std::int64_t>{}(k.warehouseId) ^
             (std::hash<std::int64_t>{}(k.nomenclatureId) << 1);
    }
  };
  std::unordered_map<CacheKey, StockOnHand, CacheKeyHash> cache_;
};

} // namespace fin
