#pragma once

#include "fin/session/LedgerSession.h"

#include <cstdint>

#include <QWidget>

class QComboBox;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace fin {

class SubcontoItemPage : public QWidget
{
  Q_OBJECT

 public:
  explicit SubcontoItemPage(LedgerSession & session, QWidget * parent = nullptr);

  void reload();
  void applyFilter(const QString & text);

 private:
  void add();
  void edit();
  void removeSelected();
  void addKind();
  std::int64_t selectedId() const;
  QString currentKindId() const;

  LedgerSession & session_;
  QComboBox * kind_ = nullptr;
  QLineEdit * searchBar_ = nullptr;
  QTableWidget * table_ = nullptr;
  QPushButton * addBtn_ = nullptr;
  QPushButton * addKindBtn_ = nullptr;
};

} // namespace fin
