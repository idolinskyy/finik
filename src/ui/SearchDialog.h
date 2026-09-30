#pragma once

#include "fin/search/GlobalSearch.h"
#include "fin/session/LedgerSession.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;
class QTimer;

namespace fin {

class SearchDialog : public QDialog
{
  Q_OBJECT

 public:
  explicit SearchDialog(LedgerSession & session, QWidget * parent = nullptr);
  SearchHit selected() const { return selected_; }
  void prime(const QString & query);

 private:
  void scheduleSearch();
  void runSearch();
  void acceptCurrent();
  void openHelp();

  LedgerSession & session_;
  SearchHit selected_;
  QLineEdit * query_ = nullptr;
  QListWidget * results_ = nullptr;
  QLabel * status_ = nullptr;
  QTimer * debounce_ = nullptr;
};

} // namespace fin
