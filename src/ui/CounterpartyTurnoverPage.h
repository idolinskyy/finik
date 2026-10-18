#pragma once

#include "fin/reports/Reports.h"
#include "fin/session/LedgerSession.h"
#include "ui/models/CounterpartyTurnoverModel.h"

#include <QWidget>

class QComboBox;
class QDateEdit;
class QTableView;

namespace fin {

class CounterpartyTurnoverPage : public QWidget
{
  Q_OBJECT

 public:
  explicit CounterpartyTurnoverPage(LedgerSession & session, QWidget * parent = nullptr);
  void rebuild();

 private:
  void printReport();

  LedgerSession & session_;
  QComboBox * account_ = nullptr;
  QDateEdit * from_ = nullptr;
  QDateEdit * to_ = nullptr;
  QTableView * view_ = nullptr;
  CounterpartyTurnoverModel * model_ = nullptr;
  CounterpartyTurnover lastReport_;
  bool hasReport_ = false;
};

} // namespace fin
