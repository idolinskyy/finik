#include "ui/CompanyDialog.h"

#include "ui/HelpBrowser.h"
#include "ui/UiSupport.h"

#include "fin/auth/Access.h"
#include "fin/domain/BankRequisites.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QVBoxLayout>

namespace fin {

CompanyDialog::CompanyDialog(LedgerSession & session, QWidget * parent)
    : QDialog(parent), session_(session)
{
  setWindowTitle(QStringLiteral("Картка підприємства"));
  setModal(true);
  resize(520, 620);

  const Result<CompanyProfile> profile = session_.company().load();
  CompanyProfile current;
  if (profile) {
    current = profile.value();
  }

  name_ = new QLineEdit(current.name);
  edrpou_ = new QLineEdit(current.edrpou);
  iban_ = new QLineEdit(current.iban);
  mfo_ = new QLineEdit(current.mfo);
  bankName_ = new QLineEdit(current.bankName);
  address_ = new QLineEdit(current.address);
  accountant_ = new QLineEdit(current.accountant);
  cashier_ = new QLineEdit(current.cashier);
  taxSystem_ = new QComboBox;
  taxSystem_->addItem(QStringLiteral("Спрощена система"), static_cast<int>(TaxSystem::Simplified));
  taxSystem_->addItem(QStringLiteral("Загальна система"), static_cast<int>(TaxSystem::General));
  taxSystem_->setCurrentIndex(current.taxSystem == TaxSystem::General ? 1 : 0);
  taxRate_ = new QSpinBox;
  taxRate_->setRange(0, 10000);
  taxRate_->setValue(current.singleTaxRateBp);
  taxRate_->setSuffix(QStringLiteral(" б.п. (500 = 5%)"));
  vatPayer_ = new QCheckBox(QStringLiteral("Платник ПДВ"));
  vatPayer_->setChecked(current.vatPayer);
  vatRate_ = new QSpinBox;
  vatRate_->setRange(0, 10000);
  // Показуємо ставку як збережено (0 = не задано); для платника без ставки
  // підставляємо базові 20%, щоб картку можна було зберегти.
  vatRate_->setValue(current.vatPayer && current.vatRateBp <= 0 ? 2000 : current.vatRateBp);
  vatRate_->setSpecialValueText(QStringLiteral("не задано"));
  vatRate_->setSuffix(QStringLiteral(" б.п. (2000 = 20%)"));
  vatRate_->setEnabled(current.vatPayer);
  taxOfficeRegion_ = new QSpinBox;
  taxOfficeRegion_->setRange(0, 99);
  taxOfficeRegion_->setValue(current.taxOfficeRegion);
  taxOfficeRegion_->setSpecialValueText(QStringLiteral("не задано"));
  taxOfficeDistrict_ = new QSpinBox;
  taxOfficeDistrict_->setRange(0, 99);
  taxOfficeDistrict_->setValue(current.taxOfficeDistrict);
  taxOfficeDistrict_->setSpecialValueText(QStringLiteral("не задано"));
  taxOfficeCode_ = new QSpinBox;
  taxOfficeCode_->setRange(0, 9999);
  taxOfficeCode_->setValue(current.taxOfficeCode);
  taxOfficeCode_->setSpecialValueText(QStringLiteral("не задано"));
  monobankToken_ = new QLineEdit;
  monobankToken_->setEchoMode(QLineEdit::Password);
  existingMonobankToken_ = current.monobankToken;
  monobankTokenEdited_ = false;
  monobankToken_->setPlaceholderText(
      existingMonobankToken_.isEmpty()
          ? QStringLiteral("Токен доступу до API Monobank")
          : QStringLiteral("Залиште порожнім, щоб не змінювати токен"));

  prroEnabled_ =
      new QCheckBox(QStringLiteral("ПРРО Checkbox: фіскалізувати реалізацію та повернення"));
  prroEnabled_->setChecked(current.prroEnabled);
  checkboxLicense_ = new QLineEdit;
  checkboxLicense_->setEchoMode(QLineEdit::Password);
  existingCheckboxLicense_ = current.checkboxLicenseKey;
  checkboxLicenseEdited_ = false;
  checkboxLicense_->setPlaceholderText(
      existingCheckboxLicense_.isEmpty()
          ? QStringLiteral("License key каси Checkbox")
          : QStringLiteral("Залиште порожнім, щоб не змінювати ключ"));
  checkboxLogin_ = new QLineEdit(current.checkboxCashierLogin);
  checkboxPin_ = new QLineEdit;
  checkboxPin_->setEchoMode(QLineEdit::Password);
  existingCheckboxPin_ = current.checkboxCashierPin;
  checkboxPinEdited_ = false;
  checkboxPin_->setPlaceholderText(existingCheckboxPin_.isEmpty()
                                       ? QStringLiteral("PIN / пароль касира")
                                       : QStringLiteral("Залиште порожнім, щоб не змінювати PIN"));
  prroPayment_ = new QComboBox;
  prroPayment_->addItem(QStringLiteral("Готівка (CASH)"), true);
  prroPayment_->addItem(QStringLiteral("Безготівка (CASHLESS)"), false);
  prroPayment_->setCurrentIndex(current.prroPaymentCash ? 0 : 1);

  name_->setWhatsThis(QStringLiteral("Назва потрапляє на ПКО, ВКО, накладну і касову книгу."));
  edrpou_->setMaxLength(10);
  iban_->setMaxLength(34);
  mfo_->setMaxLength(6);

  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Назва"), name_);
  form->addRow(QStringLiteral("ЄДРПОУ"), edrpou_);
  form->addRow(QStringLiteral("IBAN"), iban_);
  form->addRow(QStringLiteral("МФО"), mfo_);
  form->addRow(QStringLiteral("Банк"), bankName_);
  form->addRow(QStringLiteral("Адреса"), address_);
  form->addRow(QStringLiteral("Бухгалтер"), accountant_);
  form->addRow(QStringLiteral("Касир"), cashier_);
  form->addRow(QStringLiteral("Система оподаткування"), taxSystem_);
  form->addRow(QStringLiteral("Єдиний податок"), taxRate_);
  form->addRow(vatPayer_);
  form->addRow(QStringLiteral("Ставка ПДВ"), vatRate_);
  form->addRow(QStringLiteral("ДПІ: регіон (C_REG)"), taxOfficeRegion_);
  form->addRow(QStringLiteral("ДПІ: район (C_RAJ)"), taxOfficeDistrict_);
  form->addRow(QStringLiteral("ДПІ: код (C_STI_ORIG)"), taxOfficeCode_);
  form->addRow(QStringLiteral("Monobank Токен"), monobankToken_);
  form->addRow(prroEnabled_);
  form->addRow(QStringLiteral("Checkbox License Key"), checkboxLicense_);
  form->addRow(QStringLiteral("Checkbox логін касира"), checkboxLogin_);
  form->addRow(QStringLiteral("Checkbox PIN"), checkboxPin_);
  form->addRow(QStringLiteral("Тип оплати в чеку"), prroPayment_);

  auto * buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  saveBtn_ = buttons->button(QDialogButtonBox::Save);
  if (saveBtn_) {
    saveBtn_->setIcon(ui::themeIcon(QStringLiteral(":/help/icons/post.svg")));
    saveBtn_->setText(QStringLiteral("Зберегти"));
  }
  if (auto * cancelBtn = buttons->button(QDialogButtonBox::Cancel)) {
    cancelBtn->setIcon(ui::themeIcon(QStringLiteral(":/help/icons/unpost.svg")));
    cancelBtn->setText(QStringLiteral("Скасувати"));
  }
  reason_ = new QLabel;
  reason_->setWordWrap(true);

  auto * layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(reason_);
  layout->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::accepted, this, &CompanyDialog::save);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(name_, &QLineEdit::textChanged, this, &CompanyDialog::refreshGate);
  connect(vatPayer_, &QCheckBox::toggled, this, [this](bool on) {
    if (on && vatRate_->value() <= 0) {
      vatRate_->setValue(2000);
    }
    refreshGate();
  });
  connect(prroEnabled_, &QCheckBox::toggled, this, &CompanyDialog::refreshGate);
  connect(monobankToken_, &QLineEdit::textEdited, this, [this]() { monobankTokenEdited_ = true; });
  connect(checkboxLicense_, &QLineEdit::textEdited, this,
          [this]() { checkboxLicenseEdited_ = true; });
  connect(checkboxPin_, &QLineEdit::textEdited, this, [this]() { checkboxPinEdited_ = true; });
  connect(iban_, &QLineEdit::editingFinished, this, [this]() {
    if (mfo_->text().trimmed().isEmpty()) {
      mfo_->setText(mfoFromIban(iban_->text()));
    }
  });
  auto * f1 = new QShortcut(QKeySequence::HelpContents, this);
  connect(f1, &QShortcut::activated, this,
          [this]() { HelpBrowser::openTopic(this, QStringLiteral("company.html")); });
  if (!profile) {
    ui::showError(this, profile.error());
  }
  refreshGate();
}

