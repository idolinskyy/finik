#pragma once

// Модель «ОСВ по субконто».

#include "fin/reports/Reports.h"

#include <QAbstractTableModel>
#include <QFont>

namespace fin {

class SubcontoTrialBalanceModel : public QAbstractTableModel
{
  Q_OBJECT

 public:
  static constexpr int kColumns = 7;

  explicit SubcontoTrialBalanceModel(QObject * parent = nullptr);

  void setReport(SubcontoTrialBalance report);

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  int columnCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;

 private:
  SubcontoTrialBalance report_;
  QFont boldFont_;

  const SubcontoTrialBalanceRow & rowAt(int row) const;
  bool isTotalsRow(int row) const;
};

} // namespace fin
