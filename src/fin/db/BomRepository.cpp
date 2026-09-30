#include "fin/db/BomRepository.h"

#include "fin/auth/Access.h"
#include "fin/core/Sql.h"
#include "fin/core/Transaction.h"

#include <QVariant>

namespace fin {
namespace {

Status validateBom(const BillOfMaterials & bom)
{
  if (!bom.productId) {
    return Status::fail(
        Error::validation(QStringLiteral("готова продукція обов'язкова для специфікації")));
  }
  if (bom.name.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("назва специфікації обов'язкова")));
  }
  if (bom.outputQuantity.milli() <= 0) {
    return Status::fail(
        Error::validation(QStringLiteral("кількість випуску за нормою повинна бути більшою за 0")));
  }
  for (const BomLine & line : bom.lines) {
    if (!line.componentId) {
      return Status::fail(
          Error::validation(QStringLiteral("матеріал/компонент рядка специфікації обов'язковий")));
    }
    if (line.quantity.milli() <= 0) {
      return Status::fail(Error::validation(
          QStringLiteral("кількість матеріалу в рядку повинна бути більшою за 0")));
    }
  }
  return Status::ok();
}

BillOfMaterials bomFromQuery(sql::Query & query)
{
  BillOfMaterials row;
  row.id = BomId{query.value(0).toLongLong()};
  row.productId = NomenclatureId{query.value(1).toLongLong()};
  row.name = query.value(2).toString();
  row.outputQuantity = Quantity::fromMilli(query.value(3).toLongLong());
  row.isDefault = query.value(4).toInt() != 0;
  row.isActive = query.value(5).toInt() != 0;
  row.comment = query.value(6).toString();
  return row;
}

BomLine lineFromQuery(sql::Query & query)
{
  BomLine row;
  row.lineNo = query.value(0).toInt();
  row.componentId = NomenclatureId{query.value(1).toLongLong()};
  row.quantity = Quantity::fromMilli(query.value(2).toLongLong());
  row.comment = query.value(3).toString();
  return row;
}

const QString kSelectBom = QStringLiteral(
    "SELECT id, product_id, name, output_quantity_milli, is_default, is_active, comment "
    "FROM bill_of_materials");

const QString kSelectLines =
    QStringLiteral("SELECT line_no, component_id, quantity_milli, comment "
                   "FROM bom_lines WHERE bom_id = :bom_id ORDER BY line_no ASC");

} // namespace

BomRepository::BomRepository(QSqlDatabase db) : db_(std::move(db)) {}

Result<std::vector<BomLine>> BomRepository::loadLines(BomId bomId) const
{
  sql::Query query(db_);
  Status status = query.prepare(kSelectLines);
  if (!status) {
    return Result<std::vector<BomLine>>::fail(status.error());
  }
  query.bind(QStringLiteral(":bom_id"), static_cast<qlonglong>(bomId.value));
  status = query.exec();
  if (!status) {
    return Result<std::vector<BomLine>>::fail(status.error());
  }
  std::vector<BomLine> lines;
  while (query.next()) {
    lines.push_back(lineFromQuery(query));
  }
  return Result<std::vector<BomLine>>::ok(std::move(lines));
}

Result<std::vector<BillOfMaterials>> BomRepository::listAll(bool activeOnly) const
{
  sql::Query query(db_);
  QString sql = kSelectBom;
  if (activeOnly) {
    sql += QStringLiteral(" WHERE is_active = 1");
  }
  sql += QStringLiteral(" ORDER BY name ASC");

  Status status = query.prepare(sql);
  if (!status) {
    return Result<std::vector<BillOfMaterials>>::fail(status.error());
  }
  status = query.exec();
  if (!status) {
    return Result<std::vector<BillOfMaterials>>::fail(status.error());
  }
  std::vector<BillOfMaterials> list;
  while (query.next()) {
    list.push_back(bomFromQuery(query));
  }
  return Result<std::vector<BillOfMaterials>>::ok(std::move(list));
}

Result<std::vector<BillOfMaterials>> BomRepository::listForProduct(NomenclatureId productId,
                                                                   bool activeOnly) const
{
  if (!productId) {
    return Result<std::vector<BillOfMaterials>>::fail(
        Error::invalidArgument(QStringLiteral("порожній id продукції")));
  }
  sql::Query query(db_);
  QString sql = kSelectBom + QStringLiteral(" WHERE product_id = :pid");
  if (activeOnly) {
    sql += QStringLiteral(" AND is_active = 1");
  }
  sql += QStringLiteral(" ORDER BY is_default DESC, name ASC");

  Status status = query.prepare(sql);
  if (!status) {
    return Result<std::vector<BillOfMaterials>>::fail(status.error());
  }
  query.bind(QStringLiteral(":pid"), static_cast<qlonglong>(productId.value));
  status = query.exec();
  if (!status) {
    return Result<std::vector<BillOfMaterials>>::fail(status.error());
  }
  std::vector<BillOfMaterials> list;
  while (query.next()) {
    list.push_back(bomFromQuery(query));
  }
  return Result<std::vector<BillOfMaterials>>::ok(std::move(list));
}

