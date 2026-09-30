#include "ui/models/SubcontoTrialBalanceModel.h"

namespace fin {

SubcontoTrialBalanceModel::SubcontoTrialBalanceModel(QObject * parent) : QAbstractTableModel(parent)
{
  boldFont_.setBold(true);
}

void SubcontoTrialBalanceModel::setReport(SubcontoTrialBalance report)
{
  beginResetModel();
  report_ = std::move(report);
  endResetModel();
}

const SubcontoTrialBalanceRow & SubcontoTrialBalanceModel::rowAt(int row) const
{
  if (isTotalsRow(row))
    return report_.totals;
  return report_.rows[static_cast<size_t>(row)];
}

bool SubcontoTrialBalanceModel::isTotalsRow(int row) const
{
  return row == static_cast<int>(report_.rows.size());
}

int SubcontoTrialBalanceModel::rowCount(const QModelIndex & parent) const
{
  if (parent.isValid())
    return 0;
  return static_cast<int>(report_.rows.size()) + 1;
}

int SubcontoTrialBalanceModel::columnCount(const QModelIndex & parent) const
{
  if (parent.isValid())
    return 0;
  return kColumns;
}

QVariant SubcontoTrialBalanceModel::data(const QModelIndex & index, int role) const
{
  if (!index.isValid() || index.row() >= rowCount())
    return {};

  const SubcontoTrialBalanceRow & row = rowAt(index.row());

  if (role == Qt::DisplayRole) {
    switch (index.column()) {
    case 0:
      if (isTotalsRow(index.row()))
        return QStringLiteral("Разом");
      return row.subconto.name.isEmpty()
                 ? (row.subconto.entityId > 0 ? QString::number(row.subconto.entityId)
                                              : QStringLiteral("(без аналітики)"))
                 : row.subconto.name;
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

  return {};
}

QVariant SubcontoTrialBalanceModel::headerData(int section, Qt::Orientation orientation,
                                               int role) const
{
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    return {};
  switch (section) {
  case 0:
    return report_.subcontoKindName.isEmpty() ? QStringLiteral("Субконто")
                                              : report_.subcontoKindName;
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
