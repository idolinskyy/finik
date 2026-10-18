#pragma once

#include "fin/session/LedgerSession.h"

#include <QWidget>

class QDateEdit;
class QPushButton;
class QTableWidget;

namespace fin {

class CashBookPage : public QWidget
{
  Q_OBJECT

 public:
  CashBookPage(LedgerSession & session, const char * accountCode, const QString & title,
               QWidget * parent = nullptr);
  void rebuild();

 private:
  void exportBook();
  void exportJournal();
  void importStatement();
  void exportPayments();
  void fetchFromMonobank();

  LedgerSession & session_;
  const char * accountCode_;
  QString title_;
  QDateEdit * from_ = nullptr;
  QDateEdit * to_ = nullptr;
  QTableWidget * table_ = nullptr;
  QPushButton * importBtn_ = nullptr;
  QPushButton * exportPaymentsBtn_ = nullptr;
  QPushButton * monobankBtn_ = nullptr;
};

} // namespace fin
