#pragma once

// Модель списку контрагентів.

#include "fin/domain/Counterparty.h"

#include <QAbstractTableModel>
#include <vector>

namespace fin {

class CounterpartyModel : public QAbstractTableModel
{
  Q_OBJECT

 public:
  static constexpr int kColumns = 5;

  explicit CounterpartyModel(QObject * parent = nullptr);

  void setCounterparties(std::vector<Counterparty> list);

  // Повертає id контрагента за рядком у source model.
  CounterpartyId idAt(int row) const;

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  int columnCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;

 private:
  std::vector<Counterparty> list_;
};

} // namespace fin
