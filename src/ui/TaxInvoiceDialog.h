#pragma once

#include "fin/domain/Document.h"
#include "fin/session/LedgerSession.h"
#include "ui/FormActions.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTextEdit;

namespace fin {

class TaxInvoiceDialog : public QDialog
{
  Q_OBJECT

 public:
  TaxInvoiceDialog(LedgerSession & session, Document document, QWidget * parent = nullptr);
  bool persisted() const { return persisted_; }

 private:
  Status collect(Document & out) const;
  Status save();
  Status saveAndPost();
  void unpost();
  void registerNow();
  void unregisterNow();
  void printForm();
  void exportXml();
  void refreshGate();
  void updatePostedUi();
  void openHelp();
  ui::FormButtons buttons() const;
  void fillLines();

  LedgerSession & session_;
  Document document_;
  std::vector<Account> accounts_;
  bool persisted_ = false;
  QLineEdit * number_ = nullptr;
  QDateEdit * date_ = nullptr;
  QLineEdit * party_ = nullptr;
  QComboBox * contract_ = nullptr;
  QLineEdit * source_ = nullptr;
  QCheckBox * registered_ = nullptr;
  QDateEdit * registrationDate_ = nullptr;
  QTableWidget * table_ = nullptr;
  QTextEdit * comment_ = nullptr;
  QLabel * reason_ = nullptr;
  QPushButton * saveBtn_ = nullptr;
  QPushButton * postBtn_ = nullptr;
  QPushButton * unpostBtn_ = nullptr;
  QPushButton * registerBtn_ = nullptr;
  QPushButton * unregisterBtn_ = nullptr;
  QPushButton * printBtn_ = nullptr;
  QPushButton * exportBtn_ = nullptr;
};

} // namespace fin
