#pragma once

#include "fin/session/LedgerSession.h"

#include <QWidget>

class QTreeWidget;

namespace fin {

class AccountPage : public QWidget
{
  Q_OBJECT

 public:
  explicit AccountPage(LedgerSession & session, QWidget * parent = nullptr);
  void reload();
  void reveal(AccountId id);

 private:
  // Прив'язка субконто — це зміна довідника: лише бухгалтер або адміністратор
  bool canEditSubconto() const;

  LedgerSession & session_;
  QTreeWidget * tree_ = nullptr;
};

} // namespace fin
