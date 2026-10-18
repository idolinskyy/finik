#include "fin/db/IntangibleAssetRepository.h"

#include "fin/auth/Access.h"
#include "fin/core/Checked.h"
#include "fin/core/Sql.h"
#include "fin/db/AccountRepository.h"

#include <QVariant>

namespace fin {
namespace {

Status validateAsset(QSqlDatabase db, const IntangibleAsset & asset)
{
  if (asset.inventoryNo.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("інвентарний номер обов'язковий")));
  }
  if (asset.name.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("назва НМА обов'язкова")));
  }
  if (!asset.assetAccountId) {
    return Status::fail(Error::validation(QStringLiteral("рахунок обліку НМА обов'язковий")));
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
  if (!assetAccount.value().postable() || !isIntangibleAccountCode(assetAccount.value().code)) {
    return Status::fail(Error::validation(QStringLiteral("рахунок обліку має бути 125 або 127")));
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

IntangibleAsset fromQuery(sql::Query & query)
{
  IntangibleAsset row;
  row.id = IntangibleId{query.value(0).toLongLong()};
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
      intangibleStatusFromString(query.value(11).toString()).value_or(IntangibleStatus::Draft);
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
                   "FROM intangible_assets");

} // namespace

IntangibleAssetRepository::IntangibleAssetRepository(QSqlDatabase db) : db_(std::move(db)) {}

Result<std::vector<IntangibleAsset>>
IntangibleAssetRepository::listWhere(const QString & where) const
{
  sql::Query query(db_);
  Status status = query.exec(kSelect + where);
  if (!status) {
    return Result<std::vector<IntangibleAsset>>::fail(status.error());
  }
  std::vector<IntangibleAsset> rows;
  while (query.next()) {
    rows.push_back(fromQuery(query));
  }
  return Result<std::vector<IntangibleAsset>>::ok(std::move(rows));
}

Result<IntangibleAsset> IntangibleAssetRepository::findById(IntangibleId id) const
{
  if (!id) {
    return Result<IntangibleAsset>::fail(
        Error::invalidArgument(QStringLiteral("порожній id нематеріального активу")));
  }
  sql::Query query(db_);
  Status status = query.prepare(kSelect + QStringLiteral(" WHERE id = :id"));
  if (!status) {
    return Result<IntangibleAsset>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    return Result<IntangibleAsset>::fail(status.error());
  }
  if (!query.next()) {
    return Result<IntangibleAsset>::fail(
        Error::notFound(QStringLiteral("нематеріальний актив не знайдено")));
  }
  return Result<IntangibleAsset>::ok(fromQuery(query));
}

Result<IntangibleAsset>
IntangibleAssetRepository::findByInventoryNo(const QString & inventoryNo) const
{
  const QString key = inventoryNo.trimmed();
  if (key.isEmpty()) {
    return Result<IntangibleAsset>::fail(
        Error::validation(QStringLiteral("інвентарний номер обов'язковий")));
  }
  sql::Query query(db_);
  Status status = query.prepare(kSelect + QStringLiteral(" WHERE inventory_no = :no"));
  if (!status) {
    return Result<IntangibleAsset>::fail(status.error());
  }
  query.bind(QStringLiteral(":no"), key);
  status = query.exec();
  if (!status) {
    return Result<IntangibleAsset>::fail(status.error());
  }
  if (!query.next()) {
    return Result<IntangibleAsset>::fail(
        Error::notFound(QStringLiteral("нематеріальний актив %1 не знайдено").arg(key)));
  }
  return Result<IntangibleAsset>::ok(fromQuery(query));
}

Result<std::vector<IntangibleAsset>> IntangibleAssetRepository::listAll() const
{
  return listWhere(QStringLiteral(" ORDER BY inventory_no COLLATE NOCASE"));
}

Result<std::vector<IntangibleAsset>> IntangibleAssetRepository::listDraft() const
{
  return listWhere(QStringLiteral(
      " WHERE status = 'draft' AND is_active = 1 ORDER BY inventory_no COLLATE NOCASE"));
}

Result<std::vector<IntangibleAsset>> IntangibleAssetRepository::listInUse() const
{
  return listWhere(QStringLiteral(
      " WHERE status = 'in_use' AND is_active = 1 ORDER BY inventory_no COLLATE NOCASE"));
}

Result<IntangibleId> IntangibleAssetRepository::insert(const IntangibleAsset & asset)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return Result<IntangibleId>::fail(access.error());
  }
  access = validateAsset(db_, asset);
  if (!access) {
    return Result<IntangibleId>::fail(access.error());
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("INSERT INTO intangible_assets(inventory_no, name, asset_account_id, "
                     "expense_account_id, original_cents, salvage_cents, "
                     "opening_accumulated_cents, accumulated_cents, useful_life_months, "
                     "status, is_active) "
                     "VALUES(:inv, :name, :asset, :exp, :orig, :salv, :open, 0, :life, "
                     "'draft', :active)"));
  if (!status) {
    return Result<IntangibleId>::fail(status.error());
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
      return Result<IntangibleId>::fail(
          Error::conflict(QStringLiteral("інвентарний номер уже використовується")));
    }
    return Result<IntangibleId>::fail(status.error());
  }
  return Result<IntangibleId>::ok(IntangibleId{query.lastInsertId()});
}

