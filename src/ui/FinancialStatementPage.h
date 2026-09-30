#pragma once

#include "fin/session/LedgerSession.h"

#include <QWidget>

class QDateEdit;
class QTableWidget;

namespace fin {

class FinancialStatementPage : public QWidget
{
  Q_OBJECT

 public:
  explicit FinancialStatementPage(LedgerSession & session, QWidget * parent = nullptr);
  void rebuild();

 private:
  LedgerSession & session_;
  QDateEdit * from_ = nullptr;
  QDateEdit * to_ = nullptr;
  QTableWidget * income_ = nullptr;
  QTableWidget * balance_ = nullptr;
};

} // namespace fin
