#include "ui/UiSupport.h"

#include "fin/domain/Contract.h"
#include "fin/domain/Counterparty.h"
#include "fin/domain/Currency.h"
#include "fin/domain/Employee.h"
#include "fin/domain/FixedAsset.h"
#include "fin/domain/IntangibleAsset.h"
#include "fin/domain/LowValueItem.h"
#include "fin/domain/Nomenclature.h"
#include "fin/domain/Warehouse.h"

#include <QApplication>
#include <QComboBox>
#include <QDate>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QGraphicsOpacityEffect>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QSettings>
#include <QStyleFactory>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextCharFormat>
#include <QTextStream>
#include <QTimer>
#include <QVariant>

namespace fin::ui {
namespace {

class CalendarEventFilter : public QObject
{
 public:
  explicit CalendarEventFilter(QObject * parent = nullptr) : QObject(parent) {}

 protected:
  bool eventFilter(QObject * watched, QEvent * event) override
  {
    if (event != nullptr && event->type() == QEvent::Show) {
      if (auto * cal = qobject_cast<QCalendarWidget *>(watched)) {
        applyCalendarConfig(cal);
      }
    }
    return QObject::eventFilter(watched, event);
  }
};

Qt::DayOfWeek resolvedFirstDay(const CalendarConfig & config)
{
  if (config.firstDayOfWeek == 0) {
    return QLocale().firstDayOfWeek();
  }
  if (config.firstDayOfWeek >= static_cast<int>(Qt::Monday) &&
      config.firstDayOfWeek <= static_cast<int>(Qt::Sunday)) {
    return static_cast<Qt::DayOfWeek>(config.firstDayOfWeek);
  }
  return Qt::Monday;
}

QCalendarWidget::HorizontalHeaderFormat clampHeaderFormat(int value)
{
  switch (value) {
  case QCalendarWidget::SingleLetterDayNames:
  case QCalendarWidget::ShortDayNames:
  case QCalendarWidget::LongDayNames:
    return static_cast<QCalendarWidget::HorizontalHeaderFormat>(value);
  default:
    return QCalendarWidget::ShortDayNames;
  }
}

bool isDarkThemeActive()
{
  QSettings settings;
  const QString theme =
      settings.value(QStringLiteral("ui/theme"), QStringLiteral("system")).toString();
  if (theme == QStringLiteral("dark")) {
    return true;
  }
  if (theme == QStringLiteral("light")) {
    return false;
  }
  const QColor window = QApplication::palette().color(QPalette::Window);
  return window.lightness() < 128;
}

} // namespace

CalendarConfig loadCalendarConfig()
{
  QSettings settings;
  CalendarConfig config;
  config.firstDayOfWeek = settings.value(QStringLiteral("calendar/first_day_of_week"), 1).toInt();
  if (config.firstDayOfWeek != 0 && (config.firstDayOfWeek < static_cast<int>(Qt::Monday) ||
                                     config.firstDayOfWeek > static_cast<int>(Qt::Sunday))) {
    config.firstDayOfWeek = static_cast<int>(Qt::Monday);
  }
  config.showGrid = settings.value(QStringLiteral("calendar/grid_visible"), true).toBool();
  config.showWeekNumbers = settings.value(QStringLiteral("calendar/week_numbers"), true).toBool();
  config.dayNamesFormat =
      clampHeaderFormat(settings
                            .value(QStringLiteral("calendar/day_names_format"),
                                   static_cast<int>(QCalendarWidget::ShortDayNames))
                            .toInt());
  config.showNavigation = settings.value(QStringLiteral("calendar/nav_visible"), true).toBool();
  config.highlightWeekends =
      settings.value(QStringLiteral("calendar/highlight_weekends"), true).toBool();
  return config;
}

void saveCalendarConfig(const CalendarConfig & config)
{
  QSettings settings;
  settings.setValue(QStringLiteral("calendar/first_day_of_week"), config.firstDayOfWeek);
  settings.setValue(QStringLiteral("calendar/grid_visible"), config.showGrid);
  settings.setValue(QStringLiteral("calendar/week_numbers"), config.showWeekNumbers);
  settings.setValue(QStringLiteral("calendar/day_names_format"),
                    static_cast<int>(config.dayNamesFormat));
  settings.setValue(QStringLiteral("calendar/nav_visible"), config.showNavigation);
  settings.setValue(QStringLiteral("calendar/highlight_weekends"), config.highlightWeekends);
}

void applyCalendarConfig(QCalendarWidget * cal, const CalendarConfig & config)
{
  if (cal == nullptr) {
    return;
  }
  static const QLocale ukLocale(QLocale::Ukrainian, QLocale::Ukraine);
  cal->setLocale(ukLocale);
  cal->setFirstDayOfWeek(resolvedFirstDay(config));
  cal->setGridVisible(config.showGrid);
  cal->setVerticalHeaderFormat(config.showWeekNumbers ? QCalendarWidget::ISOWeekNumbers
                                                      : QCalendarWidget::NoVerticalHeader);
  cal->setHorizontalHeaderFormat(config.dayNamesFormat);
  cal->setNavigationBarVisible(config.showNavigation);

  QTextCharFormat weekday;
  weekday.setForeground(cal->palette().color(QPalette::Text));
  QTextCharFormat weekend = weekday;
  if (config.highlightWeekends) {
    weekend.setForeground(isDarkThemeActive() ? QColor(QStringLiteral("#f0a0a0"))
                                              : QColor(QStringLiteral("#b00020")));
  }
  cal->setWeekdayTextFormat(Qt::Monday, weekday);
  cal->setWeekdayTextFormat(Qt::Tuesday, weekday);
  cal->setWeekdayTextFormat(Qt::Wednesday, weekday);
  cal->setWeekdayTextFormat(Qt::Thursday, weekday);
  cal->setWeekdayTextFormat(Qt::Friday, weekday);
  cal->setWeekdayTextFormat(Qt::Saturday, weekend);
  cal->setWeekdayTextFormat(Qt::Sunday, weekend);
}

void applyCalendarConfig(QCalendarWidget * cal)
{
  applyCalendarConfig(cal, loadCalendarConfig());
}

void reapplyCalendarConfigToAll()
{
  const CalendarConfig config = loadCalendarConfig();
  const QWidgetList widgets = QApplication::allWidgets();
  for (QWidget * widget : widgets) {
    if (auto * cal = qobject_cast<QCalendarWidget *>(widget)) {
      applyCalendarConfig(cal, config);
    }
  }
}

void installCalendarConfigFilter()
{
  if (qApp == nullptr) {
    return;
  }
  static CalendarEventFilter * filter = nullptr;
  if (filter == nullptr) {
    filter = new CalendarEventFilter(qApp);
    qApp->installEventFilter(filter);
  }
}

PrintConfig loadPrintConfig()
{
  QSettings settings;
  PrintConfig config;
  config.pageNumbers = settings.value(QStringLiteral("print/page_numbers"), true).toBool();
  config.repeatTableHeaders =
      settings.value(QStringLiteral("print/repeat_table_headers"), true).toBool();
  auto clampMargin = [](int value) { return qBound(5, value, 40); };
  config.marginLeftMm =
      clampMargin(settings.value(QStringLiteral("print/margin_left_mm"), 8).toInt());
  config.marginRightMm =
      clampMargin(settings.value(QStringLiteral("print/margin_right_mm"), 8).toInt());
  config.marginTopMm =
      clampMargin(settings.value(QStringLiteral("print/margin_top_mm"), 8).toInt());
  config.marginBottomMm =
      clampMargin(settings.value(QStringLiteral("print/margin_bottom_mm"), 8).toInt());
  config.fontPt =
      qBound(8.0, settings.value(QStringLiteral("print/font_pt"), 11.0).toDouble(), 14.0);
  config.watermarkText = settings.value(QStringLiteral("print/watermark_text")).toString();
  config.pageFilter = settings.value(QStringLiteral("print/page_filter"), 0).toInt();
  if (config.pageFilter < 0 || config.pageFilter > 2) {
    config.pageFilter = 0;
  }
  return config;
}

void savePrintConfig(const PrintConfig & config)
{
  QSettings settings;
  settings.setValue(QStringLiteral("print/page_numbers"), config.pageNumbers);
  settings.setValue(QStringLiteral("print/repeat_table_headers"), config.repeatTableHeaders);
  settings.setValue(QStringLiteral("print/margin_left_mm"), qBound(5, config.marginLeftMm, 40));
  settings.setValue(QStringLiteral("print/margin_right_mm"), qBound(5, config.marginRightMm, 40));
  settings.setValue(QStringLiteral("print/margin_top_mm"), qBound(5, config.marginTopMm, 40));
  settings.setValue(QStringLiteral("print/margin_bottom_mm"), qBound(5, config.marginBottomMm, 40));
  settings.setValue(QStringLiteral("print/font_pt"), qBound(8.0, config.fontPt, 14.0));
  settings.setValue(QStringLiteral("print/watermark_text"), config.watermarkText);
  settings.setValue(QStringLiteral("print/page_filter"), qBound(0, config.pageFilter, 2));
}

QIcon themeIcon(const QString & path, const QColor & color)
{
  QIcon original(path);
  if (original.isNull()) {
    return original;
  }
  QIcon colored;
  for (int size : {16, 24, 32, 48, 64}) {
    QPixmap pix = original.pixmap(size, size);
    QPainter p(&pix);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(pix.rect(), color);
    p.end();
    colored.addPixmap(pix);
  }
  return colored;
}

QIcon themeIcon(const QString & path)
{
  return themeIcon(path, QApplication::palette().color(QPalette::WindowText));
}

void showError(QWidget * parent, const Error & error)
{
  showError(parent, error.message);
}

void showError(QWidget * parent, const Status & status)
{
  if (status) {
    return;
  }
  showError(parent, status.message());
}

void showError(QWidget * parent, const QString & message)
{
  QMessageBox::warning(parent, QStringLiteral("Помилка"), message);
}

void showToast(QWidget * parent, const QString & message)
{
  if (!parent)
    return;

  QSettings settings;
  if (settings.value(QStringLiteral("ui/sound_enabled"), true).toBool()) {
    if (message.contains(QStringLiteral("роведено"), Qt::CaseInsensitive)) {
      QApplication::beep();
    }
  }

  auto * label = new QLabel(message, parent);
  label->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
  label->setAttribute(Qt::WA_ShowWithoutActivating);
  label->setStyleSheet(QStringLiteral("QLabel { background-color: #333; color: white; padding: "
                                      "10px; border-radius: 4px; font-size: 14px; }"));
  label->adjustSize();

  QPoint pos = parent->mapToGlobal(parent->rect().bottomLeft());
  pos.setX(pos.x() + (parent->width() - label->width()) / 2);
  pos.setY(pos.y() - label->height() - 40);
  label->move(pos);

  auto * effect = new QGraphicsOpacityEffect(label);
  label->setGraphicsEffect(effect);
  label->show();

  auto * animation = new QPropertyAnimation(effect, "opacity");
  animation->setDuration(500);
  animation->setStartValue(1.0);
  animation->setEndValue(0.0);

  QTimer::singleShot(2500, label, [animation, label]() {
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    QObject::connect(animation, &QPropertyAnimation::finished, label, &QObject::deleteLater);
  });
}

void applyThemeAndStyle()
{
  QSettings settings;
  const QString styleName =
      settings.value(QStringLiteral("ui/style"), QStringLiteral("fusion")).toString();
  if (QStyleFactory::keys().contains(styleName, Qt::CaseInsensitive)) {
    QApplication::setStyle(QStyleFactory::create(styleName));
  }

  const QString calendarCss =
      QStringLiteral("QCalendarWidget QWidget#qt_calendar_navigationbar {"
                     "  padding: 4px; }"
                     "QCalendarWidget QToolButton {"
                     "  background: transparent; border: none; padding: 4px 8px;"
                     "  border-radius: 4px; }"
                     "QCalendarWidget QAbstractItemView:enabled {"
                     "  selection-background-color: #2a82da; selection-color: white;"
                     "  outline: none; }"
                     "QCalendarWidget QAbstractItemView:disabled { color: #888; }");

  const QString theme =
      settings.value(QStringLiteral("ui/theme"), QStringLiteral("system")).toString();
  if (theme == QStringLiteral("dark")) {
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
    darkPalette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);
    QApplication::setPalette(darkPalette);
    if (qApp) {
      qApp->setStyleSheet(QStringLiteral("QMenu { padding: 4px 0px; }"
                                         "QTableView { alternate-background-color: #353535; }") +
                          calendarCss +
                          QStringLiteral("QCalendarWidget QWidget#qt_calendar_navigationbar {"
                                         "  background-color: #404040; }"
                                         "QCalendarWidget QToolButton { color: white; }"
                                         "QCalendarWidget QAbstractItemView:enabled {"
                                         "  background-color: #252525; color: white; }"));
    }
  } else {
    QApplication::setPalette(QApplication::style()->standardPalette());
    if (qApp) {
      qApp->setStyleSheet(QStringLiteral("QMenu { padding: 4px 0px; }"
                                         "QTableView { alternate-background-color: #f7f7f7; }") +
                          calendarCss +
                          QStringLiteral("QCalendarWidget QWidget#qt_calendar_navigationbar {"
                                         "  background-color: #f0f0f0; }"
                                         "QCalendarWidget QAbstractItemView:enabled {"
                                         "  background-color: white; }"));
    }
  }
  reapplyCalendarConfigToAll();
}

