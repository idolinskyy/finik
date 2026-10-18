#include "ui/DocumentJournalModel.h"

#include <QColor>
#include <QDate>

namespace fin {

DocumentJournalModel::DocumentJournalModel(QObject * parent) : QAbstractTableModel(parent) {}

void DocumentJournalModel::setDocuments(std::vector<DocumentHeader> docs)
{
  beginResetModel();
  docs_ = std::move(docs);
  endResetModel();
}

const DocumentHeader & DocumentJournalModel::documentAt(int row) const
{
  return docs_.at(static_cast<size_t>(row));
}

int DocumentJournalModel::rowCount(const QModelIndex & parent) const
{
  if (parent.isValid())
    return 0;
  return static_cast<int>(docs_.size());
}

int DocumentJournalModel::columnCount(const QModelIndex & parent) const
{
  if (parent.isValid())
    return 0;
  return 6;
}

QVariant DocumentJournalModel::data(const QModelIndex & index, int role) const
{
  if (!index.isValid() || index.row() >= static_cast<int>(docs_.size()))
    return {};

  const DocumentHeader & doc = docs_[static_cast<size_t>(index.row())];

  if (role == Qt::DisplayRole) {
    switch (index.column()) {
    case 0:
      return doc.date.toString(QStringLiteral("dd.MM.yyyy"));
    case 1:
      return doc.number;
    case 2:
      return documentTypeLabelUk(doc.type);
    case 3:
      return doc.counterpartyName;
    case 4:
      return doc.total.isZero() ? QString() : doc.total.toDisplay();
    case 5:
      return doc.posted ? QStringLiteral("Так") : QStringLiteral("Ні");
    }
  } else if (role == Qt::ForegroundRole) {
    if (!doc.posted) {
      return QColor(Qt::darkGray);
    }
  }

  return {};
}

QVariant DocumentJournalModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
    switch (section) {
    case 0:
      return QStringLiteral("Дата");
    case 1:
      return QStringLiteral("Номер");
    case 2:
      return QStringLiteral("Тип");
    case 3:
      return QStringLiteral("Контрагент");
    case 4:
      return QStringLiteral("Сума");
    case 5:
      return QStringLiteral("Проведено");
    }
  }
  return {};
}

} // namespace fin
