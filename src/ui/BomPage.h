#pragma once

#include "fin/core/Ids.h"
#include "fin/session/LedgerSession.h"

#include <QWidget>

class QPushButton;
class QTableWidget;

namespace fin {

class BomPage : public QWidget
{
  Q_OBJECT

 public:
  explicit BomPage(LedgerSession & session, QWidget * parent = nullptr);
  void reload();
  void reveal(BomId id);

 private:
  void add();
  void edit();
  void removeSelected();
  void createMaterialIssue();
  BomId selectedId() const;

  LedgerSession & session_;
  QTableWidget * table_ = nullptr;
  QTableWidget * componentsTable_ = nullptr;
  QPushButton * addBtn_ = nullptr;
  QPushButton * editBtn_ = nullptr;
  QPushButton * removeBtn_ = nullptr;
  QPushButton * issueBtn_ = nullptr;
};

} // namespace fin
