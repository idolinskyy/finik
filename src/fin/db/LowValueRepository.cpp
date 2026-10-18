#include "fin/db/LowValueRepository.h"

#include "fin/auth/Access.h"
#include "fin/core/Checked.h"
#include "fin/core/Sql.h"

#include <QLatin1String>
#include <QVariant>

namespace fin {
namespace {

Status validateItem(const LowValueItem & item)
{
  if (item.inventoryNo.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("інвентарний номер МШП обов'язковий")));
  }
  if (item.name.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("назва МШП обов'язкова")));
  }
  if (item.unit.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("одиниця виміру МШП обов'язкова")));
  }
  return Status::ok();
}

LowValueItem fromQuery(sql::Query & query)
{
  LowValueItem row;
  row.id = LowValueId{query.value(0).toLongLong()};
  row.inventoryNo = query.value(1).toString();
  row.name = query.value(2).toString();
  row.unit = query.value(3).toString();
  row.quantity = Quantity::fromMilli(query.value(4).toLongLong());
  row.amount = Money::fromCents(query.value(5).toLongLong());
  row.isActive = query.value(6).toInt() != 0;
  return row;
}

const QString kSelect =
    QStringLiteral("SELECT id, inventory_no, name, unit, quantity_milli, amount_cents, is_active "
                   "FROM low_value_items");

} // namespace

LowValueRepository::LowValueRepository(QSqlDatabase db) : db_(std::move(db)) {}

Result<std::vector<LowValueItem>> LowValueRepository::listWhere(const QString & where) const
{
  sql::Query query(db_);
  Status status = query.exec(kSelect + where);
  if (!status) {
    return Result<std::vector<LowValueItem>>::fail(status.error());
  }
  std::vector<LowValueItem> rows;
  while (query.next()) {
    rows.push_back(fromQuery(query));
  }
  return Result<std::vector<LowValueItem>>::ok(std::move(rows));
}

Result<LowValueItem> LowValueRepository::findById(LowValueId id) const
{
  if (!id) {
    return Result<LowValueItem>::fail(Error::invalidArgument(QStringLiteral("порожній id МШП")));
  }
  sql::Query query(db_);
  Status status = query.prepare(kSelect + QStringLiteral(" WHERE id = :id"));
  if (!status) {
    return Result<LowValueItem>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    return Result<LowValueItem>::fail(status.error());
  }
  if (!query.next()) {
    return Result<LowValueItem>::fail(Error::notFound(QStringLiteral("МШП не знайдено")));
  }
  return Result<LowValueItem>::ok(fromQuery(query));
}

Result<LowValueItem> LowValueRepository::findByInventoryNo(const QString & inventoryNo) const
{
  const QString key = inventoryNo.trimmed();
  if (key.isEmpty()) {
    return Result<LowValueItem>::fail(
        Error::validation(QStringLiteral("інвентарний номер МШП обов'язковий")));
  }
  sql::Query query(db_);
  Status status = query.prepare(kSelect + QStringLiteral(" WHERE inventory_no = :no"));
  if (!status) {
    return Result<LowValueItem>::fail(status.error());
  }
  query.bind(QStringLiteral(":no"), key);
  status = query.exec();
  if (!status) {
    return Result<LowValueItem>::fail(status.error());
  }
  if (!query.next()) {
    return Result<LowValueItem>::fail(
        Error::notFound(QStringLiteral("МШП %1 не знайдено").arg(key)));
  }
  return Result<LowValueItem>::ok(fromQuery(query));
}

Result<std::vector<LowValueItem>> LowValueRepository::listAll() const
{
  return listWhere(QStringLiteral(" ORDER BY inventory_no COLLATE NOCASE"));
}

Result<std::vector<LowValueItem>> LowValueRepository::listActive() const
{
  return listWhere(QStringLiteral(" WHERE is_active = 1 ORDER BY inventory_no COLLATE NOCASE"));
}

Result<LowValueId> LowValueRepository::insert(const LowValueItem & item)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return Result<LowValueId>::fail(access.error());
  }
  const Status valid = validateItem(item);
  if (!valid) {
    return Result<LowValueId>::fail(valid.error());
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("INSERT INTO low_value_items(inventory_no, name, unit, quantity_milli, "
                     "amount_cents, is_active) VALUES(:inv, :name, :unit, 0, 0, :active)"));
  if (!status) {
    return Result<LowValueId>::fail(status.error());
  }
  query.bind(QStringLiteral(":inv"), item.inventoryNo.trimmed());
  query.bind(QStringLiteral(":name"), item.name.trimmed());
  query.bind(QStringLiteral(":unit"), item.unit.trimmed());
  query.bind(QStringLiteral(":active"), item.isActive ? 1 : 0);
  status = query.exec();
  if (!status) {
    if (status.message().contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive)) {
      return Result<LowValueId>::fail(
          Error::conflict(QStringLiteral("інвентарний номер МШП уже використовується")));
    }
    return Result<LowValueId>::fail(status.error());
  }
  return Result<LowValueId>::ok(LowValueId{query.lastInsertId()});
}

