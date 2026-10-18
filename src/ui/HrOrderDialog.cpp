#include "ui/HrOrderDialog.h"

#include "fin/print/PrintForms.h"
#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include <QComboBox>
#include <QDateEdit>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace fin {

HrOrderDialog::HrOrderDialog(LedgerSession & session, Document document, QWidget * parent)
    : QDialog(parent), session_(session), document_(std::move(document))
{
  setWindowTitle(QStringLiteral("Кадровий наказ"));
  resize(500, 400);

  auto * layout = new QVBoxLayout(this);
  auto * form = new QFormLayout;

  docNumber_ = new QLineEdit(document_.number);
  docDate_ = new QDateEdit(document_.date);
  docDate_->setCalendarPopup(true);
  form->addRow(QStringLiteral("Номер документа:"), docNumber_);
  form->addRow(QStringLiteral("Дата документа:"), docDate_);

  employeeCombo_ = new QComboBox;
  const Result<std::vector<Employee>> emps = session_.employees().listAll();
  if (emps) {
    employees_ = emps.value();
    for (const Employee & e : employees_) {
      employeeCombo_->addItem(e.name, QVariant::fromValue(static_cast<qlonglong>(e.id.value)));
    }
  }
  form->addRow(QStringLiteral("Працівник:"), employeeCombo_);

  orderTypeCombo_ = new QComboBox;
  orderTypeCombo_->addItem(QStringLiteral("Прийом на роботу"), static_cast<int>(HrOrderType::Hire));
  orderTypeCombo_->addItem(QStringLiteral("Переведення"), static_cast<int>(HrOrderType::Transfer));
  orderTypeCombo_->addItem(QStringLiteral("Звільнення"), static_cast<int>(HrOrderType::Dismiss));
  form->addRow(QStringLiteral("Тип наказу:"), orderTypeCombo_);

  effectiveDate_ = new QDateEdit(QDate::currentDate());
  effectiveDate_->setCalendarPopup(true);
  form->addRow(QStringLiteral("Діє з (дата):"), effectiveDate_);

  position_ = new QLineEdit;
  department_ = new QLineEdit;
  salary_ = new QLineEdit;

  form->addRow(QStringLiteral("Посада:"), position_);
  form->addRow(QStringLiteral("Підрозділ:"), department_);
  form->addRow(QStringLiteral("Оклад (грн):"), salary_);

  layout->addLayout(form);
  layout->addStretch();

  auto * btns = new QHBoxLayout;
  printBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/print.svg")),
                              QStringLiteral("Друк наказу"));
  printBtn_->setEnabled(false);
  saveBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/post.svg")),
                             QStringLiteral("Зберегти"));
  saveBtn_->setDefault(true);
  auto * cancelBtn = new QPushButton(QStringLiteral("Закрити"));

  btns->addWidget(printBtn_);
  btns->addStretch();
  btns->addWidget(saveBtn_);
  btns->addWidget(cancelBtn);
  layout->addLayout(btns);

  connect(orderTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &HrOrderDialog::updateVisibility);
  connect(saveBtn_, &QPushButton::clicked, this, &HrOrderDialog::saveAndPost);
  connect(printBtn_, &QPushButton::clicked, this, &HrOrderDialog::printForm);
  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

  if (document_.id) {
    Result<HrOrder> order = session_.hrOrders().findByDocument(document_.id);
    if (order) {
      hrOrderId_ = order.value().id;
      effectiveDate_->setDate(order.value().effectiveDate);
      orderTypeCombo_->setCurrentIndex(
          orderTypeCombo_->findData(static_cast<int>(order.value().orderType)));
      position_->setText(order.value().newPosition);
      department_->setText(order.value().newDepartment);
      salary_->setText(order.value().newSalary.toDisplay());

      int empIdx = employeeCombo_->findData(static_cast<qlonglong>(order.value().employeeId.value));
      if (empIdx >= 0)
        employeeCombo_->setCurrentIndex(empIdx);

      printBtn_->setEnabled(true);
    }
  }

  updateVisibility();
}

void HrOrderDialog::updateVisibility()
{
  const auto type = static_cast<HrOrderType>(orderTypeCombo_->currentData().toInt());
  const bool hasDetails = (type == HrOrderType::Hire || type == HrOrderType::Transfer);
  position_->setEnabled(hasDetails);
  department_->setEnabled(hasDetails);
  salary_->setEnabled(hasDetails);
}

void HrOrderDialog::saveAndPost()
{
  if (employeeCombo_->currentIndex() < 0)
    return;

  document_.number = docNumber_->text().trimmed();
  document_.date = docDate_->date();
  // Додаємо фіктивний рядок проводок, щоб пройти базову валідацію Manual
  // документа, якщо потрібно, але оскільки ми використовуємо Manual, можна
  // зберегти документ без проводок.

  if (!document_.id) {
    Result<DocumentId> docId = session_.documents().insert(document_);
    if (!docId) {
      QMessageBox::critical(this, QStringLiteral("Помилка"), docId.error().message);
      return;
    }
    document_.id = docId.value();
  } else {
    Status status = session_.documents().update(document_);
    if (!status) {
      QMessageBox::critical(this, QStringLiteral("Помилка"), status.message());
      return;
    }
  }

  HrOrder order;
  order.id = hrOrderId_;
  order.documentId = document_.id;
  order.employeeId = EmployeeId{employeeCombo_->currentData().toLongLong()};
  order.orderType = static_cast<HrOrderType>(orderTypeCombo_->currentData().toInt());
  order.effectiveDate = effectiveDate_->date();

  if (order.orderType != HrOrderType::Dismiss) {
    order.newPosition = position_->text().trimmed();
    order.newDepartment = department_->text().trimmed();
    Result<Money> m = Money::fromDisplayString(salary_->text());
    if (m)
      order.newSalary = m.value();
  }

  Result<HrOrderId> saved = session_.hrOrders().insert(order);
  if (!saved) {
    QMessageBox::critical(this, QStringLiteral("Помилка"), saved.error().message);
    return;
  }
  hrOrderId_ = saved.value();
  persisted_ = true;
  printBtn_->setEnabled(true);
  ui::showToast(this, QStringLiteral("Кадровий наказ збережено."));
}

void HrOrderDialog::printForm()
{
  if (!document_.id || !hrOrderId_)
    return;
  Result<HrOrder> order = session_.hrOrders().findById(hrOrderId_);
  if (!order)
    return;

  Result<Employee> emp = session_.employees().findById(order.value().employeeId);
  if (!emp)
    return;

  Result<QString> html = PrintForms(session_.handle()).hrOrderHtml(order.value(), emp.value());
  if (html) {
    ui::printHtml(this, QStringLiteral("Кадровий наказ"), html.value());
  }
}

} // namespace fin
