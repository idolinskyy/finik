#pragma once

#include "fin/core/Ids.h"
#include "fin/session/LedgerSession.h"

#include <QWidget>

class QPushButton;
class QTableWidget;
class QLineEdit;

namespace fin {

class WarehousePage : public QWidget
{
  Q_OBJECT

 public:
  explicit WarehousePage(LedgerSession & session, QWidget * parent = nullptr);

  void reload();
  void applyFilter(const QString & text);

 private:
  void add();
  void clone();
  void edit();
  void removeSelected();
  WarehouseId selectedId() const;

  LedgerSession & session_;
  QLineEdit * searchBar_ = nullptr;
  QTableWidget * table_ = nullptr;
  QPushButton * addBtn_ = nullptr;
};

} // namespace fin