void exportTableToCsv(const QTableWidget * table, QWidget * parent)
{
  if (!table)
    return;

  QString fileName = QFileDialog::getSaveFileName(
      parent, QStringLiteral("Експорт в CSV"), QStringLiteral("export.csv"),
      QStringLiteral("CSV Файли (*.csv);;Всі файли (*.*)"));
  if (fileName.isEmpty()) {
    return;
  }

  QFile file(fileName);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    showError(parent, QStringLiteral("Не вдалося відкрити файл для запису: ") + file.errorString());
    return;
  }

  QTextStream out(&file);
  out.setEncoding(QStringConverter::Utf8);
  // BOM для Excel
  out << "\xEF\xBB\xBF";

  // Заголовки
  for (int col = 0; col < table->columnCount(); ++col) {
    if (col > 0)
      out << ";";
    QTableWidgetItem * item = table->horizontalHeaderItem(col);
    QString text = item ? item->text() : QString();
    text.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    out << "\"" << text << "\"";
  }
  out << "\n";

  // Дані
  for (int row = 0; row < table->rowCount(); ++row) {
    for (int col = 0; col < table->columnCount(); ++col) {
      if (col > 0)
        out << ";";
      QTableWidgetItem * item = table->item(row, col);
      QString text = item ? item->text() : QString();
      text.replace(QStringLiteral("\""), QStringLiteral("\"\""));
      // Очищення пробілів у числах (наприклад 1 000.00 -> 1000.00)
      if (col >= 2)
        text.remove(QLatin1Char(' '));
      out << "\"" << text << "\"";
    }
    out << "\n";
  }

  file.close();
  showToast(parent, QStringLiteral("Експортовано в ") + fileName);
}

