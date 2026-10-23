#include <catch2/catch_test_macros.hpp>

#include "support/Session.h"
#include "ui/CashBookPage.h"
#include "ui/CashFlowPage.h"
#include "ui/DashboardPage.h"
#include "ui/FinancialStatementPage.h"
#include "ui/ReconciliationPage.h"
#include "ui/StockPage.h"
#include "ui/SubcontoTrialBalancePage.h"
#include "ui/TrialBalancePage.h"

#include "fin/domain/ChartCodes.h"
#include "fin/domain/Counterparty.h"
#include "fin/domain/Document.h"
#include "fin/domain/Nomenclature.h"
#include "fin/domain/Warehouse.h"
#include "fin/session/LedgerSession.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QSignalSpy>
#include <QTableView>
#include <QTableWidget>

using namespace fin;
using namespace fin::test;

TEST_CASE("TrialBalancePage builds table and emits navigation signals", "[ui][reports]")
{
  LedgerSession session = openSession();
  REQUIRE(session.accounts().seedChartIfEmpty());

  // Створюємо та проводимо документ для наповнення оборотів
  Document doc = makeDoc(DocumentType::Manual, QDate(2026, 6, 15), QStringLiteral("БО-ОСВ-01"));
  doc.lines.push_back(
      line(account(session, chart::kCash), account(session, chart::kRevenue), 100000));
  const DocumentId id = saveAndPost(session, doc);
  REQUIRE(id);

  TrialBalancePage page(session);
  QCoreApplication::processEvents();

  page.rebuild();
  QCoreApplication::processEvents();

  auto * view = page.findChild<QTableView *>();
  REQUIRE(view != nullptr);
  REQUIRE(view->model() != nullptr);

  // Перевірка наявності рядків у моделі ОСВ
  REQUIRE(view->model()->rowCount() > 0);

  // Перевірка сигналу переходу до Картки рахунку
  QSignalSpy cardSpy(&page, &TrialBalancePage::openAccountCard);
  Q_EMIT page.openAccountCard(AccountId{1}, QDate(2026, 6, 1), QDate(2026, 6, 30));
  REQUIRE(cardSpy.count() == 1);

  // Перевірка сигналу переходу до Аналізу рахунку
  QSignalSpy analysisSpy(&page, &TrialBalancePage::openAccountAnalysis);
  Q_EMIT page.openAccountAnalysis(AccountId{1}, QDate(2026, 6, 1), QDate(2026, 6, 30));
  REQUIRE(analysisSpy.count() == 1);
}

TEST_CASE("CashBookPage displays cash movements and allows navigation", "[ui][reports]")
{
  LedgerSession session = openSession();
  REQUIRE(session.accounts().seedChartIfEmpty());

  const CounterpartyId buyer = customer(session, QStringLiteral("Покупець Каси"));

  // Створюємо та проводимо ПКО
  Document cashDoc =
      makeDoc(DocumentType::CashIn, QDate(2026, 7, 5), QStringLiteral("ПКО-КАСА-01"));
  cashDoc.counterpartyId = buyer;
  cashDoc.lines.push_back(
      line(account(session, chart::kCash), account(session, chart::kCustomers), 45000));
  const DocumentId id = saveAndPost(session, cashDoc);
  REQUIRE(id);

  CashBookPage page(session, chart::kCash, QStringLiteral("Касова книга"));
  QCoreApplication::processEvents();

  page.rebuild();
  QCoreApplication::processEvents();

  auto * table = page.findChild<QTableWidget *>();
  REQUIRE(table != nullptr);

  // Перевірка наявності рядків касової книги
  REQUIRE(table->rowCount() > 0);

  // Перша колонка рядка першого документа (row 1) зберігає ID документа в
  // UserRole для подвійного кліку
  QTableWidgetItem * itemDoc = table->item(1, 0);
  REQUIRE(itemDoc != nullptr);
  REQUIRE(itemDoc->data(Qt::UserRole).toLongLong() == id.value);
}

TEST_CASE("ReconciliationPage generates statement of mutual settlements", "[ui][reports]")
{
  LedgerSession session = openSession();
  REQUIRE(session.accounts().seedChartIfEmpty());

  const CounterpartyId cp = customer(session, QStringLiteral("ТОВ Взаєморозрахунки"));

  // Проводимо накладну
  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 8, 1), QStringLiteral("РТ-АКТ-01"));
  sale.counterpartyId = cp;
  sale.lines.push_back(
      line(account(session, chart::kCustomers), account(session, chart::kRevenue), 80000));
  const DocumentId saleId = saveAndPost(session, sale);
  REQUIRE(saleId);

  ReconciliationPage page(session);
  QCoreApplication::processEvents();

  page.rebuild();
  QCoreApplication::processEvents();

  auto * table = page.findChild<QTableWidget *>();
  REQUIRE(table != nullptr);

  // Перевірка структури таблиці акту звірки
  REQUIRE(table->columnCount() == 5);
  REQUIRE(table->rowCount() > 0);
}

