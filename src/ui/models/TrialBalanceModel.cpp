#include "ui/models/TrialBalanceModel.h"

namespace fin {

TrialBalanceModel::TrialBalanceModel(QObject * parent) : QAbstractTableModel(parent)
{
  boldFont_.setBold(true);
}

void TrialBalanceModel::setReport(TrialBalance report)
{
  beginResetModel();
  report_ = std::move(report);
  endResetModel();
}

AccountId TrialBalanceModel::accountIdAt(int row) const
{
  if (row < 0 || row >= rowCount())
    return {};
  if (isTotalsRow(row))
    return {};
  return report_.rows[static_cast<size_t>(row)].accountId;
}

const TrialBalanceRow & TrialBalanceModel::rowAt(int row) const
{
  if (isTotalsRow(row))
    return report_.totals;
  return report_.rows[static_cast<size_t>(row)];
}

bool TrialBalanceModel::isTotalsRow(int row) const
{
  return row == static_cast<int>(report_.rows.size());
}

int TrialBalanceModel::rowCount(const QModelIndex & parent) const
{
  if (parent.isValid())
    return 0;
  // +1 для підсумкового рядка totals
  return static_cast<int>(report_.rows.size()) + 1;
}

int TrialBalanceModel::columnCount(const QModelIndex & parent) const
{
  if (parent.isValid())
    return 0;
  return kColumns;
}

QVariant TrialBalanceModel::data(const QModelIndex & index, int role) const
{
  if (!index.isValid() || index.row() >= rowCount())
    return {};

  const TrialBalanceRow & row = rowAt(index.row());

  if (role == Qt::DisplayRole) {
    switch (index.column()) {
    case 0:
      return row.code;
    case 1:
      return row.name;
    case 2:
      return row.openingDebit.isZero() ? QString() : row.openingDebit.toDisplay();
    case 3:
      return row.openingCredit.isZero() ? QString() : row.openingCredit.toDisplay();
    case 4:
      return row.turnoverDebit.isZero() ? QString() : row.turnoverDebit.toDisplay();
    case 5:
      return row.turnoverCredit.isZero() ? QString() : row.turnoverCredit.toDisplay();
    case 6:
      return row.closingDebit.isZero() ? QString() : row.closingDebit.toDisplay();
    case 7:
      return row.closingCredit.isZero() ? QString() : row.closingCredit.toDisplay();
    case 8:
      return row.currencyCode;
    case 9:
      return row.openingForeignDebit.isZero() ? QString() : row.openingForeignDebit.toDisplay();
    case 10:
      return row.openingForeignCredit.isZero() ? QString() : row.openingForeignCredit.toDisplay();
    case 11:
      return row.closingForeignDebit.isZero() ? QString() : row.closingForeignDebit.toDisplay();
    case 12:
      return row.closingForeignCredit.isZero() ? QString() : row.closingForeignCredit.toDisplay();
    }
  }

  if (role == Qt::TextAlignmentRole && index.column() >= 2 && index.column() != 8) {
    return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
  }

  if (role == Qt::FontRole && isTotalsRow(index.row())) {
    return boldFont_;
  }

  // UserRole повертає accountId для контекстного меню
  if (role == Qt::UserRole && index.column() == 0) {
    return QVariant::fromValue(row.accountId.value);
  }

  return {};
}

QVariant TrialBalanceModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    return {};
  switch (section) {
  case 0:
    return QStringLiteral("Рахунок");
  case 1:
    return QStringLiteral("Назва");
  case 2:
    return QStringLiteral("Сальдо поч. Дт");
  case 3:
    return QStringLiteral("Сальдо поч. Кт");
  case 4:
    return QStringLiteral("Оборот Дт");
  case 5:
    return QStringLiteral("Оборот Кт");
  case 6:
    return QStringLiteral("Сальдо кін. Дт");
  case 7:
    return QStringLiteral("Сальдо кін. Кт");
  case 8:
    return QStringLiteral("Валюта");
  case 9:
    return QStringLiteral("Вал. поч. Дт");
  case 10:
    return QStringLiteral("Вал. поч. Кт");
  case 11:
    return QStringLiteral("Вал. кін. Дт");
  case 12:
    return QStringLiteral("Вал. кін. Кт");
  }
  return {};
}

} // namespace fin
