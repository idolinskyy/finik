#pragma once

#include "fin/domain/Document.h"
#include "fin/domain/FixedAsset.h"
#include "fin/session/LedgerSession.h"
#include "ui/FormActions.h"

#include <QDialog>

class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTextEdit;

namespace fin {

class FaCommissionDialog : public QDialog
{
  Q_OBJECT

 public:
  FaCommissionDialog(LedgerSession & session, Document document, QWidget * parent = nullptr);
  bool persisted() const { return persisted_; }

 private:
  void addRow(FixedAssetId id);
  Status collect(Document & out) const;
  Status save();
  Status saveAndPost();
  void unpost();
  void printForm();
  void refreshGate();
  void updatePostedUi();
  void openHelp();
  ui::FormButtons buttons() const;
  AccountId correspondenceId() const;

  LedgerSession & session_;
  Document document_;
  std::vector<Account> accounts_;
  bool persisted_ = false;
  QLineEdit * number_ = nullptr;
  QDateEdit * date_ = nullptr;
  QComboBox * counterparty_ = nullptr;
  QComboBox * correspondence_ = nullptr;
  QTableWidget * table_ = nullptr;
  QTextEdit * comment_ = nullptr;
  QLabel * reason_ = nullptr;
  QPushButton * saveBtn_ = nullptr;
  QPushButton * postBtn_ = nullptr;
  QPushButton * unpostBtn_ = nullptr;
  QPushButton * printBtn_ = nullptr;
};

class FaDepreciationDialog : public QDialog
{
  Q_OBJECT

 public:
  FaDepreciationDialog(LedgerSession & session, Document document, QWidget * parent = nullptr);
  bool persisted() const { return persisted_; }

 private:
  void rebuild();
  Status collect(Document & out) const;
  Status save();
  Status saveAndPost();
  void unpost();
  void printForm();
  void refreshGate();
  void updatePostedUi();
  void showLines(const std::vector<DocumentLine> & lines);
  void openHelp();
  ui::FormButtons buttons() const;

  LedgerSession & session_;
  Document document_;
  bool persisted_ = false;
  QLineEdit * number_ = nullptr;
  QSpinBox * year_ = nullptr;
  QComboBox * month_ = nullptr;
  QTableWidget * table_ = nullptr;
  QTextEdit * comment_ = nullptr;
  QLabel * reason_ = nullptr;
  QPushButton * saveBtn_ = nullptr;
  QPushButton * postBtn_ = nullptr;
  QPushButton * unpostBtn_ = nullptr;
  QPushButton * printBtn_ = nullptr;
};

class FaWriteOffDialog : public QDialog
{
  Q_OBJECT

 public:
  FaWriteOffDialog(LedgerSession & session, Document document, QWidget * parent = nullptr);
  bool persisted() const { return persisted_; }

 private:
  void addRow(FixedAssetId id);
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
  QTableWidget * table_ = nullptr;
  QTextEdit * comment_ = nullptr;
  QLabel * reason_ = nullptr;
  QPushButton * saveBtn_ = nullptr;
  QPushButton * postBtn_ = nullptr;
  QPushButton * unpostBtn_ = nullptr;
  QPushButton * printBtn_ = nullptr;
};

} // namespace fin
