#include "fin/db/PeriodRepository.h"

#include "fin/auth/Access.h"
#include "fin/core/Sql.h"
#include "fin/core/Transaction.h"
#include "fin/db/AuditLog.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/MonthClose.h"

#include <QVariant>

namespace fin {
namespace {

QString iso(QDate date)
{
  return date.toString(Qt::ISODate);
}

QString monthLabel(QDate lastDay)
{
  return lastDay.toString(QStringLiteral("MM.yyyy"));
}

struct PeriodRow
{
  QDate closedThrough;
  QDate firstClosedFrom;
};

Result<PeriodRow> loadPeriod(QSqlDatabase db)
{
  sql::Query query(db);
  Status status = query.exec(QStringLiteral(
      "SELECT closed_through, first_closed_from FROM accounting_period WHERE id = 1"));
  if (!status) {
    return Result<PeriodRow>::fail(status.error());
  }
  if (!query.next()) {
    return Result<PeriodRow>::fail(
        Error::database(QStringLiteral("немає запису про закритий період")));
  }
  PeriodRow row;
  const QString through = query.value(0).toString();
  if (!through.trimmed().isEmpty()) {
    row.closedThrough = QDate::fromString(through, Qt::ISODate);
    if (!row.closedThrough.isValid()) {
      return Result<PeriodRow>::fail(
          Error::database(QStringLiteral("пошкоджена дата закритого періоду")));
    }
  }
  const QString from = query.value(1).toString();
  if (!from.trimmed().isEmpty()) {
    row.firstClosedFrom = QDate::fromString(from, Qt::ISODate);
    if (!row.firstClosedFrom.isValid()) {
      return Result<PeriodRow>::fail(
          Error::database(QStringLiteral("пошкоджена дата початку закритого періоду")));
    }
  }
  return Result<PeriodRow>::ok(row);
}

} // namespace

PeriodRepository::PeriodRepository(QSqlDatabase db) : db_(std::move(db)) {}

Result<QDate> PeriodRepository::lastDayOfMonth(int year, int month) const
{
  if (year < 1991 || year > 2100 || month < 1 || month > 12) {
    return Result<QDate>::fail(Error::validation(QStringLiteral("некоректний місяць закриття")));
  }
  const QDate last = QDate(year, month, 1).addMonths(1).addDays(-1);
  if (!last.isValid()) {
    return Result<QDate>::fail(Error::validation(QStringLiteral("некоректний місяць закриття")));
  }
  return Result<QDate>::ok(last);
}

Result<QDate> PeriodRepository::closedThrough() const
{
  const Result<PeriodRow> row = loadPeriod(db_);
  if (!row) {
    return Result<QDate>::fail(row.error());
  }
  return Result<QDate>::ok(row.value().closedThrough);
}

Status PeriodRepository::ensureDateWritable(QDate date) const
{
  if (!date.isValid() || date.year() < 1991 || date.year() > 2100) {
    return Status::fail(Error::validation(QStringLiteral("некоректна дата документа")));
  }
  const Result<QDate> closed = closedThrough();
  if (!closed) {
    return closed.asStatus();
  }
  if (closed.value().isValid() && date <= closed.value()) {
    return Status::fail(
        Error::periodClosed(QStringLiteral("період закрито по %1")
                                .arg(closed.value().toString(QStringLiteral("dd.MM.yyyy")))));
  }
  return Status::ok();
}

Status PeriodRepository::checkCloseThroughMonth(int year, int month) const
{
  const Result<QDate> requested = lastDayOfMonth(year, month);
  if (!requested) {
    return requested.asStatus();
  }
  const QDate lastDay = requested.value();
  const Result<PeriodRow> current = loadPeriod(db_);
  if (!current) {
    return current.asStatus();
  }
  if (current.value().closedThrough.isValid()) {
    if (lastDay <= current.value().closedThrough) {
      return Status::fail(
          Error::conflict(QStringLiteral("місяць %1 уже закрито").arg(monthLabel(lastDay))));
    }
    const QDate nextStart = current.value().closedThrough.addDays(1);
    const QDate expectedLast =
        QDate(nextStart.year(), nextStart.month(), 1).addMonths(1).addDays(-1);
    if (lastDay != expectedLast) {
      return Status::fail(
          Error::validation(QStringLiteral("закривайте місяці послідовно; наступний — %1")
                                .arg(monthLabel(expectedLast))));
    }
  }

  sql::Query drafts(db_);
  Status status = drafts.prepare(
      QStringLiteral("SELECT id FROM documents WHERE date <= :through AND posted = 0 "
                     "AND type NOT IN (%1) LIMIT 1")
          .arg(sqlNonLedgerDocumentTypes()));
  if (!status) {
    return status;
  }
  drafts.bind(QStringLiteral(":through"), iso(lastDay));
  status = drafts.exec();
  if (!status) {
    return status;
  }
  if (drafts.next()) {
    return Status::fail(
        Error::conflict(QStringLiteral("не можна закрити період: є непроведені документи по %1")
                            .arg(lastDay.toString(QStringLiteral("dd.MM.yyyy")))));
  }
  return requireIncomeExpenseClosed(db_, lastDay);
}

Status PeriodRepository::closeThroughMonth(int year, int month)
{
  Status access = Access::requireClosePeriod(db_);
  if (!access) {
    return access;
  }
  const Status allowed = checkCloseThroughMonth(year, month);
  if (!allowed) {
    return allowed;
  }
  const Result<QDate> requested = lastDayOfMonth(year, month);
  if (!requested) {
    return requested.asStatus();
  }
  const QDate lastDay = requested.value();
  const QDate firstDay(year, month, 1);

  Transaction txn(db_);
  if (!txn.started()) {
    return txn.startError();
  }

  const Result<PeriodRow> current = loadPeriod(db_);
  if (!current) {
    return current.asStatus();
  }
  QDate firstClosedFrom = current.value().firstClosedFrom;
  if (!current.value().closedThrough.isValid()) {
    firstClosedFrom = firstDay;
  }

  sql::Query update(db_);
  Status status = update.prepare(
      QStringLiteral("UPDATE accounting_period SET closed_through = :through, "
                     "first_closed_from = :from, closed_at = datetime('now') WHERE id = 1"));
  if (!status) {
    return status;
  }
  update.bind(QStringLiteral(":through"), iso(lastDay));
  update.bind(QStringLiteral(":from"), iso(firstClosedFrom));
  status = update.exec();
  if (!status) {
    return status;
  }
  status =
      appendAudit(db_, QStringLiteral("close"), QStringLiteral("period"), 0, monthLabel(lastDay));
  if (!status) {
    return status;
  }
  return txn.commit();
}

Status PeriodRepository::reopenLastMonth()
{
  Status access = Access::requireClosePeriod(db_);
  if (!access) {
    return access;
  }
  Transaction txn(db_);
  if (!txn.started()) {
    return txn.startError();
  }

  const Result<PeriodRow> current = loadPeriod(db_);
  if (!current) {
    return current.asStatus();
  }
  if (!current.value().closedThrough.isValid()) {
    return Status::fail(Error::validation(QStringLiteral("немає закритого періоду")));
  }

  const QDate monthStart(current.value().closedThrough.year(),
                         current.value().closedThrough.month(), 1);
  sql::Query update(db_);
  Status status = update.prepare(
      QStringLiteral("UPDATE accounting_period SET closed_through = :through, "
                     "first_closed_from = :from, closed_at = datetime('now') WHERE id = 1"));
  if (!status) {
    return status;
  }
  if (!current.value().firstClosedFrom.isValid() || monthStart <= current.value().firstClosedFrom) {
    update.bind(QStringLiteral(":through"), QVariant());
    update.bind(QStringLiteral(":from"), QVariant());
  } else {
    update.bind(QStringLiteral(":through"), iso(monthStart.addDays(-1)));
    update.bind(QStringLiteral(":from"), iso(current.value().firstClosedFrom));
  }
  status = update.exec();
  if (!status) {
    return status;
  }
  status = appendAudit(db_, QStringLiteral("reopen"), QStringLiteral("period"), 0,
                       monthLabel(current.value().closedThrough));
  if (!status) {
    return status;
  }
  return txn.commit();
}

} // namespace fin
