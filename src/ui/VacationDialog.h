#pragma once

#include "fin/domain/Employee.h"
#include "fin/session/LedgerSession.h"

#include <optional>

#include <QDialog>

class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;

namespace fin {

class VacationDialog : public QDialog
{
  Q_OBJECT

 public:
  VacationDialog(LedgerSession & session, Document document, QWidget * parent = nullptr);

  bool persisted() const { return persisted_; }

 private:
  void compute();
  void saveAndPost();
  void unpost();
  void printForm();
  void updateUi();
  void refreshTimesheetHint();

  LedgerSession & session_;
  Document document_;
  bool persisted_ = false;
  std::optional<VacationCalc> lastCalc_;
  std::optional<Employee> lastEmployee_;

  QLineEdit * docNumber_ = nullptr;
  QDateEdit * docDate_ = nullptr;
  QComboBox * employeeCombo_ = nullptr;
  QDateEdit * fromDate_ = nullptr;
  QDateEdit * toDate_ = nullptr;
  QLabel * hint_ = nullptr;
  QLabel * timesheetHint_ = nullptr;

  QTextEdit * resultText_ = nullptr;
  QPushButton * computeBtn_ = nullptr;
  QPushButton * saveBtn_ = nullptr;
  QPushButton * unpostBtn_ = nullptr;
  QPushButton * printBtn_ = nullptr;

  std::vector<Employee> employees_;
  std::vector<Account> accounts_;
};

} // namespace fin
