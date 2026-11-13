#include "fin/db/SubcontoRepository.h"

#include "fin/auth/Access.h"
#include "fin/core/Sql.h"
#include "fin/core/Transaction.h"

#include <QRegularExpression>
#include <QSet>

namespace fin {

SubcontoRepository::SubcontoRepository(QSqlDatabase db) : db_(db) {}

Result<std::vector<SubcontoKind>> SubcontoRepository::listKinds() const
{
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("SELECT id, name_ua, source_table, id_column, name_column "
                                   "FROM subconto_kinds ORDER BY name_ua"));
  if (!status) {
    return Result<std::vector<SubcontoKind>>::fail(status.error());
  }
  status = query.exec();
  if (!status) {
    return Result<std::vector<SubcontoKind>>::fail(status.error());
  }

  std::vector<SubcontoKind> result;
  while (query.next()) {
    SubcontoKind k;
    k.id = query.value(0).toString();
    k.nameUa = query.value(1).toString();
    k.sourceTable = query.value(2).toString();
    k.idColumn = query.value(3).toString();
    k.nameColumn = query.value(4).toString();
    result.push_back(std::move(k));
  }
  return Result<std::vector<SubcontoKind>>::ok(std::move(result));
}

Result<SubcontoKind> SubcontoRepository::findKind(const QString & kindId) const
{
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("SELECT id, name_ua, source_table, id_column, name_column "
                                   "FROM subconto_kinds WHERE id = :id"));
  if (!status) {
    return Result<SubcontoKind>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), kindId);
  status = query.exec();
  if (!status) {
    return Result<SubcontoKind>::fail(status.error());
  }
  if (!query.next()) {
    return Result<SubcontoKind>::fail(
        Error::notFound(QStringLiteral("вид субконто не знайдено: %1").arg(kindId)));
  }

  SubcontoKind k;
  k.id = query.value(0).toString();
  k.nameUa = query.value(1).toString();
  k.sourceTable = query.value(2).toString();
  k.idColumn = query.value(3).toString();
  k.nameColumn = query.value(4).toString();
  return Result<SubcontoKind>::ok(std::move(k));
}

Result<std::vector<AccountSubconto>> SubcontoRepository::listForAccount(AccountId accountId) const
{
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("SELECT account_id, line_no, subconto_kind_id, is_required "
                                   "FROM account_subconto WHERE account_id = :acc "
                                   "ORDER BY line_no ASC"));
  if (!status) {
    return Result<std::vector<AccountSubconto>>::fail(status.error());
  }
  query.bind(QStringLiteral(":acc"), static_cast<qlonglong>(accountId.value));
  status = query.exec();
  if (!status) {
    return Result<std::vector<AccountSubconto>>::fail(status.error());
  }

  std::vector<AccountSubconto> list;
  while (query.next()) {
    AccountSubconto s;
    s.accountId = AccountId{query.value(0).toLongLong()};
    s.lineNo = query.value(1).toInt();
    s.subcontoKindId = query.value(2).toString();
    s.isRequired = query.value(3).toInt() == 1;
    list.push_back(std::move(s));
  }
  return Result<std::vector<AccountSubconto>>::ok(std::move(list));
}

Status SubcontoRepository::setForAccount(AccountId accountId,
                                        const std::vector<AccountSubconto> & settings)
{
  const Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  if (!accountId) {
    return Status::fail(Error::invalidArgument(QStringLiteral("порожній рахунок для субконто")));
  }
  if (settings.size() > 3) {
    return Status::fail(
        Error::invalidArgument(QStringLiteral("рахунок не може містити більше 3 видів субконто")));
  }

  QSet<int> seenLines;
  QSet<QString> seenKinds;
  for (const auto & item : settings) {
    if (item.lineNo < 1 || item.lineNo > 3) {
      return Status::fail(
          Error::invalidArgument(QStringLiteral("номер розрізу субконто має бути від 1 до 3")));
    }
    if (seenLines.contains(item.lineNo)) {
      return Status::fail(Error::invalidArgument(
          QStringLiteral("дублювання номера розрізу субконто: %1").arg(item.lineNo)));
    }
    seenLines.insert(item.lineNo);
    if (seenKinds.contains(item.subcontoKindId)) {
      return Status::fail(Error::invalidArgument(
          QStringLiteral("вид субконто '%1' призначено двічі").arg(item.subcontoKindId)));
    }
    seenKinds.insert(item.subcontoKindId);
    const Result<SubcontoKind> kind = findKind(item.subcontoKindId);
    if (!kind) {
      return kind.asStatus();
    }
  }

  Transaction txn(db_);
  if (!txn.started()) {
    return txn.startError();
  }

  sql::Query del(db_);
  Status status =
      del.prepare(QStringLiteral("DELETE FROM account_subconto WHERE account_id = :acc"));
  if (!status) {
    return status;
  }
  del.bind(QStringLiteral(":acc"), static_cast<qlonglong>(accountId.value));
  status = del.exec();
  if (!status) {
    return status;
  }

  for (const auto & item : settings) {
    sql::Query insert(db_);
    status = insert.prepare(QStringLiteral(
        "INSERT INTO account_subconto(account_id, line_no, subconto_kind_id, is_required) "
        "VALUES(:acc, :line, :kind, :req)"));
    if (!status) {
      return status;
    }
    insert.bind(QStringLiteral(":acc"), static_cast<qlonglong>(accountId.value));
    insert.bind(QStringLiteral(":line"), item.lineNo);
    insert.bind(QStringLiteral(":kind"), item.subcontoKindId);
    insert.bind(QStringLiteral(":req"), item.isRequired ? 1 : 0);
    status = insert.exec();
    if (!status) {
      return status;
    }
  }

  return txn.commit();
}

