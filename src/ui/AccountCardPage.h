#pragma once

#include "fin/session/LedgerSession.h"
#include "ui/models/AccountCardModel.h"

#include <QWidget>

class QComboBox;
class QDateEdit;
class QTableView;

namespace fin {

class AccountCardPage : public QWidget
{
  Q_OBJECT

 public:
  explicit AccountCardPage(LedgerSession & session, QWidget * parent = nullptr);
  void rebuild();
  void showAccount(AccountId id);
  void setAccountAndPeriod(AccountId id, QDate from, QDate to);

 private:
  LedgerSession & session_;
  QComboBox * account_ = nullptr;
  QComboBox * counterparty_ = nullptr;
  QDateEdit * from_ = nullptr;
  QDateEdit * to_ = nullptr;
  QTableView * view_ = nullptr;
  AccountCardModel * model_ = nullptr;
};

} // namespace fin
