#include "fin/db/HrOrderRepository.h"

#include "fin/auth/Access.h"
#include "fin/core/Sql.h"
#include "fin/core/Transaction.h"

#include <QDate>
#include <QVariant>

namespace fin {
namespace {

QString iso(QDate date)
{
  return date.toString(Qt::ISODate);
}

Status validateOrder(const HrOrder & order)
{
  if (!order.documentId) {
    return Status::fail(
        Error::invalidArgument(QStringLiteral("кадровий наказ: порожній documentId")));
  }
  if (!order.employeeId) {
    return Status::fail(
        Error::invalidArgument(QStringLiteral("кадровий наказ: порожній employeeId")));
  }
  if (!order.effectiveDate.isValid()) {
    return Status::fail(
        Error::validation(QStringLiteral("кадровий наказ: дата набрання чинності обов'язкова")));
  }
  if (order.orderType == HrOrderType::Transfer && order.newSalary.isPositive()) {
    // оклад вже перевіряється у validateEmployee, але тут перевіряємо додатковий guard
    if (order.newSalary.cents() <= 0) {
      return Status::fail(Error::validation(
          QStringLiteral("кадровий наказ: новий оклад має бути більшим за нуль")));
    }
  }
  return Status::ok();
}

} // namespace

HrOrderRepository::HrOrderRepository(QSqlDatabase db) : db_(std::move(db)) {}

Result<HrOrderId> HrOrderRepository::insert(const HrOrder & order)
{
  Status access = Access::requireEditDirectories(db_);
  if (!access) {
    return Result<HrOrderId>::fail(access.error());
  }
  const Status valid = validateOrder(order);
  if (!valid) {
    return Result<HrOrderId>::fail(valid.error());
  }

  Transaction txn(db_);
  if (!txn.started()) {
    return Result<HrOrderId>::fail(txn.startError().error());
  }

  // 1. Зберігаємо сам запис наказу
  sql::Query q(db_);
  Status status = q.prepare(
      QStringLiteral("INSERT INTO hr_orders(document_id, employee_id, order_type, effective_date, "
                     "new_position, new_department, new_salary_cents, dismiss_reason) "
                     "VALUES(:doc, :emp, :type, :date, :pos, :dept, :sal, :reason)"));
  if (!status) {
    return Result<HrOrderId>::fail(status.error());
  }
  q.bind(QStringLiteral(":doc"), static_cast<qlonglong>(order.documentId.value));
  q.bind(QStringLiteral(":emp"), static_cast<qlonglong>(order.employeeId.value));
  q.bind(QStringLiteral(":type"), hrOrderTypeString(order.orderType));
  q.bind(QStringLiteral(":date"), iso(order.effectiveDate));
  q.bind(QStringLiteral(":pos"), order.newPosition.trimmed().isEmpty()
                                     ? QVariant(QMetaType(QMetaType::QString))
                                     : QVariant(order.newPosition.trimmed()));
  q.bind(QStringLiteral(":dept"), order.newDepartment.trimmed().isEmpty()
                                      ? QVariant(QMetaType(QMetaType::QString))
                                      : QVariant(order.newDepartment.trimmed()));
  q.bind(QStringLiteral(":sal"), order.newSalary.isPositive()
                                     ? QVariant(static_cast<qlonglong>(order.newSalary.cents()))
                                     : QVariant(QMetaType(QMetaType::LongLong)));
  q.bind(QStringLiteral(":reason"), order.dismissReason.trimmed().isEmpty()
                                        ? QVariant(QMetaType(QMetaType::QString))
                                        : QVariant(order.dismissReason.trimmed()));
  status = q.exec();
  if (!status) {
    if (status.message().contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive)) {
      return Result<HrOrderId>::fail(Error::conflict(
          QStringLiteral("кадровий наказ цього типу вже існує на цю дату для працівника")));
    }
    return Result<HrOrderId>::fail(status.error());
  }
  const HrOrderId newId{q.lastInsertId()};

  // 2. Застосовуємо зміни до картки Employee відповідно до типу наказу
  if (order.orderType == HrOrderType::Hire) {
    // При прийомі: оновлюємо посаду/підрозділ якщо вказано
    if (!order.newPosition.trimmed().isEmpty() || !order.newDepartment.trimmed().isEmpty()) {
      sql::Query upd(db_);
      status = upd.prepare(QStringLiteral("UPDATE employees SET "
                                          "position = COALESCE(NULLIF(:pos,''), position), "
                                          "department = COALESCE(NULLIF(:dept,''), department) "
                                          "WHERE id = :id"));
      if (!status) {
        return Result<HrOrderId>::fail(status.error());
      }
      upd.bind(QStringLiteral(":pos"), order.newPosition.trimmed());
      upd.bind(QStringLiteral(":dept"), order.newDepartment.trimmed());
      upd.bind(QStringLiteral(":id"), static_cast<qlonglong>(order.employeeId.value));
      status = upd.exec();
      if (!status) {
        return Result<HrOrderId>::fail(status.error());
      }
    }
  } else if (order.orderType == HrOrderType::Transfer) {
    // При переведенні: оновлюємо ті поля, що ненульові
    sql::Query upd(db_);
    status = upd.prepare(
        QStringLiteral("UPDATE employees SET "
                       "position   = CASE WHEN :pos  != '' THEN :pos  ELSE position   END, "
                       "department = CASE WHEN :dept != '' THEN :dept ELSE department END, "
                       "salary_cents = CASE WHEN :sal > 0  THEN :sal  ELSE salary_cents END "
                       "WHERE id = :id"));
    if (!status) {
      return Result<HrOrderId>::fail(status.error());
    }
    upd.bind(QStringLiteral(":pos"), order.newPosition.trimmed());
    upd.bind(QStringLiteral(":dept"), order.newDepartment.trimmed());
    upd.bind(QStringLiteral(":sal"), order.newSalary.isPositive()
                                         ? static_cast<qlonglong>(order.newSalary.cents())
                                         : qlonglong(0));
    upd.bind(QStringLiteral(":id"), static_cast<qlonglong>(order.employeeId.value));
    status = upd.exec();
    if (!status) {
      return Result<HrOrderId>::fail(status.error());
    }
  } else if (order.orderType == HrOrderType::Dismiss) {
    // При звільненні: фіксуємо дату та деактивуємо картку
    sql::Query upd(db_);
    status = upd.prepare(
        QStringLiteral("UPDATE employees SET is_active = 0, fired_on = :fired WHERE id = :id"));
    if (!status) {
      return Result<HrOrderId>::fail(status.error());
    }
    upd.bind(QStringLiteral(":fired"), iso(order.effectiveDate));
    upd.bind(QStringLiteral(":id"), static_cast<qlonglong>(order.employeeId.value));
    status = upd.exec();
    if (!status) {
      return Result<HrOrderId>::fail(status.error());
    }
  }

