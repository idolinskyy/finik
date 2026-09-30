#include <catch2/catch_test_macros.hpp>

#include "support/Session.h"
#include "ui/MainWindow.h"
#include "ui/SettingsDialog.h"
#include "ui/UiSupport.h"

#include "fin/domain/ChartCodes.h"
#include "fin/domain/Document.h"
#include "fin/domain/Nomenclature.h"
#include "fin/integrity/IntegrityChecker.h"

#include <QCalendarWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QLineEdit>
#include <QLocale>
#include <QSettings>
#include <QSpinBox>
#include <QTreeWidget>

#include <catch2/catch_approx.hpp>

using namespace fin;
using namespace fin::test;

TEST_CASE("SettingsDialog reads and persists configuration", "[ui][settings]")
{
  LedgerSession session = openSession();
  QSettings settings;
  const QString oldTheme = settings.value(QStringLiteral("ui/theme")).toString();
  const QString oldStyle = settings.value(QStringLiteral("ui/style")).toString();
  const bool oldHideSidebar = settings.value(QStringLiteral("ui/hide_sidebar")).toBool();
  const QString oldPrefixStrat =
      settings.value(QStringLiteral("business/prefix_strategy")).toString();
  const QString oldDocPrefix = settings.value(QStringLiteral("business/doc_prefix")).toString();
  Result<CompanyProfile> oldCompany = session.company().load();
  REQUIRE(oldCompany);

  SECTION("SettingsDialog loads current settings into controls")
  {
    settings.setValue(QStringLiteral("ui/theme"), QStringLiteral("dark"));
    settings.setValue(QStringLiteral("ui/style"), QStringLiteral("windows"));
    settings.setValue(QStringLiteral("ui/hide_sidebar"), true);
    CompanyProfile profile = oldCompany.value();
    profile.allowNegativeStock = true;
    REQUIRE(session.company().save(profile));
    settings.setValue(QStringLiteral("business/prefix_strategy"), QStringLiteral("global"));
    settings.setValue(QStringLiteral("business/doc_prefix"), QStringLiteral("TEST-"));

    SettingsDialog dialog(session);
    QCoreApplication::processEvents();

    auto checkboxes = dialog.findChildren<QCheckBox *>();
    auto combos = dialog.findChildren<QComboBox *>();
    auto lineEdits = dialog.findChildren<QLineEdit *>();

    REQUIRE_FALSE(checkboxes.isEmpty());
    REQUIRE_FALSE(combos.isEmpty());
    REQUIRE_FALSE(lineEdits.isEmpty());

    bool foundPrefix = false;
    for (auto * edit : lineEdits) {
      if (edit->text() == QStringLiteral("TEST-")) {
        foundPrefix = true;
        break;
      }
    }
    REQUIRE(foundPrefix);

    bool foundNegative = false;
    for (auto * cb : checkboxes) {
      if (cb->text().contains(QStringLiteral("від'ємні залишки"))) {
        foundNegative = cb->isChecked();
        break;
      }
    }
    REQUIRE(foundNegative);
  }

  SECTION("SettingsDialog saves updated values upon acceptance")
  {
    SettingsDialog dialog(session);
    QCoreApplication::processEvents();

    auto lineEdits = dialog.findChildren<QLineEdit *>();
    for (auto * edit : lineEdits) {
      if (edit->placeholderText().contains(QStringLiteral("UA-"))) {
        edit->setText(QStringLiteral("NEW-"));
      }
    }

    auto checkboxes = dialog.findChildren<QCheckBox *>();
    for (auto * cb : checkboxes) {
      if (cb->text().contains(QStringLiteral("Приховувати бічну панель"))) {
        cb->setChecked(true);
      } else if (cb->text().contains(QStringLiteral("від'ємні залишки"))) {
        cb->setChecked(true);
      }
    }

    QMetaObject::invokeMethod(&dialog, "acceptChanges");

    REQUIRE(settings.value(QStringLiteral("ui/hide_sidebar")).toBool() == true);
    const Result<CompanyProfile> saved = session.company().load();
    REQUIRE(saved);
    REQUIRE(saved.value().allowNegativeStock);
    REQUIRE(settings.value(QStringLiteral("business/doc_prefix")).toString() ==
            QStringLiteral("NEW-"));
  }

  // Відновлення
  settings.setValue(QStringLiteral("ui/theme"), oldTheme);
  settings.setValue(QStringLiteral("ui/style"), oldStyle);
  settings.setValue(QStringLiteral("ui/hide_sidebar"), oldHideSidebar);
  settings.setValue(QStringLiteral("business/prefix_strategy"), oldPrefixStrat);
  settings.setValue(QStringLiteral("business/doc_prefix"), oldDocPrefix);
  REQUIRE(session.company().save(oldCompany.value()));
}