void setRowBold(QTableWidget * table, int row)
{
  for (int col = 0; col < table->columnCount(); ++col) {
    auto * item = table->item(row, col);
    if (item) {
      QFont f = item->font();
      f.setBold(true);
      item->setFont(f);
    }
  }
}

QString accountLabel(const Account & account)
{
  return account.code + QStringLiteral("  ") + account.name;
}

void fillAccountCombo(QComboBox * combo, const std::vector<Account> & accounts, AccountId selected)
{
  combo->clear();
  combo->addItem(QStringLiteral("—"), QVariant::fromValue(qlonglong(0)));
  int index = 0;
  for (const Account & account : accounts) {
    combo->addItem(accountLabel(account),
                   QVariant::fromValue(static_cast<qlonglong>(account.id.value)));
    if (account.id == selected) {
      index = combo->count() - 1;
    }
  }
  combo->setCurrentIndex(index);
}

AccountId selectedAccount(const QComboBox * combo)
{
  return AccountId{combo->currentData().toLongLong()};
}

void fillCounterpartyCombo(QComboBox * combo, LedgerSession & session, CounterpartyId selected,
                           bool optional)
{
  combo->clear();
  if (optional) {
    combo->addItem(QStringLiteral("—"), QVariant::fromValue(qlonglong(0)));
  }
  const Result<std::vector<Counterparty>> list = session.counterparties().listAll();
  if (!list) {
    return;
  }
  int index = optional ? 0 : -1;
  int i = combo->count();
  for (const Counterparty & row : list.value()) {
    combo->addItem(row.name, QVariant::fromValue(static_cast<qlonglong>(row.id.value)));
    if (row.id == selected) {
      index = i;
    }
    ++i;
  }
  if (index >= 0) {
    combo->setCurrentIndex(index);
  }
}

