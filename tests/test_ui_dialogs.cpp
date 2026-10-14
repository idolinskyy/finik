#include <catch2/catch_test_macros.hpp>

#include "ui/CashExpenseDialogs.h"
#include "ui/DocumentDialog.h"
#include "ui/TypedDialogs.h"
#include "ui/UiSupport.h"
#include "support/Session.h"
#include "fin/session/LedgerSession.h"
#include "fin/db/AccountRepository.h"
#include "fin/db/DocumentRepository.h"
#include "fin/domain/Account.h"
#include "fin/domain/Contract.h"
#include "fin/domain/Document.h"
#include "fin/domain/Employee.h"

#include <QComboBox>
#include <QDateEdit>
#include <QVariant>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QMessageBox>
#include <QApplication>
#include <QTimer>

using namespace fin;

TEST_CASE("DocumentDialog validation and saving", "[ui][dialogs]")
{
  auto session = test::openSession();

  // Ініціалізуємо план рахунків
  REQUIRE(session.accounts().seedChartIfEmpty());

  auto acc311Res = session.accounts().findByCode(QStringLiteral("311"));
  REQUIRE(acc311Res.hasValue());
  AccountId bankAccId = acc311Res.value().id;

  auto acc361Res = session.accounts().findByCode(QStringLiteral("361"));
  REQUIRE(acc361Res.hasValue());
  AccountId customerAccId = acc361Res.value().id;

  // 361 має обов'язкове субконто «контрагент» — потрібен хтось для вибору в діалозі
  const QString kCounterpartyName = QStringLiteral("ТОВ Аналітика");
  REQUIRE(test::customer(session, kCounterpartyName));

  Document doc;
  doc.type = DocumentType::Manual;
  doc.number = QStringLiteral("MAN-1");
  doc.date = QDate::currentDate();

  DocumentDialog dialog(session, doc);
  QCoreApplication::processEvents();

  auto * lines = dialog.findChild<QTableWidget *>();
  REQUIRE(lines != nullptr);

  auto buttons = dialog.findChildren<QPushButton *>();
  QPushButton * postBtn = nullptr;
  QPushButton * addLineBtn = nullptr;
  for (auto * btn : buttons) {
    if (btn->text() == QStringLiteral("Провести")) {
      postBtn = btn;
    } else if (btn->text() == QStringLiteral("Додати рядок")) {
      addLineBtn = btn;
    }
  }
  REQUIRE(postBtn != nullptr);
  REQUIRE(addLineBtn != nullptr);

  SECTION("Invalid document has disabled Post button")
  {
    lines->setRowCount(0); // Очистимо рядки
    addLineBtn->click();
    QCoreApplication::processEvents();

    // Заповнимо тільки суму, без рахунків -> помилка валідації (незбалансована проводка)
    auto * amountEdit = qobject_cast<QLineEdit *>(lines->cellWidget(0, 5));
    REQUIRE(amountEdit != nullptr);
    amountEdit->setText(QStringLiteral("100.00"));
    QCoreApplication::processEvents();

    // Кнопка проведення має бути вимкнена
    REQUIRE(postBtn->isEnabled() == false);
  }

  SECTION("Valid document can be posted")
  {
    lines->setRowCount(0);
    addLineBtn->click();
    QCoreApplication::processEvents();

    REQUIRE(lines->rowCount() == 1);

    auto * debitCombo = qobject_cast<QComboBox *>(lines->cellWidget(0, 0));
    auto * creditCombo = qobject_cast<QComboBox *>(lines->cellWidget(0, 1));
    auto * amountEdit = qobject_cast<QLineEdit *>(lines->cellWidget(0, 5));

    REQUIRE(debitCombo != nullptr);
    REQUIRE(creditCombo != nullptr);
    REQUIRE(amountEdit != nullptr);

    int debitIdx = debitCombo->findData(QVariant::fromValue(bankAccId.value));
    int creditIdx = creditCombo->findData(QVariant::fromValue(customerAccId.value));
    REQUIRE(debitIdx >= 0);
    REQUIRE(creditIdx >= 0);

    debitCombo->setCurrentIndex(debitIdx);
    creditCombo->setCurrentIndex(creditIdx);
    amountEdit->setText(QStringLiteral("1000.00"));
    QCoreApplication::processEvents();

    // 361 має обов'язкове субконто «контрагент»: без нього проведення заблоковано
    REQUIRE(postBtn->isEnabled() == false);

    QComboBox * cpCombo = nullptr;
    for (auto * combo : dialog.findChildren<QComboBox *>()) {
      if (combo->findText(kCounterpartyName) >= 0) {
        cpCombo = combo;
        break;
      }
    }
    REQUIRE(cpCombo != nullptr);
    cpCombo->setCurrentIndex(cpCombo->findText(kCounterpartyName));
    QCoreApplication::processEvents();

    // Кнопка має бути доступна для проведення
    REQUIRE(postBtn->isEnabled() == true);

    postBtn->click();
    QCoreApplication::processEvents();

    // Тепер документ має бути збережений і проведений
    REQUIRE(dialog.persisted() == true);
  }
}

