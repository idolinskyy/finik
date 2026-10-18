#pragma once

#include "fin/session/LedgerSession.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace fin {

class CompanyDialog : public QDialog
{
  Q_OBJECT

 public:
  CompanyDialog(LedgerSession & session, QWidget * parent = nullptr);

 private:
  void save();
  void refreshGate();

  LedgerSession & session_;
  QLineEdit * name_ = nullptr;
  QLineEdit * edrpou_ = nullptr;
  QLineEdit * iban_ = nullptr;
  QLineEdit * mfo_ = nullptr;
  QLineEdit * bankName_ = nullptr;
  QLineEdit * address_ = nullptr;
  QLineEdit * accountant_ = nullptr;
  QLineEdit * cashier_ = nullptr;
  QComboBox * taxSystem_ = nullptr;
  QSpinBox * taxRate_ = nullptr;
  QCheckBox * vatPayer_ = nullptr;
  QSpinBox * vatRate_ = nullptr;
  QSpinBox * taxOfficeRegion_ = nullptr;
  QSpinBox * taxOfficeDistrict_ = nullptr;
  QSpinBox * taxOfficeCode_ = nullptr;
  QLineEdit * monobankToken_ = nullptr;
  bool monobankTokenEdited_ = false;
  QString existingMonobankToken_;
  QCheckBox * prroEnabled_ = nullptr;
  QLineEdit * checkboxLicense_ = nullptr;
  QLineEdit * checkboxLogin_ = nullptr;
  QLineEdit * checkboxPin_ = nullptr;
  QComboBox * prroPayment_ = nullptr;
  bool checkboxLicenseEdited_ = false;
  bool checkboxPinEdited_ = false;
  QString existingCheckboxLicense_;
  QString existingCheckboxPin_;
  QLabel * reason_ = nullptr;
  QPushButton * saveBtn_ = nullptr;
};

} // namespace fin