CounterpartyId selectedCounterparty(const QComboBox * combo)
{
  return CounterpartyId{combo->currentData().toLongLong()};
}

void fillNomenclatureCombo(QComboBox * combo, LedgerSession & session, NomenclatureId selected)
{
  combo->clear();
  combo->addItem(QStringLiteral("—"), QVariant::fromValue(qlonglong(0)));
  const Result<std::vector<Nomenclature>> list = session.nomenclature().listActive();
  if (!list) {
    return;
  }
  int index = 0;
  for (const Nomenclature & row : list.value()) {
    QString label = row.name;
    if (!row.sku.isEmpty()) {
      label = row.sku + QStringLiteral("  ") + row.name;
    }
    combo->addItem(label, QVariant::fromValue(static_cast<qlonglong>(row.id.value)));
    if (row.id == selected) {
      index = combo->count() - 1;
    }
  }
  combo->setCurrentIndex(index);
}

NomenclatureId selectedNomenclature(const QComboBox * combo)
{
  return NomenclatureId{combo->currentData().toLongLong()};
}

void fillFixedAssetCombo(QComboBox * combo, LedgerSession & session, FixedAssetId selected,
                         bool draftsOnly, bool inUseOnly)
{
  combo->clear();
  combo->addItem(QStringLiteral("—"), QVariant::fromValue(qlonglong(0)));
  Result<std::vector<FixedAsset>> list = draftsOnly ? session.fixedAssets().listDraft()
                                                    : (inUseOnly ? session.fixedAssets().listInUse()
                                                                 : session.fixedAssets().listAll());
  if (!list) {
    return;
  }
  std::vector<FixedAsset> rows = std::move(list.value());
  if ((draftsOnly || inUseOnly) && selected) {
    bool have = false;
    for (const FixedAsset & row : rows) {
      if (row.id == selected) {
        have = true;
        break;
      }
    }
    if (!have) {
      const Result<FixedAsset> extra = session.fixedAssets().findById(selected);
      if (extra) {
        rows.push_back(extra.value());
      }
    }
  }
  int index = 0;
  for (const FixedAsset & row : rows) {
    const QString label = row.inventoryNo + QStringLiteral("  ") + row.name + QStringLiteral("  ") +
                          row.original.toDisplay();
    combo->addItem(label, QVariant::fromValue(static_cast<qlonglong>(row.id.value)));
    if (row.id == selected) {
      index = combo->count() - 1;
    }
  }
  combo->setCurrentIndex(index);
}