Status LowValueRepository::update(const LowValueItem & item)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  if (!item.id) {
    return Status::fail(Error::invalidArgument(QStringLiteral("порожній id МШП")));
  }
  const Status valid = validateItem(item);
  if (!valid) {
    return valid;
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("UPDATE low_value_items SET inventory_no = :inv, name = :name, unit = :unit, "
                     "is_active = :active WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":inv"), item.inventoryNo.trimmed());
  query.bind(QStringLiteral(":name"), item.name.trimmed());
  query.bind(QStringLiteral(":unit"), item.unit.trimmed());
  query.bind(QStringLiteral(":active"), item.isActive ? 1 : 0);
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(item.id.value));
  status = query.exec();
  if (!status) {
    if (status.message().contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive)) {
      return Status::fail(
          Error::conflict(QStringLiteral("інвентарний номер МШП уже використовується")));
    }
    return status;
  }
  if (query.raw().numRowsAffected() <= 0) {
    return Status::fail(Error::notFound(QStringLiteral("МШП не знайдено")));
  }
  return Status::ok();
}

Status LowValueRepository::remove(LowValueId id)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  const Result<LowValueItem> existing = findById(id);
  if (!existing) {
    return existing.asStatus();
  }
  if (!existing.value().quantity.isZero() || !existing.value().amount.isZero()) {
    return Status::fail(Error::conflict(QStringLiteral("не можна видалити МШП із залишком на 22")));
  }
  sql::Query used(db_);
  Status status =
      used.prepare(QStringLiteral("SELECT 1 FROM document_lines WHERE low_value_id = :id LIMIT 1"));
  if (!status) {
    return status;
  }
  used.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = used.exec();
  if (!status) {
    return status;
  }
  if (used.next()) {
    return Status::fail(Error::conflict(QStringLiteral("картка МШП використана в документах")));
  }
  sql::Query query(db_);
  status = query.prepare(QStringLiteral("DELETE FROM low_value_items WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  return query.exec();
}

Status LowValueRepository::shiftStock(LowValueId id, Quantity quantity, Money amount,
                                      const QString & role)
{
  const Result<LowValueItem> existing = findById(id);
  if (!existing) {
    return existing.asStatus();
  }
  const Result<Quantity> nextQty = existing.value().quantity.plus(quantity);
  if (!nextQty) {
    return nextQty.asStatus();
  }
  const Result<Money> nextAmt = existing.value().amount.plus(amount);
  if (!nextAmt) {
    return nextAmt.asStatus();
  }
  if (nextQty.value().isNegative() || nextAmt.value().isNegative()) {
    return Status::fail(Error::conflict(QStringLiteral("%1: залишок МШП не може бути від'ємним")
                                            .arg(existing.value().inventoryNo)));
  }
  if (nextQty.value().isZero() && nextAmt.value().isPositive()) {
    return Status::fail(Error::conflict(QStringLiteral("%1: нульова кількість із залишком суми")
                                            .arg(existing.value().inventoryNo)));
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("UPDATE low_value_items SET quantity_milli = :qty, amount_cents = :amt "
                     "WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":qty"), static_cast<qlonglong>(nextQty.value().milli()));
  query.bind(QStringLiteral(":amt"), static_cast<qlonglong>(nextAmt.value().cents()));
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  (void)role;
  return query.exec();
}

Status LowValueRepository::receive(LowValueId id, Quantity quantity, Money amount)
{
  if (!quantity.isPositive() || !amount.isPositive()) {
    return Status::fail(Error::validation(
        QStringLiteral("надходження МШП: кількість і сума мають бути більші за нуль")));
  }
  return shiftStock(id, quantity, amount, QStringLiteral("надходження"));
}

Status LowValueRepository::issue(LowValueId id, Quantity quantity, Money amount)
{
  const Result<Quantity> negQty = quantity.negated();
  const Result<Money> negAmt = amount.negated();
  if (!negQty || !negAmt) {
    return Status::fail(Error::overflow(QStringLiteral("списання МШП")));
  }
  return shiftStock(id, negQty.value(), negAmt.value(), QStringLiteral("передача"));
}

Status LowValueRepository::revertReceive(LowValueId id, Quantity quantity, Money amount)
{
  return issue(id, quantity, amount);
}

Status LowValueRepository::revertIssue(LowValueId id, Quantity quantity, Money amount)
{
  return receive(id, quantity, amount);
}

} // namespace fin
