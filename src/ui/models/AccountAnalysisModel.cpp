#include "ui/models/AccountAnalysisModel.h"

namespace fin {

AccountAnalysisModel::AccountAnalysisModel(QObject * parent) : QAbstractTableModel(parent)
{
  boldFont_.setBold(true);
}

void AccountAnalysisModel::setReport(AccountAnalysis report)
{
  beginResetModel();
  report_ = std::move(report);
  endResetModel();
}

// Схема рядків:
//   [0]         → SectionOpening
//   [1..N]      → Normal (кореспонденції)
//   [N+1]       → SectionTurnover
//   [N+2]       → SectionClosing
AccountAnalysisModel::RowKind AccountAnalysisModel::rowKindAt(int row) const
{
  if (row == 0)
    return RowKind::SectionOpening;
  const int corrCount = static_cast<int>(report_.correspondences.size());
  if (row <= corrCount)
    return RowKind::Normal;
  if (row == corrCount + 1)
    return RowKind::SectionTurnover;
  return RowKind::SectionClosing;
}

int AccountAnalysisModel::totalRows() const
{
  return static_cast<int>(report_.correspondences.size()) + 3; // 3 секційних рядки
}

int AccountAnalysisModel::corrIndex(int row) const
{
  if (rowKindAt(row) != RowKind::Normal)
    return -1;
  return row - 1; // [0] = SectionOpening, тому зсув -1
}

int AccountAnalysisModel::rowCount(const QModelIndex & parent) const
{
  if (parent.isValid())
    return 0;
  return totalRows();
}

int AccountAnalysisModel::columnCount(const QModelIndex & parent) const
{
  if (parent.isValid())
    return 0;
  return kColumns;
}

QVariant AccountAnalysisModel::data(const QModelIndex & index, int role) const
{
  if (!index.isValid() || index.row() >= totalRows())
    return {};

  const RowKind kind = rowKindAt(index.row());
  const bool isSection = (kind != RowKind::Normal);

  if (role == Qt::FontRole && isSection)
    return boldFont_;

  if (role == Qt::TextAlignmentRole && index.column() >= 2) {
    return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
  }

  if (role == Qt::DisplayRole) {
    if (kind == RowKind::SectionOpening) {
      switch (index.column()) {
      case 0:
        return QStringLiteral("Сальдо на початок:");
      case 2:
        return report_.openingDebit.isZero() ? QString() : report_.openingDebit.toDisplay();
      case 3:
        return report_.openingCredit.isZero() ? QString() : report_.openingCredit.toDisplay();
      default:
        return {};
      }
    }
    if (kind == RowKind::SectionTurnover) {
      switch (index.column()) {
      case 0:
        return QStringLiteral("Обороти за період:");
      case 2:
        return report_.turnoverDebit.isZero() ? QString() : report_.turnoverDebit.toDisplay();
      case 3:
        return report_.turnoverCredit.isZero() ? QString() : report_.turnoverCredit.toDisplay();
      default:
        return {};
      }
    }
    if (kind == RowKind::SectionClosing) {
      switch (index.column()) {
      case 0:
        return QStringLiteral("Сальдо на кінець:");
      case 2:
        return report_.closingDebit.isZero() ? QString() : report_.closingDebit.toDisplay();
      case 3:
        return report_.closingCredit.isZero() ? QString() : report_.closingCredit.toDisplay();
      default:
        return {};
      }
    }

    // Normal — кореспонденція
    const int ci = corrIndex(index.row());
    const AccountAnalysisCorrespondence & corr = report_.correspondences[static_cast<size_t>(ci)];
    switch (index.column()) {
    case 0:
      return corr.corrAccountCode;
    case 1:
      return corr.corrAccountName;
    case 2:
      return corr.debitTurnover.isZero() ? QString() : corr.debitTurnover.toDisplay();
    case 3:
      return corr.creditTurnover.isZero() ? QString() : corr.creditTurnover.toDisplay();
    }
  }

  return {};
}

QVariant AccountAnalysisModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    return {};
  switch (section) {
  case 0:
    return QStringLiteral("Кореспондуючий рахунок");
  case 1:
    return QStringLiteral("Назва");
  case 2:
    return QStringLiteral("Оборот Дт");
  case 3:
    return QStringLiteral("Оборот Кт");
  }
  return {};
}

} // namespace fin