FixedAssetId selectedFixedAsset(const QComboBox * combo)
{
  return FixedAssetId{combo->currentData().toLongLong()};
}

void fillIntangibleCombo(QComboBox * combo, LedgerSession & session, IntangibleId selected,
                         bool draftsOnly, bool inUseOnly)
{
  combo->clear();
  combo->addItem(QStringLiteral("—"), QVariant::fromValue(qlonglong(0)));
  Result<std::vector<IntangibleAsset>> list =
      draftsOnly
          ? session.intangibles().listDraft()
          : (inUseOnly ? session.intangibles().listInUse() : session.intangibles().listAll());
  if (!list) {
    return;
  }
  std::vector<IntangibleAsset> rows = std::move(list.value());
  if ((draftsOnly || inUseOnly) && selected) {
    bool have = false;
    for (const IntangibleAsset & row : rows) {
      if (row.id == selected) {
        have = true;
        break;
      }
    }
    if (!have) {
      const Result<IntangibleAsset> extra = session.intangibles().findById(selected);
      if (extra) {
        rows.push_back(extra.value());
      }
    }
  }
  int index = 0;
  for (const IntangibleAsset & row : rows) {
    const QString label = row.inventoryNo + QStringLiteral("  ") + row.name + QStringLiteral("  ") +
                          row.original.toDisplay();
    combo->addItem(label, QVariant::fromValue(static_cast<qlonglong>(row.id.value)));
    if (row.id == selected) {
      index = combo->count() - 1;
    }
  }
  combo->setCurrentIndex(index);
}

IntangibleId selectedIntangible(const QComboBox * combo)
{
  return IntangibleId{combo->currentData().toLongLong()};
}

void fillLowValueCombo(QComboBox * combo, LedgerSession & session, LowValueId selected)
{
  combo->clear();
  combo->addItem(QStringLiteral("—"), QVariant::fromValue(qlonglong(0)));
  const Result<std::vector<LowValueItem>> list = session.lowValue().listActive();
  if (!list) {
    return;
  }
  int index = 0;
  for (const LowValueItem & row : list.value()) {
    const QString label = row.inventoryNo + QStringLiteral("  ") + row.name;
    combo->addItem(label, QVariant::fromValue(static_cast<qlonglong>(row.id.value)));
    if (row.id == selected) {
      index = combo->count() - 1;
    }
  }
  if (selected) {
    bool have = false;
    for (int i = 0; i < combo->count(); ++i) {
      if (combo->itemData(i).toLongLong() == selected.value) {
        have = true;
        index = i;
        break;
      }
    }
    if (!have) {
      const Result<LowValueItem> extra = session.lowValue().findById(selected);
      if (extra) {
        combo->addItem(extra.value().inventoryNo + QStringLiteral("  ") + extra.value().name,
                       QVariant::fromValue(static_cast<qlonglong>(selected.value)));
        index = combo->count() - 1;
      }
    }
  }
  combo->setCurrentIndex(index);
}