Result<QString> SubcontoRepository::resolveName(const QString & kindId, std::int64_t entityId) const
{
  if (kindId.isEmpty() || entityId <= 0) {
    return Result<QString>::ok(QString());
  }

  auto kindRes = findKind(kindId);
  if (!kindRes) {
    return Result<QString>::fail(kindRes.error());
  }
  const SubcontoKind & k = kindRes.value();

  // Запит з валідованими назвами колонок та таблиць з subconto_kinds
  const QString queryStr = QStringLiteral("SELECT %1 FROM %2 WHERE %3 = :id")
                               .arg(k.nameColumn, k.sourceTable, k.idColumn);

  sql::Query query(db_);
  Status status = query.prepare(queryStr);
  if (!status) {
    return Result<QString>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(entityId));
  status = query.exec();
  if (!status) {
    return Result<QString>::fail(status.error());
  }
  if (query.next()) {
    return Result<QString>::ok(query.value(0).toString());
  }
  return Result<QString>::ok(QStringLiteral("<не знайдено #%1>").arg(entityId));
}

Status SubcontoRepository::resolveNames(SubcontoSet & set) const
{
  for (int i = 1; i <= 3; ++i) {
    SubcontoValue & val = set.at(i);
    if (!val.empty() && val.name.isEmpty()) {
      auto nameRes = resolveName(val.kindId, val.entityId);
      if (nameRes) {
        val.name = nameRes.value();
      }
    }
  }
  return Status::ok();
}

namespace {

SubcontoItem itemFromQuery(sql::Query & query)
{
  SubcontoItem item;
  item.id = query.value(0).toLongLong();
  item.kindId = query.value(1).toString();
  item.code = query.value(2).toString();
  item.name = query.value(3).toString();
  item.isActive = query.value(4).toInt() == 1;
  return item;
}

bool isCustomKindSlug(const QString & id)
{
  static const QRegularExpression re(QStringLiteral("^[a-z][a-z0-9_]{1,40}$"));
  return re.match(id).hasMatch();
}

} // namespace

Result<std::vector<SubcontoItem>> SubcontoRepository::listItems(const QString & kindId,
                                                                bool activeOnly) const
{
  if (kindId.trimmed().isEmpty()) {
    return Result<std::vector<SubcontoItem>>::fail(
        Error::invalidArgument(QStringLiteral("порожній вид субконто")));
  }
  sql::Query query(db_);
  QString sql = QStringLiteral("SELECT id, kind_id, code, name, is_active FROM subconto_items "
                               "WHERE kind_id = :kind");
  if (activeOnly) {
    sql += QStringLiteral(" AND is_active = 1");
  }
  sql += QStringLiteral(" ORDER BY code COLLATE NOCASE, name COLLATE NOCASE");
  Status status = query.prepare(sql);
  if (!status) {
    return Result<std::vector<SubcontoItem>>::fail(status.error());
  }
  query.bind(QStringLiteral(":kind"), kindId);
  status = query.exec();
  if (!status) {
    return Result<std::vector<SubcontoItem>>::fail(status.error());
  }
  std::vector<SubcontoItem> rows;
  while (query.next()) {
    rows.push_back(itemFromQuery(query));
  }
  return Result<std::vector<SubcontoItem>>::ok(std::move(rows));
}