TEST_CASE("Document numbering strategy respects prefix settings in repository",
          "[settings][numbering]")
{
  LedgerSession session = openSession();
  REQUIRE(session.accounts().seedChartIfEmpty());

  QSettings settings;
  const QString oldStrat = settings.value(QStringLiteral("business/prefix_strategy")).toString();
  const QString oldPrefix = settings.value(QStringLiteral("business/doc_prefix")).toString();

  SECTION("Standard strategy uses document type prefix")
  {
    settings.setValue(QStringLiteral("business/prefix_strategy"), QStringLiteral("standard"));
    settings.setValue(QStringLiteral("business/doc_prefix"), QStringLiteral(""));

    const Result<QString> nextNum = session.documents().allocateNumber(DocumentType::Sale);
    REQUIRE(nextNum);
    REQUIRE(nextNum.value().startsWith(QStringLiteral("РТ-")));
  }

  SECTION("Global custom prefix replaces document type prefix")
  {
    settings.setValue(QStringLiteral("business/prefix_strategy"), QStringLiteral("global"));
    settings.setValue(QStringLiteral("business/doc_prefix"), QStringLiteral("FIN"));

    const Result<QString> nextNum = session.documents().allocateNumber(DocumentType::Sale);
    REQUIRE(nextNum);
    REQUIRE(nextNum.value().startsWith(QStringLiteral("FIN-")));
  }

  SECTION("Per-type prefix prepends custom prefix before document type prefix")
  {
    settings.setValue(QStringLiteral("business/prefix_strategy"), QStringLiteral("per_type"));
    settings.setValue(QStringLiteral("business/doc_prefix"), QStringLiteral("UA-"));

    const Result<QString> nextNum = session.documents().allocateNumber(DocumentType::Sale);
    REQUIRE(nextNum);
    REQUIRE(nextNum.value().startsWith(QStringLiteral("UA-РТ-")));
  }

  settings.setValue(QStringLiteral("business/prefix_strategy"), oldStrat);
  settings.setValue(QStringLiteral("business/doc_prefix"), oldPrefix);
}

TEST_CASE("Allow negative stock setting controls posting and integrity validation",
          "[settings][inventory]")
{
  LedgerSession session = openSession();
  REQUIRE(session.accounts().seedChartIfEmpty());

  const NomenclatureId sugar = item(session, QStringLiteral("Цукор"));
  const CounterpartyId buyer = customer(session, QStringLiteral("Покупець Складу"));
  const CounterpartyId supplier = customer(session, QStringLiteral("Постачальник Складу"));

  Document purchase =
      makeDoc(DocumentType::Purchase, QDate(2026, 8, 31), QStringLiteral("НП-NEG-01"));
  purchase.counterpartyId = supplier;
  purchase.lines.push_back(stockLine(account(session, chart::kGoods),
                                     account(session, chart::kSuppliers), sugar, 1000, 20000));
  saveAndPost(session, purchase);

  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 9, 1), QStringLiteral("РТ-NEG-01"));
  sale.counterpartyId = buyer;
  sale.lines.push_back(
      line(account(session, chart::kCustomers), account(session, chart::kRevenue), 400000));
  sale.lines.push_back(stockLine(account(session, chart::kCogs), account(session, chart::kGoods),
                                 sugar, 5000, 20000));

  const Result<DocumentId> docId = session.documents().insert(sale);
  REQUIRE(docId);

  CompanyProfile profile = session.company().load().value();
  profile.allowNegativeStock = false;
  REQUIRE(session.company().save(profile));
  const Status postStatusFail = session.posting().post(docId.value());
  UNSCOPED_INFO("postStatusFail ok=" << bool(postStatusFail)
                                     << " msg=" << postStatusFail.message().toStdString());
  REQUIRE(!postStatusFail);

  profile.allowNegativeStock = true;
  REQUIRE(session.company().save(profile));
  const Status postStatusOk = session.posting().post(docId.value());
  REQUIRE(bool(postStatusOk));

  IntegrityChecker checker(session.handle());
  REQUIRE(bool(checker.rejectNegativeStock()));

  profile.allowNegativeStock = false;
  REQUIRE(session.company().save(profile));
}