Result<BillOfMaterials> BomRepository::findById(BomId id) const
{
  if (!id) {
    return Result<BillOfMaterials>::fail(
        Error::invalidArgument(QStringLiteral("порожній id специфікації")));
  }
  sql::Query query(db_);
  Status status = query.prepare(kSelectBom + QStringLiteral(" WHERE id = :id"));
  if (!status) {
    return Result<BillOfMaterials>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    return Result<BillOfMaterials>::fail(status.error());
  }
  if (!query.next()) {
    return Result<BillOfMaterials>::fail(
        Error::notFound(QStringLiteral("специфікацію не знайдено")));
  }
  BillOfMaterials bom = bomFromQuery(query);
  const auto linesRes = loadLines(bom.id);
  if (!linesRes) {
    return Result<BillOfMaterials>::fail(linesRes.error());
  }
  bom.lines = linesRes.value();
  return Result<BillOfMaterials>::ok(std::move(bom));
}

Result<BillOfMaterials> BomRepository::findDefault(NomenclatureId productId) const
{
  if (!productId) {
    return Result<BillOfMaterials>::fail(
        Error::invalidArgument(QStringLiteral("порожній id продукції")));
  }
  sql::Query query(db_);
  Status status = query.prepare(
      kSelectBom +
      QStringLiteral(
          " WHERE product_id = :pid AND is_active = 1 ORDER BY is_default DESC, id ASC LIMIT 1"));
  if (!status) {
    return Result<BillOfMaterials>::fail(status.error());
  }
  query.bind(QStringLiteral(":pid"), static_cast<qlonglong>(productId.value));
  status = query.exec();
  if (!status) {
    return Result<BillOfMaterials>::fail(status.error());
  }
  if (!query.next()) {
    return Result<BillOfMaterials>::fail(
        Error::notFound(QStringLiteral("основну специфікацію для виробу не знайдено")));
  }
  BillOfMaterials bom = bomFromQuery(query);
  const auto linesRes = loadLines(bom.id);
  if (!linesRes) {
    return Result<BillOfMaterials>::fail(linesRes.error());
  }
  bom.lines = linesRes.value();
  return Result<BillOfMaterials>::ok(std::move(bom));
}

Result<BomId> BomRepository::insert(const BillOfMaterials & bom)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return Result<BomId>::fail(access.error());
  }
  Status status = validateBom(bom);
  if (!status) {
    return Result<BomId>::fail(status.error());
  }

  Transaction txn(db_);
  if (!txn.started()) {
    return Result<BomId>::fail(txn.startError().error());
  }

  // Якщо створюється дефолтна специфікація, знімаємо прапорець з інших
  if (bom.isDefault) {
    sql::Query clearDef(db_);
    status = clearDef.prepare(
        QStringLiteral("UPDATE bill_of_materials SET is_default = 0 WHERE product_id = :pid"));
    if (!status) {
      return Result<BomId>::fail(status.error());
    }
    clearDef.bind(QStringLiteral(":pid"), static_cast<qlonglong>(bom.productId.value));
    status = clearDef.exec();
    if (!status) {
      return Result<BomId>::fail(status.error());
    }
  }

  sql::Query query(db_);
  status = query.prepare(
      QStringLiteral("INSERT INTO bill_of_materials("
                     "  product_id, name, output_quantity_milli, is_default, is_active, comment"
                     ") VALUES ("
                     "  :pid, :name, :qty, :is_def, :is_act, :comment"
                     ")"));
  if (!status) {
    return Result<BomId>::fail(status.error());
  }
  query.bind(QStringLiteral(":pid"), static_cast<qlonglong>(bom.productId.value));
  query.bind(QStringLiteral(":name"), bom.name.trimmed());
  query.bind(QStringLiteral(":qty"), static_cast<qlonglong>(bom.outputQuantity.milli()));
  query.bind(QStringLiteral(":is_def"), bom.isDefault ? 1 : 0);
  query.bind(QStringLiteral(":is_act"), bom.isActive ? 1 : 0);
  query.bind(QStringLiteral(":comment"),
             bom.comment.trimmed().isEmpty() ? QString() : bom.comment.trimmed());

  status = query.exec();
  if (!status) {
    return Result<BomId>::fail(status.error());
  }

  const BomId newId{query.lastInsertId()};

  int lineIdx = 1;
  for (const BomLine & line : bom.lines) {
    sql::Query lQuery(db_);
    status = lQuery.prepare(QStringLiteral(
        "INSERT INTO bom_lines(bom_id, line_no, component_id, quantity_milli, comment) "
        "VALUES(:bid, :no, :cid, :qty, :comment)"));
    if (!status) {
      return Result<BomId>::fail(status.error());
    }
    lQuery.bind(QStringLiteral(":bid"), static_cast<qlonglong>(newId.value));
    lQuery.bind(QStringLiteral(":no"), lineIdx++);
    lQuery.bind(QStringLiteral(":cid"), static_cast<qlonglong>(line.componentId.value));
    lQuery.bind(QStringLiteral(":qty"), static_cast<qlonglong>(line.quantity.milli()));
    lQuery.bind(QStringLiteral(":comment"),
                line.comment.trimmed().isEmpty() ? QString() : line.comment.trimmed());
    status = lQuery.exec();
    if (!status) {
      return Result<BomId>::fail(status.error());
    }
  }

  status = txn.commit();
  if (!status) {
    return Result<BomId>::fail(status.error());
  }
  return Result<BomId>::ok(newId);
}

