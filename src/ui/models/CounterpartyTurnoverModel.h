#pragma once

// Модель даних для сторінки «Обороти по контрагентах».
// Підсумковий рядок (totals) відображається жирним через Qt::FontRole.

#include "fin/reports/Reports.h"

#include <QAbstractTableModel>
#include <QFont>

namespace fin {

class CounterpartyTurnoverModel : public QAbstractTableModel
{
  Q_OBJECT

 public:
  static constexpr int kColumns = 7;

  explicit CounterpartyTurnoverModel(QObject * parent = nullptr);

  void setReport(CounterpartyTurnover report);

  // Повертає counterpartyId рядка (порожній для підсумкового рядка)
  CounterpartyId counterpartyIdAt(int row) const;

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  int columnCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;

 private:
  CounterpartyTurnover report_;
  QFont boldFont_;

  bool isTotalsRow(int row) const;
  const CounterpartyTurnoverRow & rowAt(int row) const;
};

} // namespace fin
