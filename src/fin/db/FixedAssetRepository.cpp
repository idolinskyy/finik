#include "fin/db/FixedAssetRepository.h"

#include "fin/auth/Access.h"
#include "fin/core/Checked.h"
#include "fin/core/Sql.h"
#include "fin/db/AccountRepository.h"

#include <QVariant>

namespace fin {
namespace {

Status validateAsset(QSqlDatabase db, const FixedAsset & asset)
{
  if (asset.inventoryNo.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("інвентарний номер обов'язковий")));
  }
  if (asset.name.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("назва ОЗ обов'язкова")));
  }
  if (!asset.assetAccountId) {
    return Status::fail(Error::validation(QStringLiteral("рахунок обліку ОЗ обов'язковий")));
  }
  if (!asset.expenseAccountId) {
    return Status::fail(
        Error::validation(QStringLiteral("рахунок витрат амортизації обов'язковий")));
  }
  if (!asset.original.isPositive()) {
    return Status::fail(
        Error::validation(QStringLiteral("первісна вартість має бути більшою за нуль")));
  }
  if (asset.salvage.isNegative() || asset.salvage.cents() >= asset.original.cents()) {
    return Status::fail(
        Error::validation(QStringLiteral("ліквідаційна вартість має бути меншою за первісну")));
  }
  if (asset.usefulLifeMonths <= 0) {
    return Status::fail(
        Error::validation(QStringLiteral("строк корисного використання — у місяцях > 0")));
  }
  if (asset.openingAccumulated.isNegative() ||
      asset.openingAccumulated.cents() > asset.original.cents() - asset.salvage.cents()) {
    return Status::fail(Error::validation(
        QStringLiteral("знос на початок не може перевищувати вартість, що амортизується")));
  }
  AccountRepository accounts(db);
  const Result<Account> assetAccount = accounts.findById(asset.assetAccountId);
  if (!assetAccount) {
    return assetAccount.asStatus();
  }
  if (!assetAccount.value().postable() || !isFixedAssetAccountCode(assetAccount.value().code)) {
    return Status::fail(
        Error::validation(QStringLiteral("рахунок обліку має бути 104, 105 або 109")));
  }
  const Result<Account> expense = accounts.findById(asset.expenseAccountId);
  if (!expense) {
    return expense.asStatus();
  }
  if (!expense.value().postable()) {
    return Status::fail(Error::validation(QStringLiteral("рахунок витрат не приймає проводок")));
  }
  return Status::ok();
}

FixedAsset fromQuery(sql::Query & query)
{
  FixedAsset row;
  row.id = FixedAssetId{query.value(0).toLongLong()};
  row.inventoryNo = query.value(1).toString();
  row.name = query.value(2).toString();
  row.assetAccountId = AccountId{query.value(3).toLongLong()};
  row.expenseAccountId = AccountId{query.value(4).toLongLong()};
  const QString commissioned = query.value(5).toString();
  if (!commissioned.isEmpty()) {
    row.commissionedOn = QDate::fromString(commissioned, Qt::ISODate);
  }
  row.original = Money::fromCents(query.value(6).toLongLong());
  row.salvage = Money::fromCents(query.value(7).toLongLong());
  row.openingAccumulated = Money::fromCents(query.value(8).toLongLong());
  row.accumulated = Money::fromCents(query.value(9).toLongLong());
  row.usefulLifeMonths = query.value(10).toInt();
  row.status =
      fixedAssetStatusFromString(query.value(11).toString()).value_or(FixedAssetStatus::Draft);
  row.isActive = query.value(12).toInt() != 0;
  const QString disposed = query.value(13).toString();
  if (!disposed.isEmpty()) {
    row.disposedOn = QDate::fromString(disposed, Qt::ISODate);
  }
  return row;
}

