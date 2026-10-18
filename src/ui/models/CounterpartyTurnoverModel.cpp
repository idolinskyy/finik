#include "ui/models/CounterpartyTurnoverModel.h"

namespace fin {

CounterpartyTurnoverModel::CounterpartyTurnoverModel(QObject * parent) : QAbstractTableModel(parent)
{
  boldFont_.setBold(true);
}

void CounterpartyTurnoverModel::setReport(CounterpartyTurnover report)
{
  beginResetModel();
  report_ = std::move(report);
  endResetModel();
}

CounterpartyId CounterpartyTurnoverModel::counterpartyIdAt(int row) const
{
  if (isTotalsRow(row))
    return {};
  return report_.rows[static_cast<size_t>(row)].counterpartyId;
}

bool CounterpartyTurnoverModel::isTotalsRow(int row) const
{
  return row == static_cast<int>(report_.rows.size());
}

const CounterpartyTurnoverRow & CounterpartyTurnoverModel::rowAt(int row) const
{
  if (isTotalsRow(row))
    return report_.totals;
  return report_.rows[static_cast<size_t>(row)];
}

int CounterpartyTurnoverModel::rowCount(const QModelIndex & parent) const
{
  if (parent.isValid())
    return 0;
  return static_cast<int>(report_.rows.size()) + 1; // +1 для totals
}

int CounterpartyTurnoverModel::columnCount(const QModelIndex & parent) const
{
  if (parent.isValid())
    return 0;
  return kColumns;
}

QVariant CounterpartyTurnoverModel::data(const QModelIndex & index, int role) const
{
  if (!index.isValid() || index.row() >= rowCount())
    return {};

  const CounterpartyTurnoverRow & row = rowAt(index.row());

  if (role == Qt::DisplayRole) {
    switch (index.column()) {
    case 0:
      return row.name;
    case 1:
      return row.openingDebit.isZero() ? QString() : row.openingDebit.toDisplay();
    case 2:
      return row.openingCredit.isZero() ? QString() : row.openingCredit.toDisplay();
    case 3:
      return row.turnoverDebit.isZero() ? QString() : row.turnoverDebit.toDisplay();
    case 4:
      return row.turnoverCredit.isZero() ? QString() : row.turnoverCredit.toDisplay();
    case 5:
      return row.closingDebit.isZero() ? QString() : row.closingDebit.toDisplay();
    case 6:
      return row.closingCredit.isZero() ? QString() : row.closingCredit.toDisplay();
    }
  }

  if (role == Qt::TextAlignmentRole && index.column() >= 1) {
    return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
  }

  if (role == Qt::FontRole && isTotalsRow(index.row())) {
    return boldFont_;
  }

  if (role == Qt::UserRole && index.column() == 0) {
    return QVariant::fromValue(row.counterpartyId.value);
  }

  return {};
}

QVariant CounterpartyTurnoverModel::headerData(int section, Qt::Orientation orientation,
                                               int role) const
{
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    return {};
  switch (section) {
  case 0:
    return QStringLiteral("Контрагент");
  case 1:
    return QStringLiteral("Сальдо поч. Дт");
  case 2:
    return QStringLiteral("Сальдо поч. Кт");
  case 3:
    return QStringLiteral("Оборот Дт");
  case 4:
    return QStringLiteral("Оборот Кт");
  case 5:
    return QStringLiteral("Сальдо кін. Дт");
  case 6:
    return QStringLiteral("Сальдо кін. Кт");
  }
  return {};
}

} // namespace fin
