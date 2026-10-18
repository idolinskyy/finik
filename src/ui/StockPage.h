#pragma once

#include "fin/session/LedgerSession.h"

#include <QWidget>

class QComboBox;
class QDateEdit;
class QTableWidget;

namespace fin {

class StockPage : public QWidget
{
  Q_OBJECT

 public:
  explicit StockPage(LedgerSession & session, QWidget * parent = nullptr);
  void rebuild();

 private:
  LedgerSession & session_;
  QDateEdit * from_ = nullptr;
  QDateEdit * to_ = nullptr;
  QComboBox * warehouse_ = nullptr;
  QTableWidget * table_ = nullptr;
};

} // namespace fin
