#include "fin/posting/InventoryCosting.h"

#include "fin/core/Checked.h"
#include "fin/core/Sql.h"
#include "fin/db/CompanyRepository.h"
#include "fin/db/NomenclatureRepository.h"
#include "fin/db/WarehouseRepository.h"

namespace fin {

InventoryCosting::InventoryCosting(QSqlDatabase db) : db_(std::move(db)) {}

Result<StockOnHand *> InventoryCosting::loaded(WarehouseId warehouseId,
                                               NomenclatureId nomenclatureId)
{
  if (!nomenclatureId) {
    return Result<StockOnHand *>::fail(
        Error::validation(QStringLiteral("для списання 281 потрібна номенклатура")));
  }
  const std::int64_t whId = warehouseId.value > 0 ? warehouseId.value : 1;
  CacheKey key{whId, nomenclatureId.value};
  const auto found = cache_.find(key);
  if (found != cache_.end()) {
    return Result<StockOnHand *>::ok(&found->second);
  }

  sql::Query query(db_);
  Status status = query.prepare(QStringLiteral(
      "SELECT COALESCE(SUM(quantity_milli), 0), COALESCE(SUM(amount_cents), 0) "
      "FROM inventory_movements WHERE warehouse_id = :wh AND nomenclature_id = :id"));
  if (!status) {
    return Result<StockOnHand *>::fail(status.error());
  }
  query.bind(QStringLiteral(":wh"), static_cast<qlonglong>(whId));
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(nomenclatureId.value));
  status = query.exec();
  if (!status) {
    return Result<StockOnHand *>::fail(status.error());
  }
  StockOnHand onHand;
  if (query.next()) {
    onHand.quantity = Quantity::fromMilli(query.value(0).toLongLong());
    onHand.amount = Money::fromCents(query.value(1).toLongLong());
  }
  auto inserted = cache_.emplace(key, onHand);
  return Result<StockOnHand *>::ok(&inserted.first->second);
}

Status InventoryCosting::receive(WarehouseId warehouseId, NomenclatureId nomenclatureId,
                                 Quantity quantity, Money amount)
{
  Result<StockOnHand *> onHand = loaded(warehouseId, nomenclatureId);
  if (!onHand) {
    return onHand.asStatus();
  }
  const Result<Quantity> qty = onHand.value()->quantity.plus(quantity);
  if (!qty) {
    return qty.asStatus();
  }
  const Result<Money> sum = onHand.value()->amount.plus(amount);
  if (!sum) {
    return sum.asStatus();
  }
  onHand.value()->quantity = qty.value();
  onHand.value()->amount = sum.value();
  return Status::ok();
}

Result<Money> InventoryCosting::takeOutgoingCost(WarehouseId warehouseId,
                                                 NomenclatureId nomenclatureId, Quantity quantity)
{
  Result<StockOnHand *> onHand = loaded(warehouseId, nomenclatureId);
  if (!onHand) {
    return Result<Money>::fail(onHand.error());
  }
  if (!quantity.isPositive()) {
    return Result<Money>::fail(
        Error::validation(QStringLiteral("кількість списання має бути більшою за нуль")));
  }
  if (onHand.value()->quantity.milli() < quantity.milli()) {
    QString name = QStringLiteral("товар");
    const Result<Nomenclature> item = NomenclatureRepository(db_).findById(nomenclatureId);
    if (item) {
      name = item.value().name;
      if (!item.value().sku.trimmed().isEmpty()) {
        name += QStringLiteral(" (%1)").arg(item.value().sku.trimmed());
      }
    }
    QString whName =
        QStringLiteral("складі #%1").arg(warehouseId.value > 0 ? warehouseId.value : 1);
    const Result<Warehouse> wh = WarehouseRepository(db_).findById(
        WarehouseId{warehouseId.value > 0 ? warehouseId.value : 1});
    if (wh) {
      whName = wh.value().name;
    }

    bool allowNegative = false;
    const Result<CompanyProfile> company = CompanyRepository(db_).load();
    if (company) {
      allowNegative = company.value().allowNegativeStock;
    }
    if (!allowNegative) {
      return Result<Money>::fail(Error::conflict(
          QStringLiteral("недостатньо залишку «%1» на складі «%2»: потрібно %3, є %4")
              .arg(name, whName, quantity.toDisplay(), onHand.value()->quantity.toDisplay())));
    }

    // Від'ємний залишок дозволено лише коли є хоча б часткова кількість
    // для оцінки собівартості. Порожній склад без суми — не вигадуємо COGS.
    if (!onHand.value()->quantity.isPositive()) {
      return Result<Money>::fail(Error::conflict(
          QStringLiteral("немає залишку «%1» на складі «%2» для оцінки собівартості "
                         "(від'ємні залишки увімкнено, але сума списання невідома)")
              .arg(name, whName)));
    }
    const Result<std::int64_t> allocated = mulDivRound(
        onHand.value()->amount.cents(), quantity.milli(), onHand.value()->quantity.milli());
    if (!allocated) {
      return Result<Money>::fail(allocated.error());
    }
    Money cost = Money::zero();
    if (allocated.value() > 0) {
      cost = Money::fromCents(allocated.value());
    }

    const Result<Quantity> negatedQty = quantity.negated();
    if (!negatedQty) {
      return Result<Money>::fail(negatedQty.error());
    }
    const Result<Quantity> remainingQty = onHand.value()->quantity.plus(negatedQty.value());
    if (!remainingQty) {
      return Result<Money>::fail(remainingQty.error());
    }
    const Result<Money> remainingAmount = onHand.value()->amount.minus(cost);
    if (!remainingAmount) {
      return Result<Money>::fail(remainingAmount.error());
    }

    onHand.value()->quantity = remainingQty.value();
    onHand.value()->amount = remainingAmount.value();
    return Result<Money>::ok(cost);
  }

  Money cost = Money::zero();
  if (onHand.value()->quantity == quantity) {
    cost = onHand.value()->amount;
    if (!cost.isPositive()) {
      return Result<Money>::fail(
          Error::validation(QStringLiteral("на залишку немає собівартості для списання")));
    }
    onHand.value()->quantity = Quantity::zero();
    onHand.value()->amount = Money::zero();
    return Result<Money>::ok(cost);
  }

  const Result<std::int64_t> allocated = mulDivRound(
      onHand.value()->amount.cents(), quantity.milli(), onHand.value()->quantity.milli());
  if (!allocated) {
    return Result<Money>::fail(allocated.error());
  }
  if (allocated.value() <= 0) {
    return Result<Money>::fail(
        Error::validation(QStringLiteral("розрахована собівартість нульова")));
  }
  if (allocated.value() > onHand.value()->amount.cents()) {
    return Result<Money>::fail(
        Error::overflow(QStringLiteral("собівартість перевищує залишок суми")));
  }
  cost = Money::fromCents(allocated.value());
  const Result<Quantity> negatedQty = quantity.negated();
  if (!negatedQty) {
    return Result<Money>::fail(negatedQty.error());
  }
  const Result<Quantity> remainingQty = onHand.value()->quantity.plus(negatedQty.value());
  if (!remainingQty) {
    return Result<Money>::fail(remainingQty.error());
  }
  const Result<Money> remainingAmount = onHand.value()->amount.minus(cost);
  if (!remainingAmount) {
    return Result<Money>::fail(remainingAmount.error());
  }
  if (remainingQty.value().isPositive() && remainingAmount.value().isNegative()) {
    return Result<Money>::fail(
        Error::conflict(QStringLiteral("списання залишає від'ємну суму на складі")));
  }
  onHand.value()->quantity = remainingQty.value();
  onHand.value()->amount = remainingAmount.value();
  return Result<Money>::ok(cost);
}

