#include "fin/db/AuditLog.h"

#include "fin/core/Sql.h"

#include <QDateTime>
#include <QVariant>

namespace fin {

Status appendAudit(QSqlDatabase db, const QString & action, const QString & entity,
                   std::int64_t entityId, const QString & details)
{
  sql::Query context(db);
  Status status = context.exec(QStringLiteral("SELECT user_id FROM app_context WHERE id = 1"));
  if (!status) {
    return status;
  }
  QVariant user;
  if (context.next()) {
    user = context.value(0);
  }
  sql::Query insert(db);
  status = insert.prepare(
      QStringLiteral("INSERT INTO audit_log(at, user_id, action, entity, entity_id, details) "
                     "VALUES(datetime('now'), :user, :action, :entity, :id, :details)"));
  if (!status) {
    return status;
  }
  insert.bind(QStringLiteral(":user"), user);
  insert.bind(QStringLiteral(":action"), action);
  insert.bind(QStringLiteral(":entity"), entity);
  if (entityId > 0) {
    insert.bind(QStringLiteral(":id"), static_cast<qlonglong>(entityId));
  } else {
    insert.bind(QStringLiteral(":id"), QVariant());
  }
  insert.bind(QStringLiteral(":details"), details);
  return insert.exec();
}

AuditLog::AuditLog(QSqlDatabase db) : db_(std::move(db)) {}

Result<std::vector<AuditEntry>> AuditLog::listRecent(int limit, const QString & action,
                                                     const QString & entity) const
{
  if (limit <= 0) {
    limit = 200;
  }
  sql::Query query(db_);
  QString sql =
      QStringLiteral("SELECT a.id, a.at, a.user_id, COALESCE(u.login, ''), a.action, a.entity, "
                     "COALESCE(a.entity_id, 0), COALESCE(a.details, '') "
                     "FROM audit_log a LEFT JOIN users u ON u.id = a.user_id WHERE 1 = 1");
  if (!action.isEmpty()) {
    sql += QStringLiteral(" AND a.action = :av");
  }
  if (!entity.isEmpty()) {
    sql += QStringLiteral(" AND a.entity = :ev");
  }
  sql += QStringLiteral(" ORDER BY a.id DESC LIMIT :limit");
  Status status = query.prepare(sql);
  if (!status) {
    return Result<std::vector<AuditEntry>>::fail(status.error());
  }
  if (!action.isEmpty()) {
    query.bind(QStringLiteral(":av"), action);
  }
  if (!entity.isEmpty()) {
    query.bind(QStringLiteral(":ev"), entity);
  }
  query.bind(QStringLiteral(":limit"), limit);
  status = query.exec();
  if (!status) {
    return Result<std::vector<AuditEntry>>::fail(status.error());
  }
  std::vector<AuditEntry> rows;
  while (query.next()) {
    AuditEntry row;
    row.id = query.value(0).toLongLong();
    row.at = QDateTime::fromString(query.value(1).toString(), Qt::ISODate);
    if (!row.at.isValid()) {
      row.at =
          QDateTime::fromString(query.value(1).toString(), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }
    row.userId = UserId{query.value(2).toLongLong()};
    row.userLogin = query.value(3).toString();
    row.action = query.value(4).toString();
    row.entity = query.value(5).toString();
    row.entityId = query.value(6).toLongLong();
    row.details = query.value(7).toString();
    rows.push_back(row);
  }
  return Result<std::vector<AuditEntry>>::ok(std::move(rows));
}

} // namespace fin