const QString kSelect =
    QStringLiteral("SELECT id, inventory_no, name, asset_account_id, expense_account_id, "
                   "commissioned_on, original_cents, salvage_cents, opening_accumulated_cents, "
                   "accumulated_cents, useful_life_months, status, is_active, disposed_on "
                   "FROM fixed_assets");

} // namespace

FixedAssetRepository::FixedAssetRepository(QSqlDatabase db) : db_(std::move(db)) {}

Result<std::vector<FixedAsset>> FixedAssetRepository::listWhere(const QString & where) const
{
  sql::Query query(db_);
  Status status = query.exec(kSelect + where);
  if (!status) {
    return Result<std::vector<FixedAsset>>::fail(status.error());
  }
  std::vector<FixedAsset> rows;
  while (query.next()) {
    rows.push_back(fromQuery(query));
  }
  return Result<std::vector<FixedAsset>>::ok(std::move(rows));
}

Result<FixedAsset> FixedAssetRepository::findById(FixedAssetId id) const
{
  if (!id) {
    return Result<FixedAsset>::fail(
        Error::invalidArgument(QStringLiteral("порожній id основного засобу")));
  }
  sql::Query query(db_);
  Status status = query.prepare(kSelect + QStringLiteral(" WHERE id = :id"));
  if (!status) {
    return Result<FixedAsset>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    return Result<FixedAsset>::fail(status.error());
  }
  if (!query.next()) {
    return Result<FixedAsset>::fail(Error::notFound(QStringLiteral("основний засіб не знайдено")));
  }
  return Result<FixedAsset>::ok(fromQuery(query));
}

Result<FixedAsset> FixedAssetRepository::findByInventoryNo(const QString & inventoryNo) const
{
  const QString key = inventoryNo.trimmed();
  if (key.isEmpty()) {
    return Result<FixedAsset>::fail(
        Error::validation(QStringLiteral("інвентарний номер обов'язковий")));
  }
  sql::Query query(db_);
  Status status = query.prepare(kSelect + QStringLiteral(" WHERE inventory_no = :no"));
  if (!status) {
    return Result<FixedAsset>::fail(status.error());
  }
  query.bind(QStringLiteral(":no"), key);
  status = query.exec();
  if (!status) {
    return Result<FixedAsset>::fail(status.error());
  }
  if (!query.next()) {
    return Result<FixedAsset>::fail(
        Error::notFound(QStringLiteral("основний засіб %1 не знайдено").arg(key)));
  }
  return Result<FixedAsset>::ok(fromQuery(query));
}

Result<std::vector<FixedAsset>> FixedAssetRepository::listAll() const
{
  return listWhere(QStringLiteral(" ORDER BY inventory_no COLLATE NOCASE"));
}

Result<std::vector<FixedAsset>> FixedAssetRepository::listDraft() const
{
  return listWhere(QStringLiteral(
      " WHERE status = 'draft' AND is_active = 1 ORDER BY inventory_no COLLATE NOCASE"));
}

Result<std::vector<FixedAsset>> FixedAssetRepository::listInUse() const
{
  return listWhere(QStringLiteral(
      " WHERE status = 'in_use' AND is_active = 1 ORDER BY inventory_no COLLATE NOCASE"));
}