Result<Money> InventoryCosting::peekCost(WarehouseId warehouseId, NomenclatureId nomenclatureId,
                                         Quantity quantity)
{
  Result<StockOnHand *> onHand = loaded(warehouseId, nomenclatureId);
  if (!onHand) {
    return Result<Money>::fail(onHand.error());
  }
  if (!quantity.isPositive()) {
    return Result<Money>::fail(
        Error::validation(QStringLiteral("кількість оцінки має бути більшою за нуль")));
  }
  if (onHand.value()->quantity.isPositive()) {
    if (onHand.value()->quantity == quantity) {
      if (!onHand.value()->amount.isPositive()) {
        return Result<Money>::fail(
            Error::validation(QStringLiteral("на залишку немає собівартості")));
      }
      return Result<Money>::ok(onHand.value()->amount);
    }
    const Result<std::int64_t> allocated = mulDivRound(
        onHand.value()->amount.cents(), quantity.milli(), onHand.value()->quantity.milli());
    if (!allocated) {
      return Result<Money>::fail(allocated.error());
    }
    if (allocated.value() <= 0) {
      return Result<Money>::fail(
          Error::validation(QStringLiteral("розрахована собівартість нульова")));
    }
    return Result<Money>::ok(Money::fromCents(allocated.value()));
  }

  const std::int64_t whId = warehouseId.value > 0 ? warehouseId.value : 1;
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("SELECT quantity_milli, amount_cents FROM inventory_movements "
                     "WHERE warehouse_id = :wh AND nomenclature_id = :id AND quantity_milli < 0 "
                     "ORDER BY date DESC, id DESC LIMIT 1"));
  if (!status) {
    return Result<Money>::fail(status.error());
  }
  query.bind(QStringLiteral(":wh"), static_cast<qlonglong>(whId));
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(nomenclatureId.value));
  status = query.exec();
  if (!status) {
    return Result<Money>::fail(status.error());
  }
  if (!query.next()) {
    return Result<Money>::fail(
        Error::validation(QStringLiteral("немає собівартості для повернення на порожній склад")));
  }
  const std::int64_t lastQty = -query.value(0).toLongLong();
  const std::int64_t lastAmount = -query.value(1).toLongLong();
  if (lastQty <= 0 || lastAmount <= 0) {
    return Result<Money>::fail(
        Error::validation(QStringLiteral("останнє списання не містить собівартості")));
  }
  const Result<std::int64_t> allocated = mulDivRound(lastAmount, quantity.milli(), lastQty);
  if (!allocated) {
    return Result<Money>::fail(allocated.error());
  }
  if (allocated.value() <= 0) {
    return Result<Money>::fail(
        Error::validation(QStringLiteral("розрахована собівартість нульова")));
  }
  return Result<Money>::ok(Money::fromCents(allocated.value()));
}

} // namespace fin