  status = txn.commit();
  if (!status) {
    return Result<HrOrderId>::fail(status.error());
  }
  return Result<HrOrderId>::ok(newId);
}

Result<HrOrder> HrOrderRepository::findById(HrOrderId id) const
{
  if (!id) {
    return Result<HrOrder>::fail(
        Error::invalidArgument(QStringLiteral("порожній id кадрового наказу")));
  }
  sql::Query q(db_);
  Status status =
      q.prepare(QStringLiteral("SELECT id, document_id, employee_id, order_type, effective_date, "
                               "new_position, new_department, new_salary_cents, dismiss_reason "
                               "FROM hr_orders WHERE id = :id"));
  if (!status) {
    return Result<HrOrder>::fail(status.error());
  }
  q.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = q.exec();
  if (!status) {
    return Result<HrOrder>::fail(status.error());
  }
  if (!q.next()) {
    return Result<HrOrder>::fail(Error::notFound(QStringLiteral("кадровий наказ не знайдено")));
  }
  return fromQuery(q);
}

Result<HrOrder> HrOrderRepository::findByDocument(DocumentId docId) const
{
  if (!docId) {
    return Result<HrOrder>::fail(Error::invalidArgument(QStringLiteral("порожній documentId")));
  }
  sql::Query q(db_);
  Status status =
      q.prepare(QStringLiteral("SELECT id, document_id, employee_id, order_type, effective_date, "
                               "new_position, new_department, new_salary_cents, dismiss_reason "
                               "FROM hr_orders WHERE document_id = :doc"));
  if (!status) {
    return Result<HrOrder>::fail(status.error());
  }
  q.bind(QStringLiteral(":doc"), static_cast<qlonglong>(docId.value));
  status = q.exec();
  if (!status) {
    return Result<HrOrder>::fail(status.error());
  }
  if (!q.next()) {
    return Result<HrOrder>::fail(
        Error::notFound(QStringLiteral("кадровий наказ не знайдено за документом")));
  }
  return fromQuery(q);
}

Result<std::vector<HrOrder>> HrOrderRepository::listByEmployee(EmployeeId empId) const
{
  if (!empId) {
    return Result<std::vector<HrOrder>>::fail(
        Error::invalidArgument(QStringLiteral("порожній employeeId")));
  }
  sql::Query q(db_);
  Status status =
      q.prepare(QStringLiteral("SELECT id, document_id, employee_id, order_type, effective_date, "
                               "new_position, new_department, new_salary_cents, dismiss_reason "
                               "FROM hr_orders WHERE employee_id = :emp ORDER BY effective_date"));
  if (!status) {
    return Result<std::vector<HrOrder>>::fail(status.error());
  }
  q.bind(QStringLiteral(":emp"), static_cast<qlonglong>(empId.value));
  status = q.exec();
  if (!status) {
    return Result<std::vector<HrOrder>>::fail(status.error());
  }
  std::vector<HrOrder> result;
  while (q.next()) {
    const Result<HrOrder> row = fromQuery(q);
    if (!row) {
      return Result<std::vector<HrOrder>>::fail(row.error());
    }
    result.push_back(row.value());
  }
  return Result<std::vector<HrOrder>>::ok(std::move(result));
}

Result<HrOrder> HrOrderRepository::fromQuery(sql::Query & q) const
{
  const QString typeStr = q.value(3).toString();
  const auto maybeType = hrOrderTypeFromString(typeStr);
  if (!maybeType) {
    return Result<HrOrder>::fail(
        Error::database(QStringLiteral("невідомий тип кадрового наказу: %1").arg(typeStr)));
  }
  HrOrder order;
  order.id = HrOrderId{q.value(0).toLongLong()};
  order.documentId = DocumentId{q.value(1).toLongLong()};
  order.employeeId = EmployeeId{q.value(2).toLongLong()};
  order.orderType = maybeType.value();
  order.effectiveDate = QDate::fromString(q.value(4).toString(), Qt::ISODate);
  order.newPosition = q.value(5).toString();
  order.newDepartment = q.value(6).toString();
  const QVariant salVar = q.value(7);
  if (!salVar.isNull()) {
    order.newSalary = Money::fromCents(salVar.toLongLong());
  }
  order.dismissReason = q.value(8).toString();
  return Result<HrOrder>::ok(order);
}

} // namespace fin
