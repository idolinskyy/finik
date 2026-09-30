#pragma once

#include "fin/session/LedgerSession.h"

#include <QWidget>

class QPushButton;
class QTableWidget;
class QLineEdit;

namespace fin {

class LowValuePage : public QWidget
{
  Q_OBJECT

 public:
  explicit LowValuePage(LedgerSession & session, QWidget * parent = nullptr);
  void reload();
  void applyFilter(const QString & text);
  void reveal(LowValueId id);

 private:
  void add();
  void clone();
  void edit();
  void removeSelected();
  LowValueId selectedId() const;

  LedgerSession & session_;
  QLineEdit * searchBar_ = nullptr;
  QTableWidget * table_ = nullptr;
  QPushButton * addBtn_ = nullptr;
};

} // namespace fin
