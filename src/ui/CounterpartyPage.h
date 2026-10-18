#pragma once

#include "fin/session/LedgerSession.h"
#include "ui/models/CounterpartyModel.h"

#include <QWidget>

class QLineEdit;
class QPushButton;
class QSortFilterProxyModel;
class QTableView;

namespace fin {

class CounterpartyPage : public QWidget
{
  Q_OBJECT

 public:
  explicit CounterpartyPage(LedgerSession & session, QWidget * parent = nullptr);
  void reload();
  void applyFilter(const QString & text);
  void reveal(CounterpartyId id);

 private:
  void add();
  void clone();
  void edit();
  void removeSelected();
  CounterpartyId selectedId() const;

  LedgerSession & session_;
  QLineEdit * searchBar_ = nullptr;
  QTableView * view_ = nullptr;
  CounterpartyModel * model_ = nullptr;
  QSortFilterProxyModel * proxy_ = nullptr;
  QPushButton * addBtn_ = nullptr;
};

} // namespace fin
