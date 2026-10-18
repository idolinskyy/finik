#pragma once

// Модель оборотно-сальдової відомості.

#include "fin/reports/Reports.h"

#include <QAbstractTableModel>
#include <QFont>

namespace fin {

class TrialBalanceModel : public QAbstractTableModel
{
  Q_OBJECT

 public:
  static constexpr int kColumns = 13;

  explicit TrialBalanceModel(QObject * parent = nullptr);

  // Завантажує нові дані та сповіщає view про зміну.
  void setReport(TrialBalance report);

  // Повертає accountId рядка для навігації (ПКМ → Картка/Аналіз).
  AccountId accountIdAt(int row) const;

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  int columnCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;

 private:
  TrialBalance report_;
  QFont boldFont_;

  // Повертає рядок за індексом; останній рядок — totals.
  const TrialBalanceRow & rowAt(int row) const;
  bool isTotalsRow(int row) const;
};

} // namespace fin
