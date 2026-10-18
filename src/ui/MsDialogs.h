#pragma once

#include "fin/domain/Document.h"
#include "fin/domain/LowValueItem.h"
#include "fin/session/LedgerSession.h"
#include "ui/FormActions.h"

#include <QDialog>

class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTextEdit;

namespace fin {

class MsDocumentDialog : public QDialog
{
  Q_OBJECT

 public:
  MsDocumentDialog(LedgerSession & session, Document document, QWidget * parent = nullptr);
  bool persisted() const { return persisted_; }

 private:
  void addRow(const MsLine & row);
  Status collect(Document & out) const;
  Status save();
  Status saveAndPost();
  void unpost();
  void printForm();
  void refreshGate();
  void updatePostedUi();
  void openHelp();
  ui::FormButtons buttons() const;
  bool isReceipt() const;

  LedgerSession & session_;
  Document document_;
  std::vector<Account> accounts_;
  bool persisted_ = false;
  QLineEdit * number_ = nullptr;
  QDateEdit * date_ = nullptr;
  QComboBox * counterparty_ = nullptr;
  QTableWidget * table_ = nullptr;
  QTextEdit * comment_ = nullptr;
  QLabel * reason_ = nullptr;
  QPushButton * saveBtn_ = nullptr;
  QPushButton * postBtn_ = nullptr;
  QPushButton * unpostBtn_ = nullptr;
  QPushButton * printBtn_ = nullptr;
};

} // namespace fin
