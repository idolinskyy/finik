#pragma once

#include "fin/session/LedgerSession.h"
#include "ui/models/EmployeeModel.h"

#include <QWidget>

class QLineEdit;
class QPushButton;
class QSortFilterProxyModel;
class QTableView;

namespace fin {

class EmployeePage : public QWidget
{
  Q_OBJECT

 public:
  explicit EmployeePage(LedgerSession & session, QWidget * parent = nullptr);
  void reload();
  void applyFilter(const QString & text);
  void reveal(EmployeeId id);

 private:
  void add();
  void clone();
  void edit();
  void removeSelected();
  EmployeeId selectedId() const;

  LedgerSession & session_;
  QLineEdit * searchBar_ = nullptr;
  QTableView * view_ = nullptr;
  EmployeeModel * model_ = nullptr;
  QSortFilterProxyModel * proxy_ = nullptr;
  QPushButton * addBtn_ = nullptr;
};

} // namespace fin
