#include "fin/db/NomenclatureRepository.h"

#include "fin/auth/Access.h"
#include "fin/core/Sql.h"

#include <QVariant>

namespace fin {
namespace {

Status validateItem(const Nomenclature & item)
{
  if (item.name.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("назва номенклатури обов'язкова")));
  }
  if (item.unit.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("одиниця виміру обов'язкова")));
  }
  return Status::ok();
}

Nomenclature fromQuery(sql::Query & query)
{
  Nomenclature row;
  row.id = NomenclatureId{query.value(0).toLongLong()};
  row.name = query.value(1).toString();
  row.sku = query.value(2).toString();
  row.unit = query.value(3).toString();
  row.isActive = query.value(4).toInt() != 0;
  return row;
}

const QString kSelect = QStringLiteral("SELECT id, name, sku, unit, is_active FROM nomenclature");

} // namespace

NomenclatureRepository::NomenclatureRepository(QSqlDatabase db) : db_(std::move(db)) {}

Result<Nomenclature> NomenclatureRepository::findById(NomenclatureId id) const
{
  if (!id) {
    return Result<Nomenclature>::fail(
        Error::invalidArgument(QStringLiteral("порожній id номенклатури")));
  }
  sql::Query query(db_);
  Status status = query.prepare(kSelect + QStringLiteral(" WHERE id = :id"));
  if (!status) {
    return Result<Nomenclature>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    return Result<Nomenclature>::fail(status.error());
  }
  if (!query.next()) {
    return Result<Nomenclature>::fail(Error::notFound(QStringLiteral("номенклатуру не знайдено")));
  }
  return Result<Nomenclature>::ok(fromQuery(query));
}

Result<std::vector<Nomenclature>> NomenclatureRepository::listAll() const
{
  sql::Query query(db_);
  Status status = query.exec(kSelect + QStringLiteral(" ORDER BY name COLLATE NOCASE"));
  if (!status) {
    return Result<std::vector<Nomenclature>>::fail(status.error());
  }
  std::vector<Nomenclature> rows;
  while (query.next()) {
    rows.push_back(fromQuery(query));
  }
  return Result<std::vector<Nomenclature>>::ok(std::move(rows));
}

Result<std::vector<Nomenclature>> NomenclatureRepository::listActive() const
{
  sql::Query query(db_);
  Status status =
      query.exec(kSelect + QStringLiteral(" WHERE is_active = 1 ORDER BY name COLLATE NOCASE"));
  if (!status) {
    return Result<std::vector<Nomenclature>>::fail(status.error());
  }
  std::vector<Nomenclature> rows;
  while (query.next()) {
    rows.push_back(fromQuery(query));
  }
  return Result<std::vector<Nomenclature>>::ok(std::move(rows));
}

Result<NomenclatureId> NomenclatureRepository::insert(const Nomenclature & item)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return Result<NomenclatureId>::fail(access.error());
  }
  const Status valid = validateItem(item);
  if (!valid) {
    return Result<NomenclatureId>::fail(valid.error());
  }
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("INSERT INTO nomenclature(name, sku, unit, is_active) "
                                   "VALUES(:name, :sku, :unit, :active)"));
  if (!status) {
    return Result<NomenclatureId>::fail(status.error());
  }
  query.bind(QStringLiteral(":name"), item.name.trimmed());
  const QString sku = item.sku.trimmed();
  query.bind(QStringLiteral(":sku"), sku.isEmpty() ? QVariant() : QVariant(sku));
  query.bind(QStringLiteral(":unit"), item.unit.trimmed());
  query.bind(QStringLiteral(":active"), item.isActive ? 1 : 0);
  status = query.exec();
  if (!status) {
    if (status.message().contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive)) {
      return Result<NomenclatureId>::fail(
          Error::conflict(QStringLiteral("артикул уже використовується")));
    }
    return Result<NomenclatureId>::fail(status.error());
  }
  return Result<NomenclatureId>::ok(NomenclatureId{query.lastInsertId()});
}

Status NomenclatureRepository::update(const Nomenclature & item)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  if (!item.id) {
    return Status::fail(Error::invalidArgument(QStringLiteral("порожній id номенклатури")));
  }
  const Status valid = validateItem(item);
  if (!valid) {
    return valid;
  }
  sql::Query query(db_);
  Status status = query.prepare(QStringLiteral("UPDATE nomenclature SET name = :name, sku = :sku, "
                                               "unit = :unit, is_active = :active WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":name"), item.name.trimmed());
  const QString sku = item.sku.trimmed();
  query.bind(QStringLiteral(":sku"), sku.isEmpty() ? QVariant() : QVariant(sku));
  query.bind(QStringLiteral(":unit"), item.unit.trimmed());
  query.bind(QStringLiteral(":active"), item.isActive ? 1 : 0);
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(item.id.value));
  status = query.exec();
  if (!status) {
    if (status.message().contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive)) {
      return Status::fail(Error::conflict(QStringLiteral("артикул уже використовується")));
    }
    return status;
  }
  if (query.raw().numRowsAffected() <= 0) {
    return Status::fail(Error::notFound(QStringLiteral("номенклатуру не знайдено")));
  }
  return Status::ok();
}

Status NomenclatureRepository::remove(NomenclatureId id)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  if (!id) {
    return Status::fail(Error::invalidArgument(QStringLiteral("порожній id номенклатури")));
  }
  sql::Query query(db_);
  Status status = query.prepare(QStringLiteral("DELETE FROM nomenclature WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    if (status.message().contains(QLatin1String("FOREIGN KEY"), Qt::CaseInsensitive)) {
      return Status::fail(Error::conflict(
          QStringLiteral("номенклатуру не можна видалити: є документи або залишки")));
    }
    return status;
  }
  if (query.raw().numRowsAffected() <= 0) {
    return Status::fail(Error::notFound(QStringLiteral("номенклатуру не знайдено")));
  }
  return Status::ok();
}

} // namespace fin