Result<SubcontoItem> SubcontoRepository::findItem(std::int64_t id) const
{
  if (id <= 0) {
    return Result<SubcontoItem>::fail(Error::invalidArgument(QStringLiteral("порожній id статті")));
  }
  sql::Query query(db_);
  Status status = query.prepare(QStringLiteral(
      "SELECT id, kind_id, code, name, is_active FROM subconto_items WHERE id = :id"));
  if (!status) {
    return Result<SubcontoItem>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id));
  status = query.exec();
  if (!status) {
    return Result<SubcontoItem>::fail(status.error());
  }
  if (!query.next()) {
    return Result<SubcontoItem>::fail(Error::notFound(QStringLiteral("статтю субконто не знайдено")));
  }
  return Result<SubcontoItem>::ok(itemFromQuery(query));
}

Result<std::int64_t> SubcontoRepository::insertItem(const SubcontoItem & item)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return Result<std::int64_t>::fail(access.error());
  }
  const Result<SubcontoKind> kind = findKind(item.kindId);
  if (!kind) {
    return Result<std::int64_t>::fail(kind.error());
  }
  if (!isCustomSubcontoSource(kind.value().sourceTable)) {
    return Result<std::int64_t>::fail(
        Error::validation(QStringLiteral("цей вид субконто не є довільним довідником")));
  }
  if (item.code.trimmed().isEmpty() || item.name.trimmed().isEmpty()) {
    return Result<std::int64_t>::fail(
        Error::validation(QStringLiteral("код і назва статті обов'язкові")));
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("INSERT INTO subconto_items(kind_id, code, name, is_active) "
                     "VALUES(:kind, :code, :name, :active)"));
  if (!status) {
    return Result<std::int64_t>::fail(status.error());
  }
  query.bind(QStringLiteral(":kind"), item.kindId);
  query.bind(QStringLiteral(":code"), item.code.trimmed());
  query.bind(QStringLiteral(":name"), item.name.trimmed());
  query.bind(QStringLiteral(":active"), item.isActive ? 1 : 0);
  status = query.exec();
  if (!status) {
    return Result<std::int64_t>::fail(status.error());
  }
  return Result<std::int64_t>::ok(query.lastInsertId());
}

Status SubcontoRepository::updateItem(const SubcontoItem & item)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  if (item.id <= 0) {
    return Status::fail(Error::invalidArgument(QStringLiteral("порожній id статті")));
  }
  if (item.code.trimmed().isEmpty() || item.name.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("код і назва статті обов'язкові")));
  }
  sql::Query query(db_);
  Status status = query.prepare(QStringLiteral(
      "UPDATE subconto_items SET code = :code, name = :name, is_active = :active WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":code"), item.code.trimmed());
  query.bind(QStringLiteral(":name"), item.name.trimmed());
  query.bind(QStringLiteral(":active"), item.isActive ? 1 : 0);
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(item.id));
  return query.exec();
}

Status SubcontoRepository::removeItem(std::int64_t id)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  sql::Query used(db_);
  Status status = used.prepare(QStringLiteral(
      "SELECT 1 FROM document_lines WHERE custom_subconto1_id = :id OR custom_subconto2_id = :id2 "
      "OR custom_subconto3_id = :id3 LIMIT 1"));
  if (!status) {
    return status;
  }
  used.bind(QStringLiteral(":id"), static_cast<qlonglong>(id));
  used.bind(QStringLiteral(":id2"), static_cast<qlonglong>(id));
  used.bind(QStringLiteral(":id3"), static_cast<qlonglong>(id));
  status = used.exec();
  if (!status) {
    return status;
  }
  if (used.next()) {
    return Status::fail(Error::conflict(QStringLiteral("статтю використано в документах")));
  }
  sql::Query del(db_);
  status = del.prepare(QStringLiteral("DELETE FROM subconto_items WHERE id = :id"));
  if (!status) {
    return status;
  }
  del.bind(QStringLiteral(":id"), static_cast<qlonglong>(id));
  return del.exec();
}

Status SubcontoRepository::insertCustomKind(const QString & id, const QString & nameUa)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return access;
  }
  const QString slug = id.trimmed().toLower();
  if (!isCustomKindSlug(slug)) {
    return Status::fail(Error::validation(
        QStringLiteral("ідентифікатор виду: латиниця, цифри й підкреслення, від 2 символів")));
  }
  if (nameUa.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("назва виду субконто порожня")));
  }
  if (findKind(slug)) {
    return Status::fail(Error::conflict(QStringLiteral("такий вид субконто вже є")));
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("INSERT INTO subconto_kinds(id, name_ua, source_table, id_column, name_column) "
                     "VALUES(:id, :name, 'subconto_items', 'id', 'name')"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":id"), slug);
  query.bind(QStringLiteral(":name"), nameUa.trimmed());
  return query.exec();
}

} // namespace fin
