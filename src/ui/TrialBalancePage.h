#pragma once

#include "fin/session/LedgerSession.h"
#include "ui/models/TrialBalanceModel.h"

#include <QWidget>

class QDateEdit;
class QTableView;

namespace fin {

class TrialBalancePage : public QWidget
{
  Q_OBJECT

 public:
  explicit TrialBalancePage(LedgerSession & session, QWidget * parent = nullptr);
  void rebuild();

 signals:
  void openAccountCard(AccountId accountId, QDate from, QDate to);
  void openAccountAnalysis(AccountId accountId, QDate from, QDate to);
  void openSubcontoTrialBalance(AccountId accountId, QDate from, QDate to);

 private slots:
  void showContextMenu(const QPoint & pos);

 private:
  LedgerSession & session_;
  QDateEdit * from_ = nullptr;
  QDateEdit * to_ = nullptr;
  QTableView * view_ = nullptr;
  TrialBalanceModel * model_ = nullptr;
};

} // namespace fin
