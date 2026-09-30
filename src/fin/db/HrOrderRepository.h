#pragma once

// Кадрові накази.

#include "fin/core/Result.h"
#include "fin/domain/Employee.h"

#include <vector>

#include <QSqlDatabase>

namespace fin {
namespace sql {
class Query;
}

class HrOrderRepository
{
 public:
  explicit HrOrderRepository(QSqlDatabase db);

  // Записує кадровий наказ і застосовує зміни до картки Employee:
  //   – Hire:     встановлює position, department (якщо вказано);
  //   – Transfer: оновлює position, department, salary за ненульовими полями;
  //   – Dismiss:  встановлює firedOn = order.effectiveDate, isActive = false.
  Result<HrOrderId> insert(const HrOrder & order);

  Result<HrOrder> findById(HrOrderId id) const;
  Result<HrOrder> findByDocument(DocumentId docId) const;
  Result<std::vector<HrOrder>> listByEmployee(EmployeeId empId) const;

 private:
  Result<HrOrder> fromQuery(class sql::Query & q) const;

  QSqlDatabase db_;
};

} // namespace fin