Result<FixedAssetId> FixedAssetRepository::insert(const FixedAsset & asset)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return Result<FixedAssetId>::fail(access.error());
  }
  access = validateAsset(db_, asset);
  if (!access) {
    return Result<FixedAssetId>::fail(access.error());
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("INSERT INTO fixed_assets(inventory_no, name, asset_account_id, "
                     "expense_account_id, original_cents, salvage_cents, "
                     "opening_accumulated_cents, accumulated_cents, useful_life_months, "
                     "status, is_active) "
                     "VALUES(:inv, :name, :asset, :exp, :orig, :salv, :open, 0, :life, "
                     "'draft', :active)"));
  if (!status) {
    return Result<FixedAssetId>::fail(status.error());
  }
  query.bind(QStringLiteral(":inv"), asset.inventoryNo.trimmed());
  query.bind(QStringLiteral(":name"), asset.name.trimmed());
  query.bind(QStringLiteral(":asset"), static_cast<qlonglong>(asset.assetAccountId.value));
  query.bind(QStringLiteral(":exp"), static_cast<qlonglong>(asset.expenseAccountId.value));
  query.bind(QStringLiteral(":orig"), static_cast<qlonglong>(asset.original.cents()));
  query.bind(QStringLiteral(":salv"), static_cast<qlonglong>(asset.salvage.cents()));
  query.bind(QStringLiteral(":open"), static_cast<qlonglong>(asset.openingAccumulated.cents()));
  query.bind(QStringLiteral(":life"), asset.usefulLifeMonths);
  query.bind(QStringLiteral(":active"), asset.isActive ? 1 : 0);
  status = query.exec();
  if (!status) {
    if (status.message().contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive)) {
      return Result<FixedAssetId>::fail(
          Error::conflict(QStringLiteral("інвентарний номер уже використовується")));
    }
    return Result<FixedAssetId>::fail(status.error());
  }
  return Result<FixedAssetId>::ok(FixedAssetId{query.lastInsertId()});
}

Status FixedAssetRepository::update(const FixedAsset & asset)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  if (!asset.id) {
    return Status::fail(Error::invalidArgument(QStringLiteral("порожній id основного засобу")));
  }
  access = validateAsset(db_, asset);
  if (!access) {
    return access;
  }
  const Result<FixedAsset> existing = findById(asset.id);
  if (!existing) {
    return existing.asStatus();
  }
  if (existing.value().status != FixedAssetStatus::Draft) {
    if (asset.original != existing.value().original || asset.salvage != existing.value().salvage ||
        asset.usefulLifeMonths != existing.value().usefulLifeMonths ||
        asset.assetAccountId != existing.value().assetAccountId ||
        asset.openingAccumulated != existing.value().openingAccumulated) {
      return Status::fail(Error::validation(
          QStringLiteral("після введення в експлуатацію можна змінити лише назву, "
                         "інвентарний номер і рахунок витрат")));
    }
  }
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("UPDATE fixed_assets SET inventory_no = :inv, name = :name, "
                                   "asset_account_id = :asset, expense_account_id = :exp, "
                                   "original_cents = :orig, salvage_cents = :salv, "
                                   "opening_accumulated_cents = :open, useful_life_months = :life, "
                                   "is_active = :active WHERE id = :id AND status = :status"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":inv"), asset.inventoryNo.trimmed());
  query.bind(QStringLiteral(":name"), asset.name.trimmed());
  query.bind(QStringLiteral(":asset"), static_cast<qlonglong>(asset.assetAccountId.value));
  query.bind(QStringLiteral(":exp"), static_cast<qlonglong>(asset.expenseAccountId.value));
  query.bind(QStringLiteral(":orig"), static_cast<qlonglong>(asset.original.cents()));
  query.bind(QStringLiteral(":salv"), static_cast<qlonglong>(asset.salvage.cents()));
  query.bind(QStringLiteral(":open"), static_cast<qlonglong>(asset.openingAccumulated.cents()));
  query.bind(QStringLiteral(":life"), asset.usefulLifeMonths);
  query.bind(QStringLiteral(":active"), asset.isActive ? 1 : 0);
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(asset.id.value));
  query.bind(QStringLiteral(":status"), fixedAssetStatusToString(existing.value().status));
  status = query.exec();
  if (!status) {
    if (status.message().contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive)) {
      return Status::fail(
          Error::conflict(QStringLiteral("інвентарний номер уже використовується")));
    }
    return status;
  }
  return Status::ok();
}

