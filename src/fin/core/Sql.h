#pragma once

// Обгортки над QSqlQuery з Result/Status.

#include "fin/core/Result.h"

#include <cstdint>

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariant>

namespace fin::sql {

// Формує статус з помилкою з об'єкта QSqlQuery
inline Status fromQuery(const QSqlQuery & query, const QString & context)
{
  const QSqlError error = query.lastError();
  QString text = error.text().trimmed();
  if (text.isEmpty()) {
    text = QStringLiteral("unknown SQL error");
  }
  if (!context.isEmpty()) {
    text = context + QStringLiteral(": ") + text;
  }
  return Status::fail(Error::database(text));
}

// Формує статус з помилкою з об'єкта QSqlDatabase
inline Status fromDatabase(const QSqlDatabase & db, const QString & context)
{
  QString text = db.lastError().text().trimmed();
  if (text.isEmpty()) {
    text = QStringLiteral("unknown SQL error");
  }
  if (!context.isEmpty()) {
    text = context + QStringLiteral(": ") + text;
  }
  return Status::fail(Error::database(text));
}

// Обгортка над QSqlQuery з поверненням fin::Status замість булевих прапорців
class Query
{
 public:
  explicit Query(const QSqlDatabase & db) : query_(db) {}

  Status prepare(const QString & sql)
  {
    if (!query_.prepare(sql)) {
      return fromQuery(query_, QStringLiteral("prepare"));
    }
    return Status::ok();
  }

  void bind(const QString & name, const QVariant & value) { query_.bindValue(name, value); }

  Status exec()
  {
    if (!query_.exec()) {
      return fromQuery(query_, QStringLiteral("exec"));
    }
    return Status::ok();
  }

  Status exec(const QString & sql)
  {
    if (!query_.exec(sql)) {
      return fromQuery(query_, QStringLiteral("exec"));
    }
    return Status::ok();
  }

  bool next() { return query_.next(); }

  QVariant value(int index) const { return query_.value(index); }
  QVariant value(const QString & name) const { return query_.value(name); }

  QSqlQuery & raw() { return query_; }

  std::int64_t lastInsertId() const { return query_.lastInsertId().toLongLong(); }

 private:
  QSqlQuery query_;
};

} // namespace fin::sql