void CompanyDialog::save()
{
  CompanyProfile profile;
  profile.name = name_->text();
  profile.edrpou = edrpou_->text();
  profile.iban = iban_->text();
  profile.mfo = mfo_->text();
  profile.bankName = bankName_->text();
  profile.address = address_->text();
  profile.accountant = accountant_->text();
  profile.cashier = cashier_->text();
  profile.taxSystem = static_cast<TaxSystem>(taxSystem_->currentData().toInt());
  profile.singleTaxRateBp = taxRate_->value();
  profile.vatPayer = vatPayer_->isChecked();
  profile.vatRateBp = vatRate_->value();
  profile.taxOfficeRegion = taxOfficeRegion_->value();
  profile.taxOfficeDistrict = taxOfficeDistrict_->value();
  profile.taxOfficeCode = taxOfficeCode_->value();
  if (monobankTokenEdited_) {
    profile.monobankToken = monobankToken_->text().trimmed();
  } else {
    profile.monobankToken = existingMonobankToken_;
  }
  profile.prroEnabled = prroEnabled_->isChecked();
  profile.checkboxCashierLogin = checkboxLogin_->text().trimmed();
  profile.prroPaymentCash = prroPayment_->currentData().toBool();
  if (checkboxLicenseEdited_) {
    profile.checkboxLicenseKey = checkboxLicense_->text().trimmed();
  } else {
    profile.checkboxLicenseKey = existingCheckboxLicense_;
  }
  if (checkboxPinEdited_) {
    profile.checkboxCashierPin = checkboxPin_->text();
  } else {
    profile.checkboxCashierPin = existingCheckboxPin_;
  }
  const Result<CompanyProfile> loaded = session_.company().load();
  if (loaded) {
    profile.allowNegativeStock = loaded.value().allowNegativeStock;
  }
  const Status status = session_.company().save(profile);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  accept();
}