TEST_CASE("DocumentDialog header has contract and employee for subconto", "[ui][dialogs]")
{
  auto session = test::openSession();
  REQUIRE(session.accounts().seedChartIfEmpty());
  const CounterpartyId cp = test::customer(session, QStringLiteral("ТОВ Субконто"));
  Contract contract;
  contract.counterpartyId = cp;
  contract.number = QStringLiteral("SUB-1");
  contract.name = QStringLiteral("Договір субконто");
  contract.date = QDate(2026, 1, 1);
  const Result<ContractId> cid = session.contracts().insert(contract);
  REQUIRE(cid);
  Employee employee;
  employee.taxId = QStringLiteral("1111111111");
  employee.name = QStringLiteral("Коваленко Субконто");
  employee.hiredOn = QDate(2026, 1, 1);
  employee.salary = Money::fromCents(1000000);
  employee.isActive = true;
  const Result<EmployeeId> eid = session.employees().insert(employee);
  REQUIRE(eid);

  Document doc;
  doc.type = DocumentType::Manual;
  doc.number = QStringLiteral("MAN-SUB");
  doc.date = QDate(2026, 3, 1);
  doc.counterpartyId = cp;
  doc.contractId = cid.value();
  doc.employeeId = eid.value();

  DocumentDialog dialog(session, doc);
  QCoreApplication::processEvents();
  auto * contractCombo = dialog.findChild<QComboBox *>(QStringLiteral("contractCombo"));
  auto * employeeCombo = dialog.findChild<QComboBox *>(QStringLiteral("employeeCombo"));
  REQUIRE(contractCombo != nullptr);
  REQUIRE(employeeCombo != nullptr);
  REQUIRE(ui::selectedContract(contractCombo) == cid.value());
  REQUIRE(ui::selectedEmployee(employeeCombo) == eid.value());

  QPushButton * saveBtn = nullptr;
  for (auto * btn : dialog.findChildren<QPushButton *>()) {
    if (btn->text() == QStringLiteral("Зберегти")) {
      saveBtn = btn;
      break;
    }
  }
  REQUIRE(saveBtn != nullptr);
  saveBtn->click();
  QCoreApplication::processEvents();
  REQUIRE(dialog.persisted() == true);
  const Result<Document> stored =
      session.documents().findByTypeAndNumber(DocumentType::Manual, QStringLiteral("MAN-SUB"));
  REQUIRE(stored);
  REQUIRE(stored.value().contractId == cid.value());
  REQUIRE(stored.value().employeeId == eid.value());
  REQUIRE(stored.value().counterpartyId == cp);

  doc.posted = true;
  DocumentDialog posted(session, doc);
  QCoreApplication::processEvents();
  auto * postedContract = posted.findChild<QComboBox *>(QStringLiteral("contractCombo"));
  auto * postedEmployee = posted.findChild<QComboBox *>(QStringLiteral("employeeCombo"));
  REQUIRE(postedContract != nullptr);
  REQUIRE(postedEmployee != nullptr);
  REQUIRE(postedContract->isEnabled() == false);
  REQUIRE(postedEmployee->isEnabled() == false);
}

