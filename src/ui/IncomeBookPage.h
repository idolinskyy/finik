#pragma once

#include "fin/session/LedgerSession.h"

#include <QWidget>

class QDateEdit;
class QTableWidget;

namespace fin {

class IncomeBookPage : public QWidget
{
  Q_OBJECT

 public:
  explicit IncomeBookPage(LedgerSession & session, QWidget * parent = nullptr);
  void rebuild();

 private:
  void doExportXml();

  LedgerSession & session_;
  QDateEdit * from_ = nullptr;
  QDateEdit * to_ = nullptr;
  QTableWidget * table_ = nullptr;
};

} // namespace fin
