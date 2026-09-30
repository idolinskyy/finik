#pragma once

// Модель даних для довідника Працівників.
// Qt::UserRole повертає EmployeeId::value для навігації.

#include "fin/domain/Employee.h"

#include <QAbstractTableModel>
#include <vector>

namespace fin {

class EmployeeModel : public QAbstractTableModel
{
  Q_OBJECT

 public:
  static constexpr int kColumns = 4;

  explicit EmployeeModel(QObject * parent = nullptr);

  void setEmployees(std::vector<Employee> list);

  EmployeeId idAt(int row) const;

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  int columnCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;

 private:
  std::vector<Employee> list_;
};

} // namespace fin