Status IntangibleAssetRepository::update(const IntangibleAsset & asset)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  if (!asset.id) {
    return Status::fail(
        Error::invalidArgument(QStringLiteral("порожній id нематеріального активу")));
  }
  access = validateAsset(db_, asset);
  if (!access) {
    return access;
  }
  const Result<IntangibleAsset> existing = findById(asset.id);
  if (!existing) {
    return existing.asStatus();
  }
  if (existing.value().status != IntangibleStatus::Draft) {
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
  Status status = query.prepare(
      QStringLiteral("UPDATE intangible_assets SET inventory_no = :inv, name = :name, "
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
  query.bind(QStringLiteral(":status"), intangibleStatusToString(existing.value().status));
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

Status IntangibleAssetRepository::remove(IntangibleId id)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  const Result<IntangibleAsset> existing = findById(id);
  if (!existing) {
    return existing.asStatus();
  }
  if (existing.value().status != IntangibleStatus::Draft) {
    return Status::fail(
        Error::conflict(QStringLiteral("не можна видалити НМА, який уже введено або списано")));
  }
  sql::Query used(db_);
  Status status = used.prepare(
      QStringLiteral("SELECT 1 FROM document_lines WHERE intangible_id = :id LIMIT 1"));
  if (!status) {
    return status;
  }
  used.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = used.exec();
  if (!status) {
    return status;
  }
  if (used.next()) {
    return Status::fail(
        Error::conflict(QStringLiteral("нематеріальний актив використано в документі")));
  }
  sql::Query query(db_);
  status = query.prepare(QStringLiteral("DELETE FROM intangible_assets WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  return query.exec();
}

Status IntangibleAssetRepository::markCommissioned(IntangibleId id, QDate on, Money accumulated)
{
  if (!on.isValid()) {
    return Status::fail(
        Error::validation(QStringLiteral("некоректна дата введення в експлуатацію")));
  }
  const Result<IntangibleAsset> existing = findById(id);
  if (!existing) {
    return existing.asStatus();
  }
  if (existing.value().status != IntangibleStatus::Draft) {
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
      QStringLiteral("UPDATE intangible_assets SET status = 'in_use', commissioned_on = :on, "
                     "accumulated_cents = :acc WHERE id = :id AND status = 'draft'"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":on"), on.toString(Qt::ISODate));
  query.bind(QStringLiteral(":acc"), static_cast<qlonglong>(accumulated.cents()));
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  return query.exec();
}

Status IntangibleAssetRepository::revertCommission(IntangibleId id)
{
  const Result<bool> writtenOff = hasPostedWriteOff(id);
  if (!writtenOff) {
    return writtenOff.asStatus();
  }
  if (writtenOff.value()) {
    return Status::fail(
        Error::conflict(QStringLiteral("спочатку зніміть проведення списання по цьому НМА")));
  }
  const Result<bool> later = hasPostedDepreciation(id);
  if (!later) {
    return later.asStatus();
  }
  if (later.value()) {
    return Status::fail(
        Error::conflict(QStringLiteral("спочатку зніміть проведення амортизації по цьому НМА")));
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("UPDATE intangible_assets SET status = 'draft', commissioned_on = NULL, "
                     "accumulated_cents = 0 WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  return query.exec();
}

Status IntangibleAssetRepository::addDepreciation(IntangibleId id, Money amount)
{
  if (!amount.isPositive()) {
    return Status::fail(
        Error::validation(QStringLiteral("сума амортизації має бути більшою за нуль")));
  }
  const Result<IntangibleAsset> existing = findById(id);
  if (!existing) {
    return existing.asStatus();
  }
  if (existing.value().status != IntangibleStatus::InUse) {
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
      QStringLiteral("UPDATE intangible_assets SET accumulated_cents = :acc WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":acc"), static_cast<qlonglong>(next));
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  return query.exec();
}

Status IntangibleAssetRepository::subtractDepreciation(IntangibleId id, Money amount)
{
  if (!amount.isPositive()) {
    return Status::fail(
        Error::validation(QStringLiteral("сума амортизації має бути більшою за нуль")));
  }
  const Result<IntangibleAsset> existing = findById(id);
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
      QStringLiteral("UPDATE intangible_assets SET accumulated_cents = accumulated_cents - :amt "
                     "WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":amt"), static_cast<qlonglong>(amount.cents()));
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  return query.exec();
}

Result<bool> IntangibleAssetRepository::hasPostedDepreciation(IntangibleId id) const
{
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("SELECT 1 FROM document_lines l "
                                   "JOIN documents d ON d.id = l.document_id "
                                   "WHERE l.intangible_id = :id AND d.type = 'ia_depreciation' "
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

Result<bool> IntangibleAssetRepository::hasPostedWriteOff(IntangibleId id) const
{
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("SELECT 1 FROM document_lines l "
                                   "JOIN documents d ON d.id = l.document_id "
                                   "WHERE l.intangible_id = :id AND d.type = 'ia_write_off' "
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

Result<QDate> IntangibleAssetRepository::lastPostedDepreciationDate(IntangibleId id) const
{
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("SELECT MAX(d.date) FROM document_lines l "
                                   "JOIN documents d ON d.id = l.document_id "
                                   "WHERE l.intangible_id = :id AND d.type = 'ia_depreciation' "
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

Status IntangibleAssetRepository::markDisposed(IntangibleId id, QDate on)
{
  if (!on.isValid()) {
    return Status::fail(Error::validation(QStringLiteral("некоректна дата списання")));
  }
  const Result<IntangibleAsset> existing = findById(id);
  if (!existing) {
    return existing.asStatus();
  }
  if (existing.value().status != IntangibleStatus::InUse) {
    return Status::fail(Error::conflict(QStringLiteral("%1 не в експлуатації, списати не можна")
                                            .arg(existing.value().inventoryNo)));
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("UPDATE intangible_assets SET status = 'disposed', disposed_on = :on "
                     "WHERE id = :id AND status = 'in_use'"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":on"), on.toString(Qt::ISODate));
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  return query.exec();
}

Status IntangibleAssetRepository::revertDispose(IntangibleId id)
{
  const Result<IntangibleAsset> existing = findById(id);
  if (!existing) {
    return existing.asStatus();
  }
  if (existing.value().status != IntangibleStatus::Disposed) {
    return Status::fail(
        Error::conflict(QStringLiteral("%1 не списано").arg(existing.value().inventoryNo)));
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("UPDATE intangible_assets SET status = 'in_use', disposed_on = NULL "
                     "WHERE id = :id AND status = 'disposed'"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  return query.exec();
}

} // namespace fin
