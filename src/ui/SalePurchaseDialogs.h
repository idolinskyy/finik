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

class GoodsDocumentDialog : public QDialog
{
  Q_OBJECT

 public:
  GoodsDocumentDialog(LedgerSession & session, Document document, QWidget * parent = nullptr);
  bool persisted() const { return persisted_; }

 private:
  void addRow(const GoodsLine & row);
  Status collect(Document & out) const;
  Status save();
  Status saveAndPost();
  void unpost();
  void printForm();
  void updatePostedUi();
  void updateTotal();
  void refreshOffsetHint();
  void openHelp();
  void refreshGate();
  ui::FormButtons buttons() const;

  LedgerSession & session_;
  Document document_;
  std::vector<Account> accounts_;
  bool persisted_ = false;
  QLineEdit * number_ = nullptr;
  QDateEdit * date_ = nullptr;
  QComboBox * counterparty_ = nullptr;
  QComboBox * contract_ = nullptr;
  QComboBox * orderStatus_ = nullptr;
  QComboBox * currency_ = nullptr;
  QLineEdit * currencyRate_ = nullptr;
  QComboBox * warehouse_ = nullptr;
  QComboBox * targetWarehouse_ = nullptr;
  QTableWidget * table_ = nullptr;
  QTextEdit * comment_ = nullptr;
  QLabel * total_ = nullptr;
  QLabel * offsetHint_ = nullptr;
  QLabel * fiscalHint_ = nullptr;
  QLabel * reason_ = nullptr;
  QPushButton * saveBtn_ = nullptr;
  QPushButton * postBtn_ = nullptr;
  QPushButton * unpostBtn_ = nullptr;
  QPushButton * printBtn_ = nullptr;
  bool vatPayer_ = false;
  int vatRateBp_ = 2000;
};

class ServicePurchaseDialog : public QDialog
{
  Q_OBJECT

 public:
  ServicePurchaseDialog(LedgerSession & session, Document document, QWidget * parent = nullptr);
  bool persisted() const { return persisted_; }

 private:
  void addRow(const CashLine & row);
  Status collect(Document & out) const;
  Status save();
  Status saveAndPost();
  void unpost();
  void printForm();
  void updatePostedUi();
  void refreshOffsetHint();
  void openHelp();
  void refreshGate();
  ui::FormButtons buttons() const;

  LedgerSession & session_;
  Document document_;
  std::vector<Account> accounts_;
  std::vector<Account> debitAccounts_;
  bool persisted_ = false;
  QLineEdit * number_ = nullptr;
  QDateEdit * date_ = nullptr;
  QComboBox * counterparty_ = nullptr;
  QComboBox * contract_ = nullptr;
  QComboBox * currency_ = nullptr;
  QLineEdit * currencyRate_ = nullptr;
  QTableWidget * table_ = nullptr;
  QTextEdit * comment_ = nullptr;
  QLabel * total_ = nullptr;
  QLabel * offsetHint_ = nullptr;
  QLabel * reason_ = nullptr;
  QPushButton * saveBtn_ = nullptr;
  QPushButton * postBtn_ = nullptr;
  QPushButton * unpostBtn_ = nullptr;
  QPushButton * printBtn_ = nullptr;
  bool vatPayer_ = false;
  int vatRateBp_ = 2000;
};

} // namespace fin
