#include "fin/db/InventoryRepository.h"

#include "fin/core/Sql.h"

#include <QVariant>

namespace fin {

InventoryRepository::InventoryRepository(QSqlDatabase db) : db_(std::move(db)) {}

Status InventoryRepository::removeForDocument(DocumentId documentId)
{
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("DELETE FROM inventory_movements WHERE document_id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(documentId.value));
  return query.exec();
}

Status InventoryRepository::replaceForDocument(DocumentId documentId,
                                               const std::vector<InventoryMovement> & movements)
{
  Status status = removeForDocument(documentId);
  if (!status) {
    return status;
  }
  sql::Query insert(db_);
  status = insert.prepare(
      QStringLiteral("INSERT INTO inventory_movements(document_id, date, nomenclature_id, "
                     "quantity_milli, amount_cents, description, warehouse_id) "
                     "VALUES(:doc, :date, :nom, :qty, :amt, :desc, :wh)"));
  if (!status) {
    return status;
  }
  for (const InventoryMovement & row : movements) {
    insert.bind(QStringLiteral(":doc"), static_cast<qlonglong>(documentId.value));
    insert.bind(QStringLiteral(":date"), row.date.toString(Qt::ISODate));
    insert.bind(QStringLiteral(":nom"), static_cast<qlonglong>(row.nomenclatureId.value));
    insert.bind(QStringLiteral(":qty"), static_cast<qlonglong>(row.quantity.milli()));
    insert.bind(QStringLiteral(":amt"), static_cast<qlonglong>(row.amount.cents()));
    insert.bind(QStringLiteral(":desc"), row.description);
    insert.bind(QStringLiteral(":wh"),
                static_cast<qlonglong>(row.warehouseId.value > 0 ? row.warehouseId.value : 1));
    status = insert.exec();
    if (!status) {
      return status;
    }
  }
  return Status::ok();
}

Result<std::vector<StockBalance>>
InventoryRepository::balances(std::optional<WarehouseId> warehouseId) const
{
  sql::Query query(db_);
  QString sql = QStringLiteral("SELECT nomenclature_id, COALESCE(SUM(quantity_milli), 0), "
                               "COALESCE(SUM(amount_cents), 0) FROM inventory_movements ");
  if (warehouseId.has_value() && warehouseId->value > 0) {
    sql += QStringLiteral("WHERE warehouse_id = :wh ");
  }
  sql += QStringLiteral("GROUP BY nomenclature_id HAVING COALESCE(SUM(quantity_milli), 0) != 0 "
                        "OR COALESCE(SUM(amount_cents), 0) != 0 ORDER BY nomenclature_id");

  Status status = query.prepare(sql);
  if (!status) {
    return Result<std::vector<StockBalance>>::fail(status.error());
  }
  if (warehouseId.has_value() && warehouseId->value > 0) {
    query.bind(QStringLiteral(":wh"), static_cast<qlonglong>(warehouseId->value));
  }
  status = query.exec();
  if (!status) {
    return Result<std::vector<StockBalance>>::fail(status.error());
  }
  std::vector<StockBalance> rows;
  while (query.next()) {
    StockBalance row;
    row.nomenclatureId = NomenclatureId{query.value(0).toLongLong()};
    row.quantity = Quantity::fromMilli(query.value(1).toLongLong());
    row.amount = Money::fromCents(query.value(2).toLongLong());
    rows.push_back(row);
  }
  return Result<std::vector<StockBalance>>::ok(std::move(rows));
}

} // namespace fin