Status BomRepository::update(const BillOfMaterials & bom)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  if (!bom.id) {
    return Status::fail(Error::invalidArgument(QStringLiteral("порожній id специфікації")));
  }
  Status status = validateBom(bom);
  if (!status) {
    return status;
  }

  Transaction txn(db_);
  if (!txn.started()) {
    return txn.startError();
  }

  if (bom.isDefault) {
    sql::Query clearDef(db_);
    status = clearDef.prepare(QStringLiteral(
        "UPDATE bill_of_materials SET is_default = 0 WHERE product_id = :pid AND id <> :id"));
    if (!status) {
      return status;
    }
    clearDef.bind(QStringLiteral(":pid"), static_cast<qlonglong>(bom.productId.value));
    clearDef.bind(QStringLiteral(":id"), static_cast<qlonglong>(bom.id.value));
    status = clearDef.exec();
    if (!status) {
      return status;
    }
  }

  sql::Query query(db_);
  status = query.prepare(
      QStringLiteral("UPDATE bill_of_materials SET "
                     "  product_id = :pid, name = :name, output_quantity_milli = :qty, "
                     "  is_default = :is_def, is_active = :is_act, comment = :comment "
                     "WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(bom.id.value));
  query.bind(QStringLiteral(":pid"), static_cast<qlonglong>(bom.productId.value));
  query.bind(QStringLiteral(":name"), bom.name.trimmed());
  query.bind(QStringLiteral(":qty"), static_cast<qlonglong>(bom.outputQuantity.milli()));
  query.bind(QStringLiteral(":is_def"), bom.isDefault ? 1 : 0);
  query.bind(QStringLiteral(":is_act"), bom.isActive ? 1 : 0);
  query.bind(QStringLiteral(":comment"),
             bom.comment.trimmed().isEmpty() ? QString() : bom.comment.trimmed());

  status = query.exec();
  if (!status) {
    return status;
  }

  sql::Query delLines(db_);
  status = delLines.prepare(QStringLiteral("DELETE FROM bom_lines WHERE bom_id = :bid"));
  if (!status) {
    return status;
  }
  delLines.bind(QStringLiteral(":bid"), static_cast<qlonglong>(bom.id.value));
  status = delLines.exec();
  if (!status) {
    return status;
  }

  int lineIdx = 1;
  for (const BomLine & line : bom.lines) {
    sql::Query lQuery(db_);
    status = lQuery.prepare(QStringLiteral(
        "INSERT INTO bom_lines(bom_id, line_no, component_id, quantity_milli, comment) "
        "VALUES(:bid, :no, :cid, :qty, :comment)"));
    if (!status) {
      return status;
    }
    lQuery.bind(QStringLiteral(":bid"), static_cast<qlonglong>(bom.id.value));
    lQuery.bind(QStringLiteral(":no"), lineIdx++);
    lQuery.bind(QStringLiteral(":cid"), static_cast<qlonglong>(line.componentId.value));
    lQuery.bind(QStringLiteral(":qty"), static_cast<qlonglong>(line.quantity.milli()));
    lQuery.bind(QStringLiteral(":comment"),
                line.comment.trimmed().isEmpty() ? QString() : line.comment.trimmed());
    status = lQuery.exec();
    if (!status) {
      return status;
    }
  }

  return txn.commit();
}

Status BomRepository::remove(BomId id)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  if (!id) {
    return Status::fail(Error::invalidArgument(QStringLiteral("порожній id специфікації")));
  }
  sql::Query query(db_);
  Status status = query.prepare(QStringLiteral("DELETE FROM bill_of_materials WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  return query.exec();
}

} // namespace fin
