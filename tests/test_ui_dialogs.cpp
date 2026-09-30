#include <catch2/catch_test_macros.hpp>

#include "ui/DocumentDialog.h"
#include "support/Session.h"
#include "fin/session/LedgerSession.h"
#include "fin/db/AccountRepository.h"
#include "fin/db/DocumentRepository.h"
#include "fin/domain/Account.h"
#include "fin/domain/Document.h"

#include <QComboBox>
#include <QDateEdit>
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
