#include <catch2/catch_test_macros.hpp>

#include "fin/db/UserRepository.h"
#include "fin/domain/Subconto.h"
#include "fin/domain/User.h"
#include "fin/session/LedgerSession.h"
#include "support/Session.h"
#include "ui/AccountPage.h"
#include "ui/CounterpartyPage.h"
#include "ui/NomenclaturePage.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>

using namespace fin;

TEST_CASE("CounterpartyPage search functionality", "[ui][directories]")
{
  LedgerSession session = fin::test::openSession();

  Counterparty cp1;
  cp1.name = QStringLiteral("ТОВ Сільпо");
  (void)session.counterparties().insert(cp1);

  Counterparty cp2;
  cp2.name = QStringLiteral("ФОП Іваненко");
  (void)session.counterparties().insert(cp2);

  CounterpartyPage page(session);
  // Give it a moment for the model to load
  QCoreApplication::processEvents();

  auto * table = page.findChild<QTableView *>();
  REQUIRE(table != nullptr);

  auto * searchBar = page.findChild<QLineEdit *>();
  REQUIRE(searchBar != nullptr);

  auto * proxy = qobject_cast<QSortFilterProxyModel *>(table->model());
  REQUIRE(proxy != nullptr);

  SECTION("Initial state shows all")
  {
    REQUIRE(proxy->rowCount() == 2);
  }

  SECTION("Search reduces visible rows")
  {
    searchBar->setText(QStringLiteral("Сільпо"));
    QCoreApplication::processEvents();

    REQUIRE(proxy->rowCount() == 1);

    // Clear search
    searchBar->clear();
    QCoreApplication::processEvents();
    REQUIRE(proxy->rowCount() == 2);
  }
}

TEST_CASE("NomenclaturePage search functionality", "[ui][directories]")
{
  LedgerSession session = fin::test::openSession();

  Nomenclature n1;
  n1.name = QStringLiteral("Цвяхи 100мм");
  (void)session.nomenclature().insert(n1);

  Nomenclature n2;
  n2.name = QStringLiteral("Цегла червона");
  (void)session.nomenclature().insert(n2);

  NomenclaturePage page(session);
  QCoreApplication::processEvents();

  auto * table = page.findChild<QTableView *>();
  REQUIRE(table != nullptr);

  auto * searchBar = page.findChild<QLineEdit *>();
  REQUIRE(searchBar != nullptr);

  auto * proxy = qobject_cast<QSortFilterProxyModel *>(table->model());
  REQUIRE(proxy != nullptr);

  SECTION("Initial state shows all")
  {
    REQUIRE(proxy->rowCount() == 2);
  }

  SECTION("Search reduces visible rows")
  {
    searchBar->setText(QStringLiteral("Цегла"));
    QCoreApplication::processEvents();

    REQUIRE(proxy->rowCount() == 1);
  }
}

TEST_CASE("AccountPage loads the chart and refuses viewer subconto edits", "[ui][directories]")
{
  LedgerSession session = fin::test::openSession();
  AccountPage page(session);
  QCoreApplication::processEvents();

  auto * tree = page.findChild<QTreeWidget *>();
  REQUIRE(tree != nullptr);
  REQUIRE(tree->topLevelItemCount() > 0);

  UserRepository users(session.handle());
  User viewer;
  viewer.login = QStringLiteral("oglyad-acc-page");
  viewer.displayName = viewer.login;
  viewer.role = UserRole::Viewer;
  const Result<UserId> viewerId = users.insert(viewer);
  REQUIRE(viewerId);
  REQUIRE(users.setCurrentUser(viewerId.value()));

  AccountPage viewerPage(session);
  QCoreApplication::processEvents();
  const QList<QLabel *> labels = viewerPage.findChildren<QLabel *>();
  REQUIRE_FALSE(labels.isEmpty());
  bool sawGate = false;
  for (QLabel * label : labels) {
    if (label->text().contains(QStringLiteral("довідники"))) {
      sawGate = true;
    }
  }
  REQUIRE(sawGate);
  const AccountId cash = fin::test::account(session, chart::kCash);
  CHECK_FALSE(
      session.subconto().setForAccount(cash, {{cash, 1, subconto_kinds::Counterparty, true}}));
}