TEST_CASE("MainWindow dynamically applies hide_sidebar setting", "[ui][settings]")
{
  LedgerSession session = openSession();
  REQUIRE(session.accounts().seedChartIfEmpty());

  QSettings settings;
  const bool oldHide = settings.value(QStringLiteral("ui/hide_sidebar")).toBool();

  MainWindow window(session);
  QCoreApplication::processEvents();

  auto * nav = window.findChild<QTreeWidget *>();
  REQUIRE(nav != nullptr);

  // Встановлюємо приховування бічної панелі і викликаємо applySettings
  settings.setValue(QStringLiteral("ui/hide_sidebar"), true);
  window.applySettings();
  QCoreApplication::processEvents();
  REQUIRE(nav->isHidden());

  // Показуємо назад
  settings.setValue(QStringLiteral("ui/hide_sidebar"), false);
  window.applySettings();
  QCoreApplication::processEvents();
  REQUIRE_FALSE(nav->isHidden());

  settings.setValue(QStringLiteral("ui/hide_sidebar"), oldHide);
}

TEST_CASE("CalendarConfig persists and applies to QCalendarWidget", "[ui][settings][calendar]")
{
  QSettings settings;
  const QVariant oldFirst = settings.value(QStringLiteral("calendar/first_day_of_week"));
  const QVariant oldGrid = settings.value(QStringLiteral("calendar/grid_visible"));
  const QVariant oldWeeks = settings.value(QStringLiteral("calendar/week_numbers"));
  const QVariant oldNames = settings.value(QStringLiteral("calendar/day_names_format"));
  const QVariant oldNav = settings.value(QStringLiteral("calendar/nav_visible"));
  const QVariant oldHl = settings.value(QStringLiteral("calendar/highlight_weekends"));

  SECTION("load/save round-trip")
  {
    ui::CalendarConfig cfg;
    cfg.firstDayOfWeek = 7;
    cfg.showGrid = false;
    cfg.showWeekNumbers = false;
    cfg.dayNamesFormat = QCalendarWidget::SingleLetterDayNames;
    cfg.showNavigation = false;
    cfg.highlightWeekends = false;
    ui::saveCalendarConfig(cfg);

    const ui::CalendarConfig loaded = ui::loadCalendarConfig();
    REQUIRE(loaded.firstDayOfWeek == 7);
    REQUIRE_FALSE(loaded.showGrid);
    REQUIRE_FALSE(loaded.showWeekNumbers);
    REQUIRE(loaded.dayNamesFormat == QCalendarWidget::SingleLetterDayNames);
    REQUIRE_FALSE(loaded.showNavigation);
    REQUIRE_FALSE(loaded.highlightWeekends);
  }

  SECTION("applyCalendarConfig updates widget properties")
  {
    ui::CalendarConfig cfg;
    cfg.firstDayOfWeek = static_cast<int>(Qt::Sunday);
    cfg.showGrid = false;
    cfg.showWeekNumbers = true;
    cfg.dayNamesFormat = QCalendarWidget::LongDayNames;
    cfg.showNavigation = false;
    cfg.highlightWeekends = true;

    QCalendarWidget cal;
    ui::applyCalendarConfig(&cal, cfg);
    REQUIRE(cal.locale().language() == QLocale::Ukrainian);
    REQUIRE(cal.firstDayOfWeek() == Qt::Sunday);
    REQUIRE_FALSE(cal.isGridVisible());
    REQUIRE(cal.verticalHeaderFormat() == QCalendarWidget::ISOWeekNumbers);
    REQUIRE(cal.horizontalHeaderFormat() == QCalendarWidget::LongDayNames);
    REQUIRE_FALSE(cal.isNavigationBarVisible());
  }

  SECTION("locale first day (0) resolves without crashing")
  {
    ui::CalendarConfig cfg;
    cfg.firstDayOfWeek = 0;
    QCalendarWidget cal;
    ui::applyCalendarConfig(&cal, cfg);
    REQUIRE((cal.firstDayOfWeek() == Qt::Monday || cal.firstDayOfWeek() == Qt::Sunday ||
             cal.firstDayOfWeek() == QLocale().firstDayOfWeek()));
  }

  SECTION("SettingsDialog saves calendar options")
  {
    LedgerSession session = openSession();
    settings.setValue(QStringLiteral("calendar/first_day_of_week"), 1);
    settings.setValue(QStringLiteral("calendar/grid_visible"), true);
    settings.setValue(QStringLiteral("calendar/week_numbers"), true);
    settings.setValue(QStringLiteral("calendar/day_names_format"),
                      static_cast<int>(QCalendarWidget::ShortDayNames));
    settings.setValue(QStringLiteral("calendar/nav_visible"), true);
    settings.setValue(QStringLiteral("calendar/highlight_weekends"), true);

    SettingsDialog dialog(session);
    QCoreApplication::processEvents();

    auto combos = dialog.findChildren<QComboBox *>();
    for (auto * combo : combos) {
      const int sunIdx = combo->findData(7);
      if (sunIdx >= 0 && combo->count() == 3) {
        combo->setCurrentIndex(sunIdx);
        break;
      }
    }
    for (auto * cb : dialog.findChildren<QCheckBox *>()) {
      if (cb->text().contains(QStringLiteral("сітку"))) {
        cb->setChecked(false);
      } else if (cb->text().contains(QStringLiteral("номери тижнів"))) {
        cb->setChecked(false);
      }
    }

    QMetaObject::invokeMethod(&dialog, "acceptChanges");

    REQUIRE(settings.value(QStringLiteral("calendar/first_day_of_week")).toInt() == 7);
    REQUIRE_FALSE(settings.value(QStringLiteral("calendar/grid_visible")).toBool());
    REQUIRE_FALSE(settings.value(QStringLiteral("calendar/week_numbers")).toBool());
  }

  if (oldFirst.isValid())
    settings.setValue(QStringLiteral("calendar/first_day_of_week"), oldFirst);
  else
    settings.remove(QStringLiteral("calendar/first_day_of_week"));
  if (oldGrid.isValid())
    settings.setValue(QStringLiteral("calendar/grid_visible"), oldGrid);
  else
    settings.remove(QStringLiteral("calendar/grid_visible"));
  if (oldWeeks.isValid())
    settings.setValue(QStringLiteral("calendar/week_numbers"), oldWeeks);
  else
    settings.remove(QStringLiteral("calendar/week_numbers"));
  if (oldNames.isValid())
    settings.setValue(QStringLiteral("calendar/day_names_format"), oldNames);
  else
    settings.remove(QStringLiteral("calendar/day_names_format"));
  if (oldNav.isValid())
    settings.setValue(QStringLiteral("calendar/nav_visible"), oldNav);
  else
    settings.remove(QStringLiteral("calendar/nav_visible"));
  if (oldHl.isValid())
    settings.setValue(QStringLiteral("calendar/highlight_weekends"), oldHl);
  else
    settings.remove(QStringLiteral("calendar/highlight_weekends"));
}

