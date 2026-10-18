#pragma once

#include "fin/core/Ids.h"
#include "fin/session/LedgerSession.h"

#include <QWidget>

class QLineEdit;
class QPushButton;
class QTableWidget;

namespace fin {

class ContractPage : public QWidget
{
  Q_OBJECT

 public:
  explicit ContractPage(LedgerSession & session, QWidget * parent = nullptr);

  void reload();
  void applyFilter(const QString & text);
  void reveal(ContractId id);

 private:
  void add();
  void edit();
  void removeSelected();
  ContractId selectedId() const;

  LedgerSession & session_;
  QLineEdit * searchBar_ = nullptr;
  QTableWidget * table_ = nullptr;
  QPushButton * addBtn_ = nullptr;
};

} // namespace fin
