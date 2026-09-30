#pragma once

#include "fin/domain/Document.h"

#include <QAbstractTableModel>
#include <vector>

namespace fin {

class DocumentJournalModel : public QAbstractTableModel
{
  Q_OBJECT

 public:
  explicit DocumentJournalModel(QObject * parent = nullptr);

  void setDocuments(std::vector<DocumentHeader> docs);
  const DocumentHeader & documentAt(int row) const;

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  int columnCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;

 private:
  std::vector<DocumentHeader> docs_;
};

} // namespace fin
