#pragma once

// Модель «Картка рахунку».

#include "fin/reports/Reports.h"

#include <QAbstractTableModel>
#include <QFont>

namespace fin {

class AccountCardModel : public QAbstractTableModel
{
  Q_OBJECT

 public:
  static constexpr int kColumns = 11;

  enum class RowKind
  {
    Opening,
    Normal,
    Closing
  };

  explicit AccountCardModel(QObject * parent = nullptr);

  void setReport(AccountCard card);

  // Повертає documentId рядка (0 для секційних рядків).
  DocumentId documentIdAt(int row) const;

  RowKind rowKindAt(int row) const;

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  int columnCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;

 private:
  AccountCard card_;
  QFont boldFont_;

  // Кількість рядків: 1 (opening) + rows.size() + 1 (closing)
  int totalRows() const;
  // Повертає індекс у card_.rows (або -1 для секційних рядків)
  int normalRowIndex(int row) const;
};

} // namespace fin
