#pragma once

#include "fin/session/LedgerSession.h"
#include "ui/models/NomenclatureModel.h"

#include <QWidget>

class QLineEdit;
class QPushButton;
class QSortFilterProxyModel;
class QTableView;

namespace fin {

class NomenclaturePage : public QWidget
{
  Q_OBJECT

 public:
  explicit NomenclaturePage(LedgerSession & session, QWidget * parent = nullptr);
  void reload();
  void applyFilter(const QString & text);
  void reveal(NomenclatureId id);

 private:
  void add();
  void clone();
  void edit();
  void removeSelected();
  NomenclatureId selectedId() const;

  LedgerSession & session_;
  QLineEdit * searchBar_ = nullptr;
  QTableView * view_ = nullptr;
  NomenclatureModel * model_ = nullptr;
  QSortFilterProxyModel * proxy_ = nullptr;
  QPushButton * addBtn_ = nullptr;
};

} // namespace fin