LowValueId selectedLowValue(const QComboBox * combo)
{
  return LowValueId{combo->currentData().toLongLong()};
}

void fillWarehouseCombo(QComboBox * combo, LedgerSession & session, WarehouseId selected,
                        bool includeAll)
{
  combo->clear();
  if (includeAll) {
    combo->addItem(QStringLiteral("Усі склади"), QVariant::fromValue(qlonglong(0)));
  }
  const Result<std::vector<Warehouse>> list = session.warehouses().listAll(true);
  if (!list) {
    return;
  }
  int index = 0;
  for (const Warehouse & wh : list.value()) {
    combo->addItem(wh.name, QVariant::fromValue(static_cast<qlonglong>(wh.id.value)));
    if (wh.id == selected) {
      index = combo->count() - 1;
    }
  }
  if (selected && index == 0 && (!includeAll || selected.value != 0)) {
    for (int i = 0; i < combo->count(); ++i) {
      if (combo->itemData(i).toLongLong() == selected.value) {
        index = i;
        break;
      }
    }
  }
  combo->setCurrentIndex(index);
}

WarehouseId selectedWarehouse(const QComboBox * combo)
{
  if (combo == nullptr) {
    return {};
  }
  return WarehouseId{combo->currentData().toLongLong()};
}

void fillContractCombo(QComboBox * combo, LedgerSession & session, CounterpartyId cpId,
                       ContractId selected, bool includeEmpty)
{
  combo->clear();
  if (includeEmpty) {
    combo->addItem(QStringLiteral("—"), QVariant::fromValue(qlonglong(0)));
  }
  if (!cpId) {
    return;
  }
  const Result<std::vector<Contract>> list = session.contracts().listByCounterparty(cpId, true);
  if (!list) {
    return;
  }
  int index = 0;
  for (const Contract & c : list.value()) {
    const QString label = c.number.isEmpty()
                              ? c.name
                              : (c.number + QStringLiteral(" (") + c.name + QStringLiteral(")"));
    combo->addItem(label, QVariant::fromValue(static_cast<qlonglong>(c.id.value)));
    if (c.id == selected) {
      index = combo->count() - 1;
    }
  }
  if (selected) {
    for (int i = 0; i < combo->count(); ++i) {
      if (combo->itemData(i).toLongLong() == selected.value) {
        index = i;
        break;
      }
    }
  }
  combo->setCurrentIndex(index);
}

ContractId selectedContract(const QComboBox * combo)
{
  if (combo == nullptr) {
    return {};
  }
  return ContractId{combo->currentData().toLongLong()};
}

void fillCurrencyCombo(QComboBox * combo, LedgerSession & session, const QString & selected)
{
  combo->clear();
  combo->addItem(QStringLiteral("UAH — гривня"), QStringLiteral("UAH"));
  const Result<std::vector<Currency>> list = session.currencies().listAll(true);
  if (list) {
    for (const Currency & currency : list.value()) {
      if (currency.code.compare(QStringLiteral("UAH"), Qt::CaseInsensitive) == 0) {
        continue;
      }
      const QString label = currency.nameUa.isEmpty()
                                ? currency.code
                                : (currency.code + QStringLiteral(" — ") + currency.nameUa);
      combo->addItem(label, currency.code);
    }
  }
  const QString want =
      selected.trimmed().isEmpty() ? QStringLiteral("UAH") : selected.trimmed().toUpper();
  for (int i = 0; i < combo->count(); ++i) {
    if (combo->itemData(i).toString() == want) {
      combo->setCurrentIndex(i);
      return;
    }
  }
  combo->setCurrentIndex(0);
}

QString selectedCurrencyCode(const QComboBox * combo)
{
  if (combo == nullptr) {
    return QStringLiteral("UAH");
  }
  const QString code = combo->currentData().toString().trimmed().toUpper();
  return code.isEmpty() ? QStringLiteral("UAH") : code;
}

