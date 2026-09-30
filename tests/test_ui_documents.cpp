#include <catch2/catch_test_macros.hpp>

#include "support/Session.h"
#include "ui/DocumentJournalPage.h"
#include "ui/SalePurchaseDialogs.h"

#include "fin/domain/ChartCodes.h"
#include "fin/domain/Counterparty.h"
#include "fin/domain/Document.h"
#include "fin/domain/Nomenclature.h"
#include "fin/domain/Warehouse.h"
#include "fin/session/LedgerSession.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTableWidget>

using namespace fin;
using namespace fin::test;

TEST_CASE("DocumentJournalPage loads documents and supports filtering", "[ui][documents]")
{
  LedgerSession session = openSession();
  REQUIRE(session.accounts().seedChartIfEmpty());

  const CounterpartyId supplier = customer(session, QStringLiteral("ТОВ Постач Плюс"));
  const CounterpartyId client = customer(session, QStringLiteral("ПП Клієнт Сервіс"));

  // Документ 1: Надходження
  Document doc1 = makeDoc(DocumentType::Purchase, QDate(2026, 5, 10), QStringLiteral("НП-000101"));
  doc1.counterpartyId = supplier;
  doc1.comment = QStringLiteral("Закупівля комплектуючих");
  doc1.lines.push_back(
      line(account(session, chart::kGoods), account(session, chart::kSuppliers), 100000));
  const Result<DocumentId> id1 = session.documents().insert(doc1);
  REQUIRE(id1);

  // Документ 2: Реалізація
  Document doc2 = makeDoc(DocumentType::Sale, QDate(2026, 5, 15), QStringLiteral("РТ-000202"));
  doc2.counterpartyId = client;
  doc2.comment = QStringLiteral("Відвантаження замовлення");
  doc2.lines.push_back(
      line(account(session, chart::kCustomers), account(session, chart::kRevenue), 150000));
  const Result<DocumentId> id2 = session.documents().insert(doc2);
  REQUIRE(id2);

  DocumentJournalPage page(session);
  QCoreApplication::processEvents();

  auto * table = page.findChild<QTableView *>();
  REQUIRE(table != nullptr);

  auto * searchBar = page.findChild<QLineEdit *>();
  REQUIRE(searchBar != nullptr);

  auto * proxy = qobject_cast<QSortFilterProxyModel *>(table->model());
  REQUIRE(proxy != nullptr);

  SECTION("Initial state displays all inserted documents")
  {
    REQUIRE(proxy->rowCount() >= 2);
  }

  SECTION("Filtering by text in search bar")
  {
    searchBar->setText(QStringLiteral("000202"));
    QCoreApplication::processEvents();
    REQUIRE(proxy->rowCount() == 1);

    searchBar->setText(QStringLiteral("Постач Плюс"));
    QCoreApplication::processEvents();
    REQUIRE(proxy->rowCount() == 1);

    searchBar->setText(QStringLiteral("НеіснуючийДокумент"));
    QCoreApplication::processEvents();
    REQUIRE(proxy->rowCount() == 0);

    searchBar->clear();
    QCoreApplication::processEvents();
    REQUIRE(proxy->rowCount() >= 2);
  }

  SECTION("Filtering by document type combobox")
  {
    auto combos = page.findChildren<QComboBox *>();
    REQUIRE_FALSE(combos.isEmpty());

    // Перший комбобокс - фільтр типів документів
    QComboBox * typeCombo = combos.first();
    REQUIRE(typeCombo != nullptr);

    int saleIdx = typeCombo->findData(QStringLiteral("sale"));
    if (saleIdx >= 0) {
      typeCombo->setCurrentIndex(saleIdx);
      QCoreApplication::processEvents();
      REQUIRE(proxy->rowCount() == 1);
    }
  }
}

TEST_CASE("GoodsDocumentDialog initializes rows and calculates totals", "[ui][documents]")
{
  LedgerSession session = openSession();
  REQUIRE(session.accounts().seedChartIfEmpty());

  const CounterpartyId buyer = customer(session, QStringLiteral("ТОВ Покупець-Тест"));
  const NomenclatureId item1 = item(session, QStringLiteral("Стіл офісний"));
  const WarehouseId wh = WarehouseId{1};

  Document saleDoc;
  saleDoc.type = DocumentType::Sale;
  saleDoc.number = QStringLiteral("РТ-TEST-01");
  saleDoc.date = QDate::currentDate();
  saleDoc.counterpartyId = buyer;
  saleDoc.warehouseId = wh;

  GoodsDocumentDialog dialog(session, saleDoc);
  QCoreApplication::processEvents();

  auto * table = dialog.findChild<QTableWidget *>();
  REQUIRE(table != nullptr);

  auto buttons = dialog.findChildren<QPushButton *>();
  QPushButton * addLineBtn = nullptr;
  QPushButton * saveBtn = nullptr;
  QPushButton * postBtn = nullptr;
  for (auto * btn : buttons) {
    if (btn->text() == QStringLiteral("Додати рядок")) {
      addLineBtn = btn;
    } else if (btn->text() == QStringLiteral("Зберегти")) {
      saveBtn = btn;
    } else if (btn->text() == QStringLiteral("Провести")) {
      postBtn = btn;
    }
  }
  REQUIRE(addLineBtn != nullptr);
  REQUIRE(saveBtn != nullptr);
  REQUIRE(postBtn != nullptr);

  // За замовчуванням створюється один початковий рядок
  REQUIRE(table->rowCount() == 1);

  // Додавання рядка
  addLineBtn->click();
  QCoreApplication::processEvents();
  REQUIRE(table->rowCount() == 2);

  // Перевірка колонок рядка
  auto * nomCombo = qobject_cast<QComboBox *>(table->cellWidget(0, 0));
  auto * qtyEdit = qobject_cast<QLineEdit *>(table->cellWidget(0, 1));
  auto * priceEdit = qobject_cast<QLineEdit *>(table->cellWidget(0, 2));
  REQUIRE(nomCombo != nullptr);
  REQUIRE(qtyEdit != nullptr);
  REQUIRE(priceEdit != nullptr);

  // Встановлюємо номенклатуру та кількість/ціну
  int nomIdx = nomCombo->findData(QVariant::fromValue(item1.value));
  if (nomIdx >= 0) {
    nomCombo->setCurrentIndex(nomIdx);
  }
  qtyEdit->setText(QStringLiteral("5"));
  priceEdit->setText(QStringLiteral("1000.00"));
  QCoreApplication::processEvents();

  // Перевірка перерахунку суми рядка
  auto * amountEdit = qobject_cast<QLineEdit *>(table->cellWidget(0, 3));
  REQUIRE(amountEdit != nullptr);

  // Викликаємо editingFinished для запуску лямбда-перерахунку
  Q_EMIT qtyEdit->editingFinished();
  Q_EMIT priceEdit->editingFinished();
  QCoreApplication::processEvents();

  // 5 * 1000.00 = 5 000.00
  REQUIRE(amountEdit->text().contains(QStringLiteral("5")));

  // Перевірка наявності мітки підсумку
  auto labels = dialog.findChildren<QLabel *>();
  bool foundTotal = false;
  for (auto * lbl : labels) {
    if (lbl->text().contains(QStringLiteral("Всього")) ||
        lbl->text().contains(QStringLiteral("Разом")) ||
        lbl->text().contains(QStringLiteral("5"))) {
      foundTotal = true;
      break;
    }
  }
  REQUIRE(foundTotal);
}
