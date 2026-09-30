#pragma once

#include "fin/core/Result.h"
#include "fin/domain/Employee.h"

#include <vector>

#include <QSqlDatabase>
#include <QString>

namespace fin {

class EmployeeRepository
{
 public:
  explicit EmployeeRepository(QSqlDatabase db);

  Result<Employee> findById(EmployeeId id) const;
  Result<Employee> findByTaxId(const QString & taxId) const;
  Result<std::vector<Employee>> listAll() const;
  Result<std::vector<Employee>> listActive() const;
  Result<EmployeeId> insert(const Employee & employee);
  Status update(const Employee & employee);
  Status remove(EmployeeId id);

 private:
  Result<std::vector<Employee>> listWhere(const QString & where) const;

  QSqlDatabase db_;
};

} // namespace fin
