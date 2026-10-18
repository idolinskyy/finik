#include "fin/db/WarehouseRepository.h"

#include "fin/auth/Access.h"
#include "fin/core/Sql.h"

namespace fin {
namespace {

Status validateWarehouse(const Warehouse & warehouse)
{
  if (warehouse.code.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("код складу обов'язковий")));
  }
  if (warehouse.name.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("назва складу обов'язкова")));
  }
  return Status::ok();
}

Warehouse fromQuery(sql::Query & query)
{
  Warehouse row;
  row.id = WarehouseId{query.value(0).toLongLong()};
  row.code = query.value(1).toString();
  row.name = query.value(2).toString();
  row.address = query.value(3).toString();
  row.isActive = query.value(4).toInt() != 0;
  return row;
}

const QString kSelect = QStringLiteral("SELECT id, code, name, address, is_active FROM warehouses");

} // namespace

WarehouseRepository::WarehouseRepository(QSqlDatabase db) : db_(std::move(db)) {}

Result<Warehouse> WarehouseRepository::findById(WarehouseId id) const
{
  if (!id) {
    return Result<Warehouse>::fail(Error::invalidArgument(QStringLiteral("порожній id складу")));
  }
  sql::Query query(db_);
  Status status = query.prepare(kSelect + QStringLiteral(" WHERE id = :id"));
  if (!status) {
    return Result<Warehouse>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    return Result<Warehouse>::fail(status.error());
  }
  if (!query.next()) {
    return Result<Warehouse>::fail(Error::notFound(QStringLiteral("склад не знайдено")));
  }
  return Result<Warehouse>::ok(fromQuery(query));
}

Result<Warehouse> WarehouseRepository::findByCode(const QString & code) const
{
  const QString trimmedCode = code.trimmed();
  if (trimmedCode.isEmpty()) {
    return Result<Warehouse>::fail(Error::invalidArgument(QStringLiteral("порожній код складу")));
  }
  sql::Query query(db_);
  Status status = query.prepare(kSelect + QStringLiteral(" WHERE code = :code"));
  if (!status) {
    return Result<Warehouse>::fail(status.error());
  }
  query.bind(QStringLiteral(":code"), trimmedCode);
  status = query.exec();
  if (!status) {
    return Result<Warehouse>::fail(status.error());
  }
  if (!query.next()) {
    return Result<Warehouse>::fail(
        Error::notFound(QStringLiteral("склад з кодом %1 не знайдено").arg(trimmedCode)));
  }
  return Result<Warehouse>::ok(fromQuery(query));
}

Result<std::vector<Warehouse>> WarehouseRepository::listAll(bool activeOnly) const
{
  sql::Query query(db_);
  QString sql = kSelect;
  if (activeOnly) {
    sql += QStringLiteral(" WHERE is_active = 1");
  }
  sql += QStringLiteral(" ORDER BY id ASC");
  Status status = query.exec(sql);
  if (!status) {
    return Result<std::vector<Warehouse>>::fail(status.error());
  }
  std::vector<Warehouse> rows;
  while (query.next()) {
    rows.push_back(fromQuery(query));
  }
  return Result<std::vector<Warehouse>>::ok(std::move(rows));
}

Result<WarehouseId> WarehouseRepository::insert(const Warehouse & warehouse)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return Result<WarehouseId>::fail(access.error());
  }
  const Status valid = validateWarehouse(warehouse);
  if (!valid) {
    return Result<WarehouseId>::fail(valid.error());
  }
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("INSERT INTO warehouses(code, name, address, is_active) "
                                   "VALUES(:code, :name, :address, :active)"));
  if (!status) {
    return Result<WarehouseId>::fail(status.error());
  }
  query.bind(QStringLiteral(":code"), warehouse.code.trimmed());
  query.bind(QStringLiteral(":name"), warehouse.name.trimmed());
  query.bind(QStringLiteral(":address"), warehouse.address.trimmed());
  query.bind(QStringLiteral(":active"), warehouse.isActive ? 1 : 0);
  status = query.exec();
  if (!status) {
    return Result<WarehouseId>::fail(status.error());
  }
  return Result<WarehouseId>::ok(WarehouseId{query.lastInsertId()});
}

Status WarehouseRepository::update(const Warehouse & warehouse)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  if (!warehouse.id) {
    return Status::fail(Error::invalidArgument(QStringLiteral("порожній id складу")));
  }
  const Status valid = validateWarehouse(warehouse);
  if (!valid) {
    return valid;
  }
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("UPDATE warehouses SET code = :code, name = :name, "
                                   "address = :address, is_active = :active WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":code"), warehouse.code.trimmed());
  query.bind(QStringLiteral(":name"), warehouse.name.trimmed());
  query.bind(QStringLiteral(":address"), warehouse.address.trimmed());
  query.bind(QStringLiteral(":active"), warehouse.isActive ? 1 : 0);
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(warehouse.id.value));
  status = query.exec();
  if (!status) {
    return status;
  }
  if (query.raw().numRowsAffected() <= 0) {
    return Status::fail(Error::notFound(QStringLiteral("склад не знайдено")));
  }
  return Status::ok();
}

Status WarehouseRepository::remove(WarehouseId id)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  if (!id) {
    return Status::fail(Error::invalidArgument(QStringLiteral("порожній id складу")));
  }
  if (id.value == 1) {
    return Status::fail(
        Error::conflict(QStringLiteral("неможливо видалити основний склад (id = 1)")));
  }
  sql::Query query(db_);
  Status status = query.prepare(QStringLiteral("DELETE FROM warehouses WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    const QString text = status.message();
    if (text.contains(QLatin1String("FOREIGN KEY"), Qt::CaseInsensitive)) {
      return Status::fail(Error::conflict(
          QStringLiteral("склад не можна видалити: є пов'язані документи або рухи")));
    }
    return status;
  }
  if (query.raw().numRowsAffected() <= 0) {
    return Status::fail(Error::notFound(QStringLiteral("склад не знайдено")));
  }
  return Status::ok();
}

} // namespace fin
