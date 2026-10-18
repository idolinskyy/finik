#pragma once

#include "fin/session/LedgerSession.h"
#include "ui/models/SubcontoTrialBalanceModel.h"

#include <QWidget>

class QComboBox;
class QDateEdit;
class QLabel;
class QTableView;

namespace fin {

class SubcontoTrialBalancePage : public QWidget
{
  Q_OBJECT

 public:
  explicit SubcontoTrialBalancePage(LedgerSession & session, QWidget * parent = nullptr);

  void setAccountAndPeriod(AccountId accountId, QDate from, QDate to, int lineNo = 1);
  void rebuild();

 private:
  void reloadAccounts();
  void reloadLineNos();

  LedgerSession & session_;
  QComboBox * accountCb_ = nullptr;
  QComboBox * lineCb_ = nullptr;
  QLabel * kindHint_ = nullptr;
  QDateEdit * from_ = nullptr;
  QDateEdit * to_ = nullptr;
  QTableView * view_ = nullptr;
  SubcontoTrialBalanceModel * model_ = nullptr;
};

} // namespace fin
