#pragma once

#include <QWidget>
#include <QLabel>
#include <QTableWidget>
#include "fin/session/LedgerSession.h"

namespace fin {

class DashboardPage : public QWidget
{
  Q_OBJECT

 public:
  explicit DashboardPage(LedgerSession & session, QWidget * parent = nullptr);

  void reload();

 private:
  LedgerSession & session_;

  QLabel * usdLabel_ = nullptr;
  QLabel * eurLabel_ = nullptr;
  QLabel * plnLabel_ = nullptr;

  QLabel * cashLabel_ = nullptr;
  QLabel * bankLabel_ = nullptr;
  QLabel * cashFlowLabel_ = nullptr;
  QLabel * marginLabel_ = nullptr;

  QTableWidget * recentTable_ = nullptr;
};

} // namespace fin