TEST_CASE("PrintConfig persists via QSettings", "[ui][settings][print]")
{
  QSettings settings;
  const QVariant oldNumbers = settings.value(QStringLiteral("print/page_numbers"));
  const QVariant oldHeaders = settings.value(QStringLiteral("print/repeat_table_headers"));
  const QVariant oldLeft = settings.value(QStringLiteral("print/margin_left_mm"));
  const QVariant oldRight = settings.value(QStringLiteral("print/margin_right_mm"));
  const QVariant oldTop = settings.value(QStringLiteral("print/margin_top_mm"));
  const QVariant oldBottom = settings.value(QStringLiteral("print/margin_bottom_mm"));
  const QVariant oldFont = settings.value(QStringLiteral("print/font_pt"));
  const QVariant oldWm = settings.value(QStringLiteral("print/watermark_text"));
  const QVariant oldFilter = settings.value(QStringLiteral("print/page_filter"));

  ui::PrintConfig cfg;
  cfg.pageNumbers = false;
  cfg.repeatTableHeaders = false;
  cfg.marginLeftMm = 12;
  cfg.marginRightMm = 14;
  cfg.marginTopMm = 10;
  cfg.marginBottomMm = 16;
  cfg.fontPt = 9.5;
  cfg.watermarkText = QStringLiteral("ЧЕРНЕТКА");
  cfg.pageFilter = 1;
  ui::savePrintConfig(cfg);

  const ui::PrintConfig loaded = ui::loadPrintConfig();
  REQUIRE_FALSE(loaded.pageNumbers);
  REQUIRE_FALSE(loaded.repeatTableHeaders);
  REQUIRE(loaded.marginLeftMm == 12);
  REQUIRE(loaded.marginRightMm == 14);
  REQUIRE(loaded.marginTopMm == 10);
  REQUIRE(loaded.marginBottomMm == 16);
  REQUIRE(loaded.fontPt == Catch::Approx(9.5));
  REQUIRE(loaded.watermarkText == QStringLiteral("ЧЕРНЕТКА"));
  REQUIRE(loaded.pageFilter == 1);

  cfg.pageNumbers = true;
  cfg.repeatTableHeaders = true;
  cfg.watermarkText.clear();
  cfg.pageFilter = 0;
  ui::savePrintConfig(cfg);
  const ui::PrintConfig again = ui::loadPrintConfig();
  REQUIRE(again.pageNumbers);
  REQUIRE(again.repeatTableHeaders);
  REQUIRE(again.watermarkText.isEmpty());
  REQUIRE(again.pageFilter == 0);

  LedgerSession session = openSession();
  SettingsDialog dialog(session);
  QCoreApplication::processEvents();
  bool foundPageNumbers = false;
  bool foundHeaders = false;
  bool foundWatermark = false;
  bool foundFilter = false;
  for (auto * cb : dialog.findChildren<QCheckBox *>()) {
    if (cb->text().contains(QStringLiteral("номери сторінок"))) {
      foundPageNumbers = true;
      cb->setChecked(false);
    } else if (cb->text().contains(QStringLiteral("заголовків стовпців"))) {
      foundHeaders = true;
      cb->setChecked(false);
    }
  }
  for (auto * edit : dialog.findChildren<QLineEdit *>()) {
    if (edit->placeholderText().contains(QStringLiteral("ЧЕРНЕТКА"))) {
      foundWatermark = true;
      edit->setText(QStringLiteral("КОПІЯ"));
    }
  }
  for (auto * combo : dialog.findChildren<QComboBox *>()) {
    if (combo->findData(1) >= 0 && combo->itemText(0).contains(QStringLiteral("Усі сторінки"))) {
      foundFilter = true;
      combo->setCurrentIndex(combo->findData(2));
    }
  }
  REQUIRE(foundPageNumbers);
  REQUIRE(foundHeaders);
  REQUIRE(foundWatermark);
  REQUIRE(foundFilter);
  QMetaObject::invokeMethod(&dialog, "acceptChanges");
  REQUIRE_FALSE(settings.value(QStringLiteral("print/page_numbers")).toBool());
  REQUIRE_FALSE(settings.value(QStringLiteral("print/repeat_table_headers")).toBool());
  REQUIRE(settings.value(QStringLiteral("print/watermark_text")).toString() ==
          QStringLiteral("КОПІЯ"));
  REQUIRE(settings.value(QStringLiteral("print/page_filter")).toInt() == 2);

  auto restore = [&](const char * key, const QVariant & old) {
    if (old.isValid())
      settings.setValue(QString::fromUtf8(key), old);
    else
      settings.remove(QString::fromUtf8(key));
  };
  restore("print/page_numbers", oldNumbers);
  restore("print/repeat_table_headers", oldHeaders);
  restore("print/margin_left_mm", oldLeft);
  restore("print/margin_right_mm", oldRight);
  restore("print/margin_top_mm", oldTop);
  restore("print/margin_bottom_mm", oldBottom);
  restore("print/font_pt", oldFont);
  restore("print/watermark_text", oldWm);
  restore("print/page_filter", oldFilter);
}