TEST_CASE("BankDocumentDialog header has contract for subconto", "[ui][dialogs][bank]")
{
  auto session = test::openSession();
  REQUIRE(session.accounts().seedChartIfEmpty());
  const CounterpartyId cp = test::customer(session, QStringLiteral("ТОВ Банк-договір"));
  Contract contract;
  contract.counterpartyId = cp;
  contract.number = QStringLiteral("BNK-1");
  contract.name = QStringLiteral("Договір банку");
  contract.date = QDate(2026, 1, 1);
  const Result<ContractId> cid = session.contracts().insert(contract);
  REQUIRE(cid);

  Document doc;
  doc.type = DocumentType::BankPayment;
  doc.number = QStringLiteral("BNK-SUB");
  doc.date = QDate(2026, 3, 1);
  doc.counterpartyId = cp;
  doc.contractId = cid.value();

  BankDocumentDialog dialog(session, doc);
  QCoreApplication::processEvents();
  auto * contractCombo = dialog.findChild<QComboBox *>(QStringLiteral("contractCombo"));
  REQUIRE(contractCombo != nullptr);
  REQUIRE(ui::selectedContract(contractCombo) == cid.value());

  auto * table = dialog.findChild<QTableWidget *>();
  REQUIRE(table != nullptr);
  REQUIRE(table->rowCount() >= 1);
  auto * amountEdit = qobject_cast<QLineEdit *>(table->cellWidget(0, 1));
  REQUIRE(amountEdit != nullptr);
  amountEdit->setText(QStringLiteral("100.00"));
  QCoreApplication::processEvents();

  QPushButton * saveBtn = nullptr;
  for (auto * btn : dialog.findChildren<QPushButton *>()) {
    if (btn->text() == QStringLiteral("Зберегти")) {
      saveBtn = btn;
      break;
    }
  }
  REQUIRE(saveBtn != nullptr);
  saveBtn->click();
  QCoreApplication::processEvents();
  REQUIRE(dialog.persisted() == true);
  const Result<Document> stored =
      session.documents().findByTypeAndNumber(DocumentType::BankPayment, QStringLiteral("BNK-SUB"));
  REQUIRE(stored);
  REQUIRE(stored.value().contractId == cid.value());
  REQUIRE(stored.value().counterpartyId == cp);

  doc.posted = true;
  BankDocumentDialog posted(session, doc);
  QCoreApplication::processEvents();
  auto * postedContract = posted.findChild<QComboBox *>(QStringLiteral("contractCombo"));
  REQUIRE(postedContract != nullptr);
  REQUIRE(postedContract->isEnabled() == false);
}

TEST_CASE("CashDocumentDialog header has contract for subconto", "[ui][dialogs][cash]")
{
  auto session = test::openSession();
  REQUIRE(session.accounts().seedChartIfEmpty());
  const AccountId customers = test::account(session, "361");
  const CounterpartyId cp = test::customer(session, QStringLiteral("ТОВ Каса-договір"));
  Contract contract;
  contract.counterpartyId = cp;
  contract.number = QStringLiteral("CSH-1");
  contract.name = QStringLiteral("Договір каси");
  contract.date = QDate(2026, 1, 1);
  const Result<ContractId> cid = session.contracts().insert(contract);
  REQUIRE(cid);

  Document doc;
  doc.type = DocumentType::CashIn;
  doc.number = QStringLiteral("CSH-SUB");
  doc.date = QDate(2026, 3, 1);
  doc.counterpartyId = cp;
  doc.contractId = cid.value();

  CashDocumentDialog dialog(session, doc);
  QCoreApplication::processEvents();
  auto * contractCombo = dialog.findChild<QComboBox *>(QStringLiteral("contractCombo"));
  REQUIRE(contractCombo != nullptr);
  REQUIRE(ui::selectedContract(contractCombo) == cid.value());

  auto * table = dialog.findChild<QTableWidget *>();
  REQUIRE(table != nullptr);
  REQUIRE(table->rowCount() >= 1);
  auto * accountCombo = qobject_cast<QComboBox *>(table->cellWidget(0, 0));
  auto * amountEdit = qobject_cast<QLineEdit *>(table->cellWidget(0, 1));
  REQUIRE(accountCombo != nullptr);
  REQUIRE(amountEdit != nullptr);
  const int accIdx = accountCombo->findData(QVariant::fromValue(static_cast<qlonglong>(customers.value)));
  REQUIRE(accIdx >= 0);
  accountCombo->setCurrentIndex(accIdx);
  amountEdit->setText(QStringLiteral("100.00"));
  QCoreApplication::processEvents();

  QPushButton * saveBtn = nullptr;
  for (auto * btn : dialog.findChildren<QPushButton *>()) {
    if (btn->text() == QStringLiteral("Зберегти")) {
      saveBtn = btn;
      break;
    }
  }
  REQUIRE(saveBtn != nullptr);
  saveBtn->click();
  QCoreApplication::processEvents();
  REQUIRE(dialog.persisted() == true);
  const Result<Document> stored =
      session.documents().findByTypeAndNumber(DocumentType::CashIn, QStringLiteral("CSH-SUB"));
  REQUIRE(stored);
  REQUIRE(stored.value().contractId == cid.value());
  REQUIRE(stored.value().counterpartyId == cp);

  doc.posted = true;
  CashDocumentDialog posted(session, doc);
  QCoreApplication::processEvents();
  auto * postedContract = posted.findChild<QComboBox *>(QStringLiteral("contractCombo"));
  REQUIRE(postedContract != nullptr);
  REQUIRE(postedContract->isEnabled() == false);
}
