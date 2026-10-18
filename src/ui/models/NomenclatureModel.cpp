#include "ui/models/NomenclatureModel.h"

namespace fin {

NomenclatureModel::NomenclatureModel(QObject * parent) : QAbstractTableModel(parent) {}

void NomenclatureModel::setNomenclature(std::vector<Nomenclature> list)
{
  beginResetModel();
  list_ = std::move(list);
  endResetModel();
}

NomenclatureId NomenclatureModel::idAt(int row) const
{
  if (row < 0 || row >= static_cast<int>(list_.size()))
    return {};
  return list_[static_cast<size_t>(row)].id;
}

int NomenclatureModel::rowCount(const QModelIndex & parent) const
{
  if (parent.isValid())
    return 0;
  return static_cast<int>(list_.size());
}

int NomenclatureModel::columnCount(const QModelIndex & parent) const
{
  if (parent.isValid())
    return 0;
  return kColumns;
}

QVariant NomenclatureModel::data(const QModelIndex & index, int role) const
{
  if (!index.isValid() || index.row() >= static_cast<int>(list_.size()))
    return {};

  const Nomenclature & n = list_[static_cast<size_t>(index.row())];

  if (role == Qt::DisplayRole) {
    switch (index.column()) {
    case 0:
      return n.sku;
    case 1:
      return n.name;
    case 2:
      return n.unit;
    case 3:
      return n.isActive ? QStringLiteral("Активна") : QStringLiteral("Неактивна");
    }
  }

  if (role == Qt::UserRole) {
    return QVariant::fromValue(n.id.value);
  }

  return {};
}

QVariant NomenclatureModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    return {};
  switch (section) {
  case 0:
    return QStringLiteral("Артикул");
  case 1:
    return QStringLiteral("Назва");
  case 2:
    return QStringLiteral("Одиниця");
  case 3:
    return QStringLiteral("Стан");
  }
  return {};
}

} // namespace fin
