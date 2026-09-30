#pragma once

#include "fin/session/LedgerSession.h"
#include "fin/domain/Employee.h"

#include <QDialog>

class QComboBox;
class QDateEdit;
class QLineEdit;
class QPushButton;

namespace fin {

class HrOrderDialog : public QDialog
{
  Q_OBJECT

 public:
  HrOrderDialog(LedgerSession & session, Document document, QWidget * parent = nullptr);

  bool persisted() const { return persisted_; }

 private:
  void saveAndPost();
  void printForm();
  void updateVisibility();

  LedgerSession & session_;
  Document document_;
  bool persisted_ = false;
  HrOrderId hrOrderId_;

  QLineEdit * docNumber_ = nullptr;
  QDateEdit * docDate_ = nullptr;
  QComboBox * employeeCombo_ = nullptr;
  QComboBox * orderTypeCombo_ = nullptr;
  QDateEdit * effectiveDate_ = nullptr;

  QLineEdit * position_ = nullptr;
  QLineEdit * department_ = nullptr;
  QLineEdit * salary_ = nullptr;

  QPushButton * saveBtn_ = nullptr;
  QPushButton * printBtn_ = nullptr;

  std::vector<Employee> employees_;
};

} // namespace fin
