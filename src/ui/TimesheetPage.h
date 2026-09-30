#pragma once

#include "fin/session/LedgerSession.h"
#include <QWidget>

class QComboBox;
class QSpinBox;
class QTableWidget;
class QPushButton;

namespace fin {

class TimesheetPage : public QWidget
{
  Q_OBJECT

 public:
  explicit TimesheetPage(LedgerSession & session, QWidget * parent = nullptr);
  void reload();

 private:
  void saveRow(int row);
  void printTimesheet();

  LedgerSession & session_;
  QComboBox * monthCombo_ = nullptr;
  QSpinBox * yearSpinBox_ = nullptr;
  QTableWidget * table_ = nullptr;
  QPushButton * printBtn_ = nullptr;
};

} // namespace fin