QString currencyRateToDisplay(std::int64_t rateMicros, int nominal)
{
  if (rateMicros <= 0 || nominal <= 0) {
    return {};
  }
  const std::int64_t intPart = rateMicros / ExchangeRate::kMicrosPerUah;
  const std::int64_t fracPart = (rateMicros % ExchangeRate::kMicrosPerUah) / 100;
  return QStringLiteral("%1.%2")
      .arg(QString::number(intPart), QString::number(fracPart).rightJustified(4, QLatin1Char('0')));
}

Status currencyRateFromDisplay(const QString & text, int nominal, std::int64_t & rateMicros)
{
  const QString cleaned = text.trimmed().replace(QLatin1Char(','), QLatin1Char('.'));
  if (cleaned.isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("вкажіть курс валюти")));
  }
  const QStringList parts = cleaned.split(QLatin1Char('.'));
  bool ok1 = false;
  const std::int64_t intPart = parts[0].toLongLong(&ok1);
  if (!ok1 || intPart < 0) {
    return Status::fail(Error::validation(QStringLiteral("некоректний курс валюти")));
  }
  std::int64_t fracPart = 0;
  if (parts.size() > 1) {
    QString fracStr = parts[1].left(4);
    while (fracStr.length() < 4) {
      fracStr += QLatin1Char('0');
    }
    bool ok2 = false;
    fracPart = fracStr.toLongLong(&ok2);
    if (!ok2) {
      return Status::fail(Error::validation(QStringLiteral("некоректний курс валюти")));
    }
  }
  const std::int64_t micros = intPart * ExchangeRate::kMicrosPerUah + fracPart * 100;
  if (micros <= 0 || nominal <= 0) {
    return Status::fail(Error::validation(QStringLiteral("курс має бути більшим за нуль")));
  }
  rateMicros = micros;
  return Status::ok();
}

void loadCurrencyRateForDate(QLineEdit * rateEdit, LedgerSession & session, const QString & code,
                             QDate date)
{
  if (rateEdit == nullptr) {
    return;
  }
  const QString currency = code.trimmed().toUpper();
  if (currency.isEmpty() || currency == QLatin1String("UAH")) {
    rateEdit->clear();
    rateEdit->setEnabled(false);
    return;
  }
  rateEdit->setEnabled(true);
  const Result<CurrencyRate> rate = session.currencies().getRate(currency, date);
  if (!rate) {
    rateEdit->clear();
    return;
  }
  rateEdit->setText(currencyRateToDisplay(rate.value().rateMicros, rate.value().nominal));
  rateEdit->setProperty("currencyNominal", rate.value().nominal);
}

QString brandNameUk()
{
  return QStringLiteral("Фінік");
}

QPixmap brandLogo(int width)
{
  QPixmap raw(QStringLiteral(":/help/logo.png"));
  if (raw.isNull() || width <= 0) {
    return raw;
  }
  return raw.scaled(width, width, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

bool selectTableRowById(QTableWidget * table, qlonglong id)
{
  if (table == nullptr || id <= 0) {
    return false;
  }
  for (int row = 0; row < table->rowCount(); ++row) {
    QTableWidgetItem * cell = table->item(row, 0);
    if (cell != nullptr && cell->data(Qt::UserRole).toLongLong() == id) {
      table->setCurrentCell(row, 0);
      table->scrollToItem(cell);
      return true;
    }
  }
  return false;
}

void fillEmployeeCombo(QComboBox * combo, LedgerSession & session, EmployeeId selected,
                       bool optional)
{
  combo->clear();
  if (optional) {
    combo->addItem(QStringLiteral("—"), QVariant::fromValue(qlonglong(0)));
  }
  const Result<std::vector<Employee>> list = session.employees().listAll();
  if (!list) {
    return;
  }
  int index = optional ? 0 : -1;
  int i = combo->count();
  for (const Employee & row : list.value()) {
    combo->addItem(row.name, QVariant::fromValue(static_cast<qlonglong>(row.id.value)));
    if (row.id == selected) {
      index = i;
    }
    ++i;
  }
  if (index >= 0) {
    combo->setCurrentIndex(index);
  }
}

EmployeeId selectedEmployee(const QComboBox * combo)
{
  return EmployeeId{combo->currentData().toLongLong()};
}

} // namespace fin::ui