void CompanyDialog::refreshGate()
{
  const Status access = Access::requireEditCompany(session_.handle());
  const bool writable = bool(access);
  name_->setReadOnly(!writable);
  edrpou_->setReadOnly(!writable);
  iban_->setReadOnly(!writable);
  mfo_->setReadOnly(!writable);
  bankName_->setReadOnly(!writable);
  address_->setReadOnly(!writable);
  accountant_->setReadOnly(!writable);
  cashier_->setReadOnly(!writable);
  taxSystem_->setEnabled(writable);
  taxRate_->setEnabled(writable);
  vatPayer_->setEnabled(writable);
  vatRate_->setEnabled(writable && vatPayer_->isChecked());
  taxOfficeRegion_->setEnabled(writable);
  taxOfficeDistrict_->setEnabled(writable);
  taxOfficeCode_->setEnabled(writable);
  monobankToken_->setReadOnly(!writable);
  prroEnabled_->setEnabled(writable);
  const bool prroOn = prroEnabled_->isChecked();
  checkboxLicense_->setReadOnly(!writable);
  checkboxLicense_->setEnabled(writable && prroOn);
  checkboxLogin_->setReadOnly(!writable);
  checkboxLogin_->setEnabled(writable && prroOn);
  checkboxPin_->setReadOnly(!writable);
  checkboxPin_->setEnabled(writable && prroOn);
  prroPayment_->setEnabled(writable && prroOn);
  if (!writable) {
    saveBtn_->setEnabled(false);
    saveBtn_->setToolTip(access.message());
    reason_->setText(access.message());
    return;
  }
  const bool ok = !name_->text().trimmed().isEmpty();
  saveBtn_->setEnabled(ok);
  saveBtn_->setToolTip(ok ? QString() : QStringLiteral("назва підприємства обов'язкова"));
  reason_->setText(ok ? QStringLiteral("Можна зберегти картку.")
                      : QStringLiteral("Назва підприємства обов'язкова."));
}

} // namespace fin
