#pragma once

// Модель даних для довідника Номенклатури.
// Qt::UserRole повертає NomenclatureId::value для вибору при редагуванні.

#include "fin/domain/Nomenclature.h"

#include <QAbstractTableModel>
#include <vector>

namespace fin {

class NomenclatureModel : public QAbstractTableModel
{
  Q_OBJECT

 public:
  static constexpr int kColumns = 4;

  explicit NomenclatureModel(QObject * parent = nullptr);

  void setNomenclature(std::vector<Nomenclature> list);

  NomenclatureId idAt(int row) const;

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  int columnCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;

 private:
  std::vector<Nomenclature> list_;
};

} // namespace fin