Status FixedAssetRepository::remove(FixedAssetId id)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  const Result<FixedAsset> existing = findById(id);
  if (!existing) {
    return existing.asStatus();
  }
  if (existing.value().status != FixedAssetStatus::Draft) {
    return Status::fail(
        Error::conflict(QStringLiteral("не можна видалити ОЗ, який уже введено або списано")));
  }
  sql::Query used(db_);
  Status status = used.prepare(
      QStringLiteral("SELECT 1 FROM document_lines WHERE fixed_asset_id = :id LIMIT 1"));
  if (!status) {
    return status;
  }
  used.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = used.exec();
  if (!status) {
    return status;
  }
  if (used.next()) {
    return Status::fail(Error::conflict(QStringLiteral("основний засіб використано в документі")));
  }
  sql::Query query(db_);
  status = query.prepare(QStringLiteral("DELETE FROM fixed_assets WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  return query.exec();
}

Status FixedAssetRepository::markCommissioned(FixedAssetId id, QDate on, Money accumulated)
{
  if (!on.isValid()) {
    return Status::fail(
        Error::validation(QStringLiteral("некоректна дата введення в експлуатацію")));
  }
  const Result<FixedAsset> existing = findById(id);
  if (!existing) {
    return existing.asStatus();
  }
  if (existing.value().status != FixedAssetStatus::Draft) {
    return Status::fail(Error::conflict(
        QStringLiteral("%1 уже введено в експлуатацію").arg(existing.value().inventoryNo)));
  }
  if (accumulated.isNegative() ||
      accumulated.cents() > existing.value().original.cents() - existing.value().salvage.cents()) {
    return Status::fail(
        Error::validation(QStringLiteral("%1: знос на початок перевищує вартість, що амортизується")
                              .arg(existing.value().inventoryNo)));
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("UPDATE fixed_assets SET status = 'in_use', commissioned_on = :on, "
                     "accumulated_cents = :acc WHERE id = :id AND status = 'draft'"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":on"), on.toString(Qt::ISODate));
  query.bind(QStringLiteral(":acc"), static_cast<qlonglong>(accumulated.cents()));
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  return query.exec();
}

Status FixedAssetRepository::revertCommission(FixedAssetId id)
{
  const Result<bool> writtenOff = hasPostedWriteOff(id);
  if (!writtenOff) {
    return writtenOff.asStatus();
  }
  if (writtenOff.value()) {
    return Status::fail(
        Error::conflict(QStringLiteral("спочатку зніміть проведення списання по цьому ОЗ")));
  }
  const Result<bool> later = hasPostedDepreciation(id);
  if (!later) {
    return later.asStatus();
  }
  if (later.value()) {
    return Status::fail(
        Error::conflict(QStringLiteral("спочатку зніміть проведення амортизації по цьому ОЗ")));
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("UPDATE fixed_assets SET status = 'draft', commissioned_on = NULL, "
                     "accumulated_cents = 0 WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  return query.exec();
}

Status FixedAssetRepository::addDepreciation(FixedAssetId id, Money amount)
{
  if (!amount.isPositive()) {
    return Status::fail(
        Error::validation(QStringLiteral("сума амортизації має бути більшою за нуль")));
  }
  const Result<FixedAsset> existing = findById(id);
  if (!existing) {
    return existing.asStatus();
  }
  if (existing.value().status != FixedAssetStatus::InUse) {
    return Status::fail(Error::validation(
        QStringLiteral("%1 не введено в експлуатацію").arg(existing.value().inventoryNo)));
  }
  std::int64_t next = existing.value().accumulated.cents();
  Status added = addTo(next, amount.cents(), QStringLiteral("амортизація"));
  if (!added) {
    return added;
  }
  const std::int64_t cap = existing.value().original.cents() - existing.value().salvage.cents();
  if (next > cap) {
    return Status::fail(
        Error::validation(QStringLiteral("%1: амортизація перевищила вартість, що амортизується")
                              .arg(existing.value().inventoryNo)));
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("UPDATE fixed_assets SET accumulated_cents = :acc WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":acc"), static_cast<qlonglong>(next));
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  return query.exec();
}

Status FixedAssetRepository::subtractDepreciation(FixedAssetId id, Money amount)
{
  if (!amount.isPositive()) {
    return Status::fail(
        Error::validation(QStringLiteral("сума амортизації має бути більшою за нуль")));
  }
  const Result<FixedAsset> existing = findById(id);
  if (!existing) {
    return existing.asStatus();
  }
  if (amount.cents() > existing.value().accumulated.cents()) {
    return Status::fail(
        Error::validation(QStringLiteral("%1: не можна зняти амортизацію, якої немає на картці")
                              .arg(existing.value().inventoryNo)));
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("UPDATE fixed_assets SET accumulated_cents = accumulated_cents - :amt "
                     "WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":amt"), static_cast<qlonglong>(amount.cents()));
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  return query.exec();
}

Result<bool> FixedAssetRepository::hasPostedDepreciation(FixedAssetId id) const
{
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("SELECT 1 FROM document_lines l "
                                   "JOIN documents d ON d.id = l.document_id "
                                   "WHERE l.fixed_asset_id = :id AND d.type = 'fa_depreciation' "
                                   "AND d.posted = 1 LIMIT 1"));
  if (!status) {
    return Result<bool>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    return Result<bool>::fail(status.error());
  }
  return Result<bool>::ok(query.next());
}

Result<bool> FixedAssetRepository::hasPostedWriteOff(FixedAssetId id) const
{
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("SELECT 1 FROM document_lines l "
                                   "JOIN documents d ON d.id = l.document_id "
                                   "WHERE l.fixed_asset_id = :id AND d.type = 'fa_write_off' "
                                   "AND d.posted = 1 LIMIT 1"));
  if (!status) {
    return Result<bool>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    return Result<bool>::fail(status.error());
  }
  return Result<bool>::ok(query.next());
}

Result<QDate> FixedAssetRepository::lastPostedDepreciationDate(FixedAssetId id) const
{
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("SELECT MAX(d.date) FROM document_lines l "
                                   "JOIN documents d ON d.id = l.document_id "
                                   "WHERE l.fixed_asset_id = :id AND d.type = 'fa_depreciation' "
                                   "AND d.posted = 1"));
  if (!status) {
    return Result<QDate>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    return Result<QDate>::fail(status.error());
  }
  if (!query.next() || query.value(0).isNull()) {
    return Result<QDate>::ok(QDate());
  }
  return Result<QDate>::ok(QDate::fromString(query.value(0).toString(), Qt::ISODate));
}

Status FixedAssetRepository::markDisposed(FixedAssetId id, QDate on)
{
  if (!on.isValid()) {
    return Status::fail(Error::validation(QStringLiteral("некоректна дата списання")));
  }
  const Result<FixedAsset> existing = findById(id);
  if (!existing) {
    return existing.asStatus();
  }
  if (existing.value().status != FixedAssetStatus::InUse) {
    return Status::fail(Error::conflict(QStringLiteral("%1 не в експлуатації, списати не можна")
                                            .arg(existing.value().inventoryNo)));
  }
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("UPDATE fixed_assets SET status = 'disposed', disposed_on = :on "
                                   "WHERE id = :id AND status = 'in_use'"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":on"), on.toString(Qt::ISODate));
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  return query.exec();
}

Status FixedAssetRepository::revertDispose(FixedAssetId id)
{
  const Result<FixedAsset> existing = findById(id);
  if (!existing) {
    return existing.asStatus();
  }
  if (existing.value().status != FixedAssetStatus::Disposed) {
    return Status::fail(
        Error::conflict(QStringLiteral("%1 не списано").arg(existing.value().inventoryNo)));
  }
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("UPDATE fixed_assets SET status = 'in_use', disposed_on = NULL "
                                   "WHERE id = :id AND status = 'disposed'"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  return query.exec();
}

} // namespace fin
