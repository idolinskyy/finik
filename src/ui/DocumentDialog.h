#pragma once

#include "fin/domain/Document.h"
#include "fin/session/LedgerSession.h"
#include "ui/FormActions.h"

#include <QDialog>

class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QTableWidget;
class QTextEdit;

class QPushButton;

namespace fin {

class DocumentDialog : public QDialog
{
  Q_OBJECT

 public:
  DocumentDialog(LedgerSession & session, Document document, QWidget * parent = nullptr);
  bool persisted() const { return persisted_; }

  static bool createNew(LedgerSession & session, DocumentType type, QWidget * parent);
  static bool createBasedOn(LedgerSession & session, const Document & source, DocumentType target,
                            QWidget * parent);
  static bool editExisting(LedgerSession & session, DocumentId id, QWidget * parent);
  static bool openDocument(LedgerSession & session, Document document, QWidget * parent);

 private:
  void addLineRow(const DocumentLine & line);
  void bindQtyPrice(QLineEdit * qty, QLineEdit * price, QLineEdit * amount);
  void applyTypeDefaults();
  Status collect(Document & out) const;
  Status save();
  Status saveAndPost();
  void unpost();
  void updatePostedUi();
  void refreshGate();
  ui::FormButtons buttons() const;

  LedgerSession & session_;
  Document document_;
  std::vector<Account> accounts_;
  bool persisted_ = false;
  QComboBox * type_ = nullptr;
  QLineEdit * number_ = nullptr;
  QDateEdit * date_ = nullptr;
  QComboBox * counterparty_ = nullptr;
  QComboBox * contract_ = nullptr;
  QComboBox * employee_ = nullptr;
  QTableWidget * lines_ = nullptr;
  QTextEdit * comment_ = nullptr;
  QLabel * hint_ = nullptr;
  QLabel * reason_ = nullptr;
  QPushButton * saveBtn_ = nullptr;
  QPushButton * postBtn_ = nullptr;
  QPushButton * unpostBtn_ = nullptr;
};

} // namespace fin
