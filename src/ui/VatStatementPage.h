#pragma once

#include "fin/session/LedgerSession.h"

#include <QWidget>

class QDateEdit;
class QTableWidget;

namespace fin {

class VatStatementPage : public QWidget
{
  Q_OBJECT

 public:
  explicit VatStatementPage(LedgerSession & session, QWidget * parent = nullptr);
  void rebuild();

 private:
  LedgerSession & session_;
  QDateEdit * from_ = nullptr;
  QDateEdit * to_ = nullptr;
  QTableWidget * table_ = nullptr;
};

} // namespace fin
