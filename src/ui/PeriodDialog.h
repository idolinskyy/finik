#pragma once

#include "fin/session/LedgerSession.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace fin {

class PeriodDialog : public QDialog
{
  Q_OBJECT

 public:
  PeriodDialog(LedgerSession & session, QWidget * parent = nullptr);

 private:
  void refresh();
  void closeMonth();
  void reopenMonth();
  void postResult();

  LedgerSession & session_;
  QLabel * status_ = nullptr;
  QSpinBox * year_ = nullptr;
  QComboBox * month_ = nullptr;
  QPushButton * closeBtn_ = nullptr;
  QPushButton * resultBtn_ = nullptr;
  QPushButton * reopenBtn_ = nullptr;
};

} // namespace fin
