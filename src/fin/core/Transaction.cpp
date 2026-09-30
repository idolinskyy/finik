#include "fin/core/Transaction.h"

#include "fin/core/Sql.h"

namespace fin {

Transaction::Transaction(QSqlDatabase db) : db_(std::move(db))
{
  sql::Query query(db_);
  const Status status = query.exec(QStringLiteral("BEGIN IMMEDIATE"));
  if (!status) {
    startError_ = status.error();
    started_ = false;
    return;
  }
  started_ = true;
}

Transaction::~Transaction()
{
  if (started_ && !committed_) {
    sql::Query query(db_);
    static_cast<void>(query.exec(QStringLiteral("ROLLBACK")));
  }
}

Status Transaction::commit()
{
  if (!started_) {
    return Status::fail(startError_);
  }
  sql::Query query(db_);
  const Status status = query.exec(QStringLiteral("COMMIT"));
  if (!status) {
    return status;
  }
  committed_ = true;
  return Status::ok();
}

} // namespace fin
