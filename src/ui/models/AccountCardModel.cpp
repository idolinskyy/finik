#include "ui/models/AccountCardModel.h"

namespace fin {
namespace {

QString formatSubcontoSet(const SubcontoSet & set)
{
  QStringList parts;
  for (int i = 1; i <= 3; ++i) {
    const SubcontoValue & v = set.at(i);
    if (v.empty())
      continue;
    parts << (v.name.isEmpty() ? QString::number(v.entityId) : v.name);
  }
  return parts.join(QStringLiteral("; "));
}

} // namespace

AccountCardModel::AccountCardModel(QObject * parent) : QAbstractTableModel(parent)
{
  boldFont_.setBold(true);
}

void AccountCardModel::setReport(AccountCard card)
{
  beginResetModel();
  card_ = std::move(card);
  endResetModel();
}

DocumentId AccountCardModel::documentIdAt(int row) const
{
  const int ni = normalRowIndex(row);
  if (ni < 0)
    return {};
  return card_.rows[static_cast<size_t>(ni)].documentId;
}

AccountCardModel::RowKind AccountCardModel::rowKindAt(int row) const
{
  if (row == 0)
    return RowKind::Opening;
  if (row == totalRows() - 1)
    return RowKind::Closing;
  return RowKind::Normal;
}

int AccountCardModel::totalRows() const
{
  return static_cast<int>(card_.rows.size()) + 2; // Opening + Normal* + Closing
}

int AccountCardModel::normalRowIndex(int row) const
{
  if (row <= 0 || row >= totalRows() - 1)
    return -1;
  return row - 1; // зсув на 1 через Opening-рядок
}

int AccountCardModel::rowCount(const QModelIndex & parent) const
{
  if (parent.isValid())
    return 0;
  return totalRows();
}

int AccountCardModel::columnCount(const QModelIndex & parent) const
{
  if (parent.isValid())
    return 0;
  return kColumns;
}

QVariant AccountCardModel::data(const QModelIndex & index, int role) const
{
  if (!index.isValid() || index.row() >= totalRows())
    return {};

  const RowKind kind = rowKindAt(index.row());
  const bool isSection = (kind != RowKind::Normal);

  if (role == Qt::FontRole && isSection)
    return boldFont_;

  if (role == Qt::TextAlignmentRole &&
      (index.column() >= 3 && index.column() != 7 && index.column() != 10)) {
    return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
  }

  if (role == Qt::DisplayRole) {
    if (kind == RowKind::Opening) {
      switch (index.column()) {
      case 2:
        return QStringLiteral("Сальдо на початок");
      case 5:
        return card_.openingDebit.isZero() ? QString() : card_.openingDebit.toDisplay();
      case 6:
        return card_.openingCredit.isZero() ? QString() : card_.openingCredit.toDisplay();
      default:
        return {};
      }
    }

    if (kind == RowKind::Closing) {
      switch (index.column()) {
      case 2:
        return QStringLiteral("Сальдо на кінець / оборот");
      case 3:
        return card_.turnoverDebit.isZero() ? QString() : card_.turnoverDebit.toDisplay();
      case 4:
        return card_.turnoverCredit.isZero() ? QString() : card_.turnoverCredit.toDisplay();
      case 5:
        return card_.closingDebit.isZero() ? QString() : card_.closingDebit.toDisplay();
      case 6:
        return card_.closingCredit.isZero() ? QString() : card_.closingCredit.toDisplay();
      default:
        return {};
      }
    }

    // Normal row
    const int ni = normalRowIndex(index.row());
    const AccountCardRow & entry = card_.rows[static_cast<size_t>(ni)];
    switch (index.column()) {
    case 0:
      return entry.date.toString(QStringLiteral("dd.MM.yyyy"));
    case 1:
      return entry.documentNumber;
    case 2:
      return entry.description;
    case 3:
      return entry.debit.isZero() ? QString() : entry.debit.toDisplay();
    case 4:
      return entry.credit.isZero() ? QString() : entry.credit.toDisplay();
    case 5:
      return entry.balanceDebit.isZero() ? QString() : entry.balanceDebit.toDisplay();
    case 6:
      return entry.balanceCredit.isZero() ? QString() : entry.balanceCredit.toDisplay();
    case 7:
      return entry.currencyCode;
    case 8:
      return entry.currencyDebit.isZero() ? QString() : entry.currencyDebit.toDisplay();
    case 9:
      return entry.currencyCredit.isZero() ? QString() : entry.currencyCredit.toDisplay();
    case 10: {
      const QString debit = formatSubcontoSet(entry.debitSubconto);
      const QString credit = formatSubcontoSet(entry.creditSubconto);
      if (!debit.isEmpty() && !credit.isEmpty())
        return QStringLiteral("Дт: %1 / Кт: %2").arg(debit, credit);
      if (!debit.isEmpty())
        return debit;
      return credit;
    }
    }
  }

  // UserRole: повертає documentId для відкриття документа при double-click
  if (role == Qt::UserRole) {
    const int ni = normalRowIndex(index.row());
    if (ni >= 0) {
      return QVariant::fromValue(card_.rows[static_cast<size_t>(ni)].documentId.value);
    }
  }

  return {};
}

QVariant AccountCardModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    return {};
  switch (section) {
  case 0:
    return QStringLiteral("Дата");
  case 1:
    return QStringLiteral("Документ");
  case 2:
    return QStringLiteral("Зміст");
  case 3:
    return QStringLiteral("Дебет");
  case 4:
    return QStringLiteral("Кредит");
  case 5:
    return QStringLiteral("Сальдо Дт");
  case 6:
    return QStringLiteral("Сальдо Кт");
  case 7:
    return QStringLiteral("Валюта");
  case 8:
    return QStringLiteral("Вал. Дт");
  case 9:
    return QStringLiteral("Вал. Кт");
  case 10:
    return QStringLiteral("Субконто");
  }
  return {};
}

} // namespace fin
