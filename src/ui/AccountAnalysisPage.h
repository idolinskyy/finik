#pragma once

#include "fin/session/LedgerSession.h"
#include "ui/models/AccountAnalysisModel.h"

#include <QWidget>

class QDateEdit;
class QComboBox;
class QTableView;

namespace fin {

class AccountAnalysisPage : public QWidget
{
  Q_OBJECT

 public:
  explicit AccountAnalysisPage(LedgerSession & session, QWidget * parent = nullptr);

  // Дозволяє зовнішньому коду задати рахунок і період та одразу побудувати звіт
  void setAccountAndPeriod(AccountId accountId, QDate from, QDate to);

  void rebuild();

 private:
  LedgerSession & session_;
  QComboBox * accountCb_ = nullptr;
  QDateEdit * from_ = nullptr;
  QDateEdit * to_ = nullptr;
  QTableView * view_ = nullptr;
  AccountAnalysisModel * model_ = nullptr;
};

} // namespace fin
