#pragma once

#include "fin/session/LedgerSession.h"

#include <QWidget>

class QDateEdit;
class QLabel;
class QTableWidget;

namespace fin {

class CashFlowPage : public QWidget
{
  Q_OBJECT

 public:
  explicit CashFlowPage(LedgerSession & session, QWidget * parent = nullptr);
  void rebuild();

 private:
  LedgerSession & session_;
  QDateEdit * from_ = nullptr;
  QDateEdit * to_ = nullptr;
  QLabel * summary_ = nullptr;
  QTableWidget * table_ = nullptr;
  QTableWidget * margin_ = nullptr;
};

} // namespace fin
