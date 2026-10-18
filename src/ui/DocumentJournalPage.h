#pragma once

#include "fin/session/LedgerSession.h"

#include <QWidget>

class QComboBox;
class QDateEdit;
class QLineEdit;
class QLabel;
class QPushButton;
class QTableView;
class QSortFilterProxyModel;

namespace fin {
class DocumentJournalModel;

class DocumentJournalPage : public QWidget
{
  Q_OBJECT

 public:
  explicit DocumentJournalPage(LedgerSession & session, QWidget * parent = nullptr);
  void reload();

 private:
  void createOfType();
  void createHrOrder();
  void openSelected();
  void copySelected();
  void basedOnSelected();
  void postSelected();
  void unpostSelected();
  void removeSelected();
  void printSelected();
  void refreshActions();
  DocumentId selectedId() const;
  DocumentType selectedType() const;
  bool selectedPosted() const;

  void applyFilter(const QString & text);

  LedgerSession & session_;
  QDateEdit * from_ = nullptr;
  QDateEdit * to_ = nullptr;
  QComboBox * typeFilter_ = nullptr;
  QComboBox * counterpartyFilter_ = nullptr;
  QLineEdit * searchBar_ = nullptr;
  QTableView * table_ = nullptr;
  DocumentJournalModel * model_ = nullptr;
  QSortFilterProxyModel * proxy_ = nullptr;
  QLabel * reason_ = nullptr;
  QPushButton * createBtn_ = nullptr;
  QPushButton * createHrBtn_ = nullptr;
};

} // namespace fin