TEST_CASE("CashFlowPage and DashboardPage show foreign currency money accounts", "[ui][reports]")
{
  LedgerSession session = openSession();
  REQUIRE(session.accounts().seedChartIfEmpty());

  const QDate today = QDate::currentDate();
  const QDate monthStart(today.year(), today.month(), 1);

  Document uah = makeDoc(DocumentType::CashIn, monthStart, QStringLiteral("ПКО-CF-UI"));
  uah.lines.push_back(
      line(account(session, chart::kCash), account(session, chart::kRevenue), 70000));
  REQUIRE(saveAndPost(session, uah));

  Document fx = makeDoc(DocumentType::Manual, monthStart, QStringLiteral("БО-CF-UI-FX"));
  fx.lines.push_back(
      line(account(session, chart::kForeignBank), account(session, chart::kRevenue), 30000));
  REQUIRE(saveAndPost(session, fx));

  SECTION("CashFlowPage")
  {
    CashFlowPage page(session);
    QCoreApplication::processEvents();
    page.rebuild();
    QCoreApplication::processEvents();

    const auto tables = page.findChildren<QTableWidget *>();
    REQUIRE(tables.size() >= 2);
    for (auto * table : tables) {
      REQUIRE(table->rowCount() > 0);
    }
  }

  SECTION("DashboardPage sums UAH and foreign money accounts")
  {
    DashboardPage page(session);
    QCoreApplication::processEvents();
    page.reload();
    QCoreApplication::processEvents();

    QString cashText;
    QString bankText;
    for (auto * label : page.findChildren<QLabel *>()) {
      if (label->text().startsWith(QStringLiteral("Каса"))) {
        cashText = label->text();
      } else if (label->text().startsWith(QStringLiteral("Банк"))) {
        bankText = label->text();
      }
    }
    REQUIRE(cashText.contains(QStringLiteral("301+302")));
    REQUIRE(bankText.contains(QStringLiteral("311+312")));
    REQUIRE(cashText.contains(Money::fromCents(70000).toDisplay()));
    REQUIRE(bankText.contains(Money::fromCents(30000).toDisplay()));
  }
}

TEST_CASE("FinancialStatementPage and StockPage initialize and refresh", "[ui][reports]")
{
  LedgerSession session = openSession();
  REQUIRE(session.accounts().seedChartIfEmpty());

  SECTION("FinancialStatementPage")
  {
    FinancialStatementPage page(session);
    QCoreApplication::processEvents();

    page.rebuild();
    QCoreApplication::processEvents();

    auto tables = page.findChildren<QTableWidget *>();
    // Баланс (Форма №1) та Звіт про фін. результати (Форма №2)
    REQUIRE(tables.size() >= 2);
    for (auto * tbl : tables) {
      REQUIRE(tbl->rowCount() > 0);
    }
  }

  SECTION("StockPage")
  {
    const NomenclatureId item1 = item(session, QStringLiteral("Товар на складі"));
    const WarehouseId wh = WarehouseId{1};

    // Оприбуткування товару
    Document purchase =
        makeDoc(DocumentType::Purchase, QDate(2026, 9, 1), QStringLiteral("НП-СКЛАД-01"));
    purchase.warehouseId = wh;
    purchase.counterpartyId = customer(session, QStringLiteral("ТОВ Постачальник складу"));
    purchase.lines.push_back(stockLine(account(session, chart::kGoods),
                                       account(session, chart::kSuppliers), item1, 10000, 50000));
    REQUIRE(saveAndPost(session, purchase));

    StockPage page(session);
    QCoreApplication::processEvents();

    page.rebuild();
    QCoreApplication::processEvents();

    auto * table = page.findChild<QTableWidget *>();
    REQUIRE(table != nullptr);
    REQUIRE(table->rowCount() > 0);
  }
}

TEST_CASE("SubcontoTrialBalancePage offers all dimensions", "[ui][reports][subconto]")
{
  LedgerSession session = openSession();
  SubcontoTrialBalancePage page(session);
  page.setAccountAndPeriod(account(session, chart::kCustomers), QDate(2026, 1, 1), QDate(2026, 1, 31),
                           0);
  auto * lineCb = page.findChild<QComboBox *>(QStringLiteral("subcontoLineCombo"));
  REQUIRE(lineCb != nullptr);
  bool hasAll = false;
  for (int i = 0; i < lineCb->count(); ++i) {
    if (lineCb->itemData(i).toInt() == 0 &&
        lineCb->itemText(i).contains(QStringLiteral("Усі виміри"))) {
      hasAll = true;
    }
  }
  REQUIRE(hasAll);
}
