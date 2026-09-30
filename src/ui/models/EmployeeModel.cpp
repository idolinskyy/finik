#include "ui/models/EmployeeModel.h"

namespace fin {

EmployeeModel::EmployeeModel(QObject * parent) : QAbstractTableModel(parent) {}

void EmployeeModel::setEmployees(std::vector<Employee> list)
{
  beginResetModel();
  list_ = std::move(list);
  endResetModel();
}

EmployeeId EmployeeModel::idAt(int row) const
{
  if (row < 0 || row >= static_cast<int>(list_.size()))
    return {};
  return list_[static_cast<size_t>(row)].id;
}

int EmployeeModel::rowCount(const QModelIndex & parent) const
{
  if (parent.isValid())
    return 0;
  return static_cast<int>(list_.size());
}

int EmployeeModel::columnCount(const QModelIndex & parent) const
{
  if (parent.isValid())
    return 0;
  return kColumns;
}

QVariant EmployeeModel::data(const QModelIndex & index, int role) const
{
  if (!index.isValid() || index.row() >= static_cast<int>(list_.size()))
    return {};

  const Employee & e = list_[static_cast<size_t>(index.row())];

  if (role == Qt::DisplayRole) {
    switch (index.column()) {
    case 0:
      return e.name;
    case 1:
      return e.taxId;
    case 2:
      return e.position;
    case 3:
      return e.isActive ? QStringLiteral("Працює") : QStringLiteral("Звільнений");
    }
  }

  if (role == Qt::UserRole) {
    return QVariant::fromValue(e.id.value);
  }

  return {};
}

QVariant EmployeeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    return {};
  switch (section) {
  case 0:
    return QStringLiteral("ПІБ");
  case 1:
    return QStringLiteral("РНОКПП");
  case 2:
    return QStringLiteral("Посада");
  case 3:
    return QStringLiteral("Статус");
  }
  return {};
}

} // namespace fin
