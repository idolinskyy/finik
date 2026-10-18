#pragma once

#include "fin/session/LedgerSession.h"

#include <QWidget>

class QComboBox;
class QDateEdit;
class QTableWidget;

namespace fin {

class ReconciliationPage : public QWidget
{
  Q_OBJECT

 public:
  explicit ReconciliationPage(LedgerSession & session, QWidget * parent = nullptr);
  void rebuild();

 private:
  void printAct();

  LedgerSession & session_;
  QComboBox * account_ = nullptr;
  QComboBox * counterparty_ = nullptr;
  QDateEdit * from_ = nullptr;
  QDateEdit * to_ = nullptr;
  QTableWidget * table_ = nullptr;
};

} // namespace fin
