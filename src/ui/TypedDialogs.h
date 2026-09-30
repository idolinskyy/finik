#pragma once

#include "fin/domain/Document.h"
#include "fin/domain/DocumentBlueprint.h"
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

class BankDocumentDialog : public QDialog
{
  Q_OBJECT

 public:
  BankDocumentDialog(LedgerSession & session, Document document, QWidget * parent = nullptr);
  bool persisted() const { return persisted_; }

 private:
  void addRow(const BankLine & row);
  Status collect(Document & out) const;
  Status save();
  Status saveAndPost();
  void unpost();
  void printForm();
  void refreshGate();
  void updatePostedUi();
  void openHelp();
  ui::FormButtons buttons() const;

  LedgerSession & session_;
  Document document_;
  std::vector<Account> accounts_;
  bool persisted_ = false;
  QLineEdit * number_ = nullptr;
  QDateEdit * date_ = nullptr;
  QComboBox * direction_ = nullptr;
  QComboBox * counterparty_ = nullptr;
  QComboBox * currency_ = nullptr;
  QLineEdit * currencyRate_ = nullptr;
  QTableWidget * table_ = nullptr;
  QTextEdit * comment_ = nullptr;
  QLabel * reason_ = nullptr;
  QPushButton * saveBtn_ = nullptr;
  QPushButton * postBtn_ = nullptr;
  QPushButton * unpostBtn_ = nullptr;
  QPushButton * printBtn_ = nullptr;
};

class OpeningDocumentDialog : public QDialog
{
  Q_OBJECT

 public:
  OpeningDocumentDialog(LedgerSession & session, Document document, QWidget * parent = nullptr);
  bool persisted() const { return persisted_; }

 private:
  void addMoneyRow(const OpeningMoneyLine & row);
  void addGoodsRow(const GoodsLine & row);
  Status collect(Document & out) const;
  Status save();
  Status saveAndPost();
  void unpost();
  void refreshGate();
  void updatePostedUi();
  void openHelp();
  ui::FormButtons buttons() const;

  LedgerSession & session_;
  Document document_;
  std::vector<Account> accounts_;
  bool persisted_ = false;
  QLineEdit * number_ = nullptr;
  QDateEdit * date_ = nullptr;
  QTableWidget * money_ = nullptr;
  QTableWidget * goods_ = nullptr;
  QTextEdit * comment_ = nullptr;
  QLabel * reason_ = nullptr;
  QPushButton * saveBtn_ = nullptr;
  QPushButton * postBtn_ = nullptr;
  QPushButton * unpostBtn_ = nullptr;
};

} // namespace fin
