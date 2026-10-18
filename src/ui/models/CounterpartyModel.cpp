#include "ui/models/CounterpartyModel.h"

namespace fin {

CounterpartyModel::CounterpartyModel(QObject * parent) : QAbstractTableModel(parent) {}

void CounterpartyModel::setCounterparties(std::vector<Counterparty> list)
{
  beginResetModel();
  list_ = std::move(list);
  endResetModel();
}

CounterpartyId CounterpartyModel::idAt(int row) const
{
  if (row < 0 || row >= static_cast<int>(list_.size()))
    return {};
  return list_[static_cast<size_t>(row)].id;
}

int CounterpartyModel::rowCount(const QModelIndex & parent) const
{
  if (parent.isValid())
    return 0;
  return static_cast<int>(list_.size());
}

int CounterpartyModel::columnCount(const QModelIndex & parent) const
{
  if (parent.isValid())
    return 0;
  return kColumns;
}

QVariant CounterpartyModel::data(const QModelIndex & index, int role) const
{
  if (!index.isValid() || index.row() >= static_cast<int>(list_.size()))
    return {};

  const Counterparty & cp = list_[static_cast<size_t>(index.row())];

  if (role == Qt::DisplayRole) {
    switch (index.column()) {
    case 0:
      return cp.edrpou;
    case 1:
      return cp.name;
    case 2: {
      QStringList types;
      if (cp.isCustomer)
        types << QStringLiteral("Покупець");
      if (cp.isSupplier)
        types << QStringLiteral("Постачальник");
      return types.join(QStringLiteral(" / "));
    }
    case 3:
      return cp.iban;
    case 4:
      return cp.bankName;
    }
  }

  // UserRole: counterpartyId для selectedId() та reveal()
  if (role == Qt::UserRole) {
    return QVariant::fromValue(cp.id.value);
  }

  return {};
}

QVariant CounterpartyModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    return {};
  switch (section) {
  case 0:
    return QStringLiteral("ЄДРПОУ");
  case 1:
    return QStringLiteral("Назва");
  case 2:
    return QStringLiteral("Тип");
  case 3:
    return QStringLiteral("IBAN");
  case 4:
    return QStringLiteral("Банк");
  }
  return {};
}

} // namespace fin
