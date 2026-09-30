#pragma once

// Модель «Аналіз рахунку».

#include "fin/reports/Reports.h"

#include <QAbstractTableModel>
#include <QFont>
#include <vector>

namespace fin {

class AccountAnalysisModel : public QAbstractTableModel
{
  Q_OBJECT

 public:
  static constexpr int kColumns = 4;

  // Тип рядка: нормальна кореспонденція або секційний підсумок
  enum class RowKind
  {
    SectionOpening,
    Normal,
    SectionTurnover,
    SectionClosing
  };

  explicit AccountAnalysisModel(QObject * parent = nullptr);

  void setReport(AccountAnalysis report);

  RowKind rowKindAt(int row) const;

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  int columnCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;

 private:
  AccountAnalysis report_;
  QFont boldFont_;

  // Загальна кількість рядків = 3 секційних + кількість кореспондентів
  int totalRows() const;
  // Повертає індекс у report_.correspondences (-1 для секційних)
  int corrIndex(int row) const;
};

} // namespace fin
