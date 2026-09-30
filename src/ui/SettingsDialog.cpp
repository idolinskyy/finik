#include "SettingsDialog.h"
#include "UiSupport.h"
#include "ui/HelpBrowser.h"

#include "fin/auth/Access.h"
#include "fin/db/AppPaths.h"
#include "fin/db/UserRepository.h"

#include <QCalendarWidget>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace fin {

SettingsDialog::SettingsDialog(LedgerSession & session, QWidget * parent)
    : QDialog(parent), session_(session)
{
  setWindowTitle(QStringLiteral("Налаштування програми"));
  setMinimumSize(720, 520);
  resize(760, 580);

  QSettings settings;
  originalPath_ = settings.value(QStringLiteral("database/custom_path")).toString();

  sidebar_ = new QListWidget(this);
  sidebar_->setFixedWidth(160);

  QListWidgetItem * itemGeneral = new QListWidgetItem(
      ui::themeIcon(QStringLiteral(":/help/icons/settings.svg")), QStringLiteral(" Основні"));
  QListWidgetItem * itemDb = new QListWidgetItem(
      ui::themeIcon(QStringLiteral(":/help/icons/warehouse.svg")), QStringLiteral(" База даних"));
  QListWidgetItem * itemAppearance =
      new QListWidgetItem(ui::themeIcon(QStringLiteral(":/help/icons/desktop.svg")),
                          QStringLiteral(" Вигляд та вікна"));
  QListWidgetItem * itemPrint = new QListWidgetItem(
      ui::themeIcon(QStringLiteral(":/help/icons/print.svg")), QStringLiteral(" Друк"));
  QListWidgetItem * itemBackup =
      new QListWidgetItem(ui::themeIcon(QStringLiteral(":/help/icons/sparkles.svg")),
                          QStringLiteral(" Безпека та Копії"));
  QListWidgetItem * itemAutomation = new QListWidgetItem(
      ui::themeIcon(QStringLiteral(":/help/icons/clock.svg")), QStringLiteral(" Автоматизація"));

  QListWidgetItem * itemAccounting = new QListWidgetItem(
      ui::themeIcon(QStringLiteral(":/help/icons/balance.svg")), QStringLiteral(" Облік"));

  sidebar_->addItem(itemGeneral);
  sidebar_->addItem(itemAccounting);
  sidebar_->addItem(itemDb);
  sidebar_->addItem(itemAppearance);
  sidebar_->addItem(itemPrint);
  sidebar_->addItem(itemBackup);
  sidebar_->addItem(itemAutomation);

  stack_ = new QStackedWidget(this);
  stack_->addWidget(createGeneralPage());
  stack_->addWidget(createAccountingPage());
  stack_->addWidget(createDatabasePage());
  stack_->addWidget(createAppearancePage());
  stack_->addWidget(createPrintPage());
  stack_->addWidget(createBackupPage());
  stack_->addWidget(createAutomationPage());

  connect(sidebar_, &QListWidget::currentRowChanged, stack_, &QStackedWidget::setCurrentIndex);
  sidebar_->setCurrentRow(0);

  QDialogButtonBox * buttons =
      new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  if (auto * saveBtn = buttons->button(QDialogButtonBox::Save)) {
    saveBtn->setIcon(ui::themeIcon(QStringLiteral(":/help/icons/post.svg")));
    saveBtn->setText(QStringLiteral("Зберегти"));
  }
  if (auto * cancelBtn = buttons->button(QDialogButtonBox::Cancel)) {
    cancelBtn->setIcon(ui::themeIcon(QStringLiteral(":/help/icons/unpost.svg")));
    cancelBtn->setText(QStringLiteral("Скасувати"));
  }
  connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::acceptChanges);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  QHBoxLayout * hLayout = new QHBoxLayout;
  hLayout->addWidget(sidebar_);
  hLayout->addWidget(stack_);

  QVBoxLayout * mainLayout = new QVBoxLayout(this);
  mainLayout->addLayout(hLayout);
  mainLayout->addWidget(buttons);

  auto * f1 = new QShortcut(QKeySequence::HelpContents, this);
  connect(f1, &QShortcut::activated, this,
          [this]() { HelpBrowser::openTopic(this, QStringLiteral("settings.html")); });
}

QWidget * SettingsDialog::createGeneralPage()
{
  QWidget * page = new QWidget(this);
  QVBoxLayout * layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);

  QLabel * title = new QLabel(QStringLiteral("<h2>Основні налаштування</h2>"), page);
  layout->addWidget(title);

  QSettings settings;

  // Розмір шрифту
  fontSizeCombo_ = new QComboBox(page);
  fontSizeCombo_->addItems({QStringLiteral("9"), QStringLiteral("10"), QStringLiteral("11"),
                            QStringLiteral("12"), QStringLiteral("13"), QStringLiteral("14"),
                            QStringLiteral("16")});
  int currentSize = settings.value(QStringLiteral("ui/font_size"), 10).toInt();
  fontSizeCombo_->setCurrentText(QString::number(currentSize));

  // Підтвердження виходу
  confirmExitCheck_ =
      new QCheckBox(QStringLiteral("Запитувати підтвердження при виході з програми"), page);
  confirmExitCheck_->setChecked(settings.value(QStringLiteral("ui/confirm_exit"), false).toBool());

  restoreTabsCheck_ =
      new QCheckBox(QStringLiteral("Відновлювати відкриті вкладки при запуску"), page);
  restoreTabsCheck_->setChecked(settings.value(QStringLiteral("ui/restore_tabs"), true).toBool());

  QFormLayout * form = new QFormLayout;
  form->addRow(QStringLiteral("Розмір шрифту інтерфейсу:"), fontSizeCombo_);

  layout->addLayout(form);
  layout->addSpacing(10);
  layout->addWidget(confirmExitCheck_);
  layout->addWidget(restoreTabsCheck_);

  QLabel * hint = new QLabel(
      QStringLiteral("<i>Зміна розміру шрифту потребує перезапуску програми.</i>"), page);
  hint->setStyleSheet(QStringLiteral("color: #666;"));
  layout->addWidget(hint);

  layout->addStretch();
  return page;
}

QWidget * SettingsDialog::createAccountingPage()
{
  QWidget * page = new QWidget(this);
  QVBoxLayout * layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);

  QLabel * title = new QLabel(QStringLiteral("<h2>Облікові налаштування</h2>"), page);
  layout->addWidget(title);

  QSettings settings;
  bool allowNegative = false;
  const Result<CompanyProfile> company = session_.company().load();
  if (company) {
    allowNegative = company.value().allowNegativeStock;
  }
  allowNegativeStockCheck_ =
      new QCheckBox(QStringLiteral("Дозволити від'ємні залишки на складах (без "
                                   "суворого контролю списання)"),
                    page);
  allowNegativeStockCheck_->setChecked(allowNegative);
  const Status companyAccess = Access::requireEditCompany(session_.handle());
  allowNegativeStockCheck_->setEnabled(bool(companyAccess));
  accountingGateLabel_ = new QLabel(page);
  accountingGateLabel_->setWordWrap(true);
  if (!companyAccess) {
    accountingGateLabel_->setText(companyAccess.message());
  }

  prefixStrategyCombo_ = new QComboBox(page);
  prefixStrategyCombo_->addItem(QStringLiteral("Стандартний (без власного префікса)"),
                                QStringLiteral("standard"));
  prefixStrategyCombo_->addItem(QStringLiteral("Глобальний (замінює тип документа)"),
                                QStringLiteral("global"));
  prefixStrategyCombo_->addItem(QStringLiteral("Перед типом документа (напр., UA-РТ-)"),
                                QStringLiteral("per_type"));

  QString currentStrat =
      settings.value(QStringLiteral("business/prefix_strategy"), QStringLiteral("standard"))
          .toString();
  int stratIdx = prefixStrategyCombo_->findData(currentStrat);
  if (stratIdx >= 0) {
    prefixStrategyCombo_->setCurrentIndex(stratIdx);
  }

  docPrefixEdit_ = new QLineEdit(page);
  docPrefixEdit_->setText(
      settings.value(QStringLiteral("business/doc_prefix"), QStringLiteral("")).toString());
  docPrefixEdit_->setPlaceholderText(QStringLiteral("Наприклад: UA- або ФІРМА-"));

  QLabel * prefixPreviewLabel = new QLabel(page);
  prefixPreviewLabel->setStyleSheet(QStringLiteral("color: #2b78e4; font-weight: 500;"));
  prefixPreviewLabel->setWordWrap(true);

  auto updatePreview = [this, prefixPreviewLabel]() {
    const QString strat = prefixStrategyCombo_->currentData().toString();
    const QString custom = docPrefixEdit_->text().trimmed();
    docPrefixEdit_->setEnabled(strat != QStringLiteral("standard"));

    QString prefix = QStringLiteral("РТ"); // Приклад: Реалізація товарів
    if (strat == QStringLiteral("global") && !custom.isEmpty()) {
      prefix = custom;
    } else if (strat == QStringLiteral("per_type") && !custom.isEmpty()) {
      if (custom.endsWith(QLatin1Char('-'))) {
        prefix = custom + prefix;
      } else {
        prefix = custom + QLatin1Char('-') + prefix;
      }
    }
    if (prefix.endsWith(QLatin1Char('-'))) {
      prefix.chop(1);
    }
    const QString sample = QStringLiteral("%1-000001").arg(prefix);
    if (strat == QStringLiteral("standard")) {
      prefixPreviewLabel->setText(QStringLiteral("Зразок: %1 (стандартний код типу)").arg(sample));
    } else if (strat == QStringLiteral("global")) {
      prefixPreviewLabel->setText(
          QStringLiteral("Зразок: %1 (єдиний префікс для всіх типів)").arg(sample));
    } else {
      prefixPreviewLabel->setText(
          QStringLiteral("Зразок: %1 (префікс + тип документа)").arg(sample));
    }
  };

  connect(prefixStrategyCombo_, &QComboBox::currentIndexChanged, page, updatePreview);
  connect(docPrefixEdit_, &QLineEdit::textChanged, page, updatePreview);
  updatePreview();

  QFormLayout * form = new QFormLayout;
  form->addRow(QStringLiteral("Стратегія нумерації:"), prefixStrategyCombo_);
  form->addRow(QStringLiteral("Власний префікс номера:"), docPrefixEdit_);
  form->addRow(QStringLiteral("Зразок номера:"), prefixPreviewLabel);

  layout->addSpacing(10);
  layout->addWidget(allowNegativeStockCheck_);
  if (accountingGateLabel_ && !accountingGateLabel_->text().isEmpty()) {
    layout->addWidget(accountingGateLabel_);
  }
  layout->addSpacing(10);
  layout->addLayout(form);

  QLabel * hint = new QLabel(
      QStringLiteral("<i>Контроль залишків забороняє проведення видаткових документів за "
                     "відсутності потрібної кількості товару на складі.</i>"),
      page);
  hint->setStyleSheet(QStringLiteral("color: #666;"));
  hint->setWordWrap(true);
  layout->addWidget(hint);

  layout->addStretch();
  return page;
}

QWidget * SettingsDialog::createDatabasePage()
{
  QWidget * page = new QWidget(this);
  QVBoxLayout * layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);

  QLabel * title = new QLabel(QStringLiteral("<h2>База даних</h2>"), page);
  layout->addWidget(title);

  dbPathEdit_ = new QLineEdit(page);
  dbPathEdit_->setReadOnly(true);

  if (originalPath_.isEmpty()) {
    dbPathEdit_->setText(defaultDatabaseFilePath());
  } else {
    dbPathEdit_->setText(originalPath_);
  }

  QPushButton * browseBtn = new QPushButton(
      ui::themeIcon(QStringLiteral(":/help/icons/search.svg")), QStringLiteral("Огляд..."), page);
  connect(browseBtn, &QPushButton::clicked, this, &SettingsDialog::browseDbPath);

  QPushButton * resetBtn =
      new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/refresh.svg")),
                      QStringLiteral("За замовчуванням"), page);
  connect(resetBtn, &QPushButton::clicked, this, &SettingsDialog::resetDbPath);

  QHBoxLayout * pathLayout = new QHBoxLayout;
  pathLayout->addWidget(dbPathEdit_);
  pathLayout->addWidget(browseBtn);
  pathLayout->addWidget(resetBtn);

  QFormLayout * form = new QFormLayout;
  form->addRow(QStringLiteral("Файл бази даних:"), pathLayout);

  layout->addLayout(form);

  QLabel * infoLabel = new QLabel(QStringLiteral("<i>Увага: Зміна розташування файлу бази даних "
                                                 "потребуватиме перезапуску програми.</i>"),
                                  page);
  infoLabel->setStyleSheet(QStringLiteral("color: #666; font-size: 11px;"));
  infoLabel->setWordWrap(true);
  layout->addWidget(infoLabel);

  layout->addStretch();
  return page;
}

QWidget * SettingsDialog::createAppearancePage()
{
  QWidget * page = new QWidget(this);
  QVBoxLayout * layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);

  QLabel * title = new QLabel(QStringLiteral("<h2>Вигляд та вікна</h2>"), page);
  layout->addWidget(title);

  QSettings settings;

  // Тема оформлення
  themeCombo_ = new QComboBox(page);
  themeCombo_->addItem(QStringLiteral("Світла"), QStringLiteral("light"));
  themeCombo_->addItem(QStringLiteral("Темна"), QStringLiteral("dark"));
  themeCombo_->addItem(QStringLiteral("Системна"), QStringLiteral("system"));

  QString currentTheme =
      settings.value(QStringLiteral("ui/theme"), QStringLiteral("system")).toString();
  int themeIdx = themeCombo_->findData(currentTheme);
  if (themeIdx >= 0)
    themeCombo_->setCurrentIndex(themeIdx);

  // Стиль вікон
  styleCombo_ = new QComboBox(page);
  styleCombo_->addItem(QStringLiteral("Windows"), QStringLiteral("windows"));
  styleCombo_->addItem(QStringLiteral("Fusion"), QStringLiteral("fusion"));
  styleCombo_->addItem(QStringLiteral("macOS"), QStringLiteral("macos"));

  QString currentStyle =
      settings.value(QStringLiteral("ui/style"), QStringLiteral("fusion")).toString();
  int styleIdx = styleCombo_->findData(currentStyle);
  if (styleIdx >= 0)
    styleCombo_->setCurrentIndex(styleIdx);

  // Поведінка Enter
  enterBehaviorCombo_ = new QComboBox(page);
  enterBehaviorCombo_->addItem(QStringLiteral("Завершення редагування і перехід на рядок нижче"),
                               true);
  enterBehaviorCombo_->addItem(QStringLiteral("Перехід на наступну колонку праворуч (як Tab)"),
                               false);

  bool enterDown = settings.value(QStringLiteral("ui/enter_moves_down"), true).toBool();
  int enterIdx = enterBehaviorCombo_->findData(enterDown);
  if (enterIdx >= 0)
    enterBehaviorCombo_->setCurrentIndex(enterIdx);

  // Бічна панель
  hideSidebarCheck_ = new QCheckBox(QStringLiteral("Приховувати бічну панель навігації"), page);
  hideSidebarCheck_->setChecked(settings.value(QStringLiteral("ui/hide_sidebar"), false).toBool());

  QFormLayout * form = new QFormLayout;
  form->addRow(QStringLiteral("Тема оформлення:"), themeCombo_);
  form->addRow(QStringLiteral("Стиль вікон:"), styleCombo_);
  form->addRow(QStringLiteral("Поведінка клавіші Enter:"), enterBehaviorCombo_);

  layout->addLayout(form);
  layout->addSpacing(10);
  layout->addWidget(hideSidebarCheck_);

  const ui::CalendarConfig calCfg = ui::loadCalendarConfig();

  QGroupBox * calGroup = new QGroupBox(QStringLiteral("Налаштування спливаючого календаря"), page);
  QHBoxLayout * calOuter = new QHBoxLayout(calGroup);

  QFormLayout * calForm = new QFormLayout;
  calFirstDayCombo_ = new QComboBox(calGroup);
  calFirstDayCombo_->addItem(QStringLiteral("Понеділок"), 1);
  calFirstDayCombo_->addItem(QStringLiteral("Неділя"), 7);
  calFirstDayCombo_->addItem(QStringLiteral("Системний (за замовчуванням)"), 0);
  {
    int idx = calFirstDayCombo_->findData(calCfg.firstDayOfWeek);
    if (idx < 0)
      idx = 0;
    calFirstDayCombo_->setCurrentIndex(idx);
  }

  calHeaderFormatCombo_ = new QComboBox(calGroup);
  calHeaderFormatCombo_->addItem(QStringLiteral("Короткі (Пн, Вт, Ср…)"),
                                 static_cast<int>(QCalendarWidget::ShortDayNames));
  calHeaderFormatCombo_->addItem(QStringLiteral("Однолітерні (П, В, С…)"),
                                 static_cast<int>(QCalendarWidget::SingleLetterDayNames));
  calHeaderFormatCombo_->addItem(QStringLiteral("Повні (Понеділок…)"),
                                 static_cast<int>(QCalendarWidget::LongDayNames));
  {
    int idx = calHeaderFormatCombo_->findData(static_cast<int>(calCfg.dayNamesFormat));
    if (idx < 0)
      idx = 0;
    calHeaderFormatCombo_->setCurrentIndex(idx);
  }

  calWeekNumbersCheck_ =
      new QCheckBox(QStringLiteral("Показувати номери тижнів (ISO 8601)"), calGroup);
  calWeekNumbersCheck_->setChecked(calCfg.showWeekNumbers);
  calGridCheck_ = new QCheckBox(QStringLiteral("Відображати сітку між датами"), calGroup);
  calGridCheck_->setChecked(calCfg.showGrid);
  calHighlightWeekendsCheck_ =
      new QCheckBox(QStringLiteral("Виділяти вихідні дні кольором"), calGroup);
  calHighlightWeekendsCheck_->setChecked(calCfg.highlightWeekends);
  calNavVisibleCheck_ =
      new QCheckBox(QStringLiteral("Відображати панель вибору місяця та року"), calGroup);
  calNavVisibleCheck_->setChecked(calCfg.showNavigation);

  calForm->addRow(QStringLiteral("Перший день тижня:"), calFirstDayCombo_);
  calForm->addRow(QStringLiteral("Формат назв днів:"), calHeaderFormatCombo_);
  calForm->addRow(calWeekNumbersCheck_);
  calForm->addRow(calGridCheck_);
  calForm->addRow(calHighlightWeekendsCheck_);
  calForm->addRow(calNavVisibleCheck_);

  calPreview_ = new QCalendarWidget(calGroup);
  calPreview_->setMinimumSize(280, 220);
  calPreview_->setMaximumHeight(260);
  calPreview_->setGridVisible(true);

  calOuter->addLayout(calForm, 1);
  calOuter->addWidget(calPreview_, 0);

  connect(calFirstDayCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &SettingsDialog::updateCalendarPreview);
  connect(calHeaderFormatCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &SettingsDialog::updateCalendarPreview);
  connect(calWeekNumbersCheck_, &QCheckBox::toggled, this, &SettingsDialog::updateCalendarPreview);
  connect(calGridCheck_, &QCheckBox::toggled, this, &SettingsDialog::updateCalendarPreview);
  connect(calHighlightWeekendsCheck_, &QCheckBox::toggled, this,
          &SettingsDialog::updateCalendarPreview);
  connect(calNavVisibleCheck_, &QCheckBox::toggled, this, &SettingsDialog::updateCalendarPreview);
  updateCalendarPreview();

  layout->addSpacing(12);
  layout->addWidget(calGroup);

  layout->addSpacing(20);

  QLabel * desc =
      new QLabel(QStringLiteral("Якщо ви випадково змінили розміри колонок у таблицях або "
                                "розташування вікон, ви можете повернути їх до початкового стану."),
                 page);
  desc->setWordWrap(true);
  layout->addWidget(desc);

  QPushButton * resetUiBtn =
      new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/wizard.svg")),
                      QStringLiteral("Скинути налаштування вікон та таблиць"), page);
  resetUiBtn->setFixedWidth(300);
  connect(resetUiBtn, &QPushButton::clicked, this, &SettingsDialog::resetUiState);

  layout->addSpacing(10);
  layout->addWidget(resetUiBtn);
  layout->addStretch();
  return page;
}

QWidget * SettingsDialog::createPrintPage()
{
  QWidget * page = new QWidget(this);
  QVBoxLayout * layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);

  QLabel * title = new QLabel(QStringLiteral("<h2>Друк</h2>"), page);
  layout->addWidget(title);

  const ui::PrintConfig printCfg = ui::loadPrintConfig();

  printPageNumbersCheck_ =
      new QCheckBox(QStringLiteral("Показувати номери сторінок (1/2, 2/2…)"), page);
  printPageNumbersCheck_->setChecked(printCfg.pageNumbers);

  printRepeatHeadersCheck_ = new QCheckBox(
      QStringLiteral("Повторювати рядок заголовків стовпців на кожній сторінці"), page);
  printRepeatHeadersCheck_->setChecked(printCfg.repeatTableHeaders);

  auto makeMarginSpin = [page](int value) {
    auto * spin = new QSpinBox(page);
    spin->setRange(5, 40);
    spin->setSuffix(QStringLiteral(" мм"));
    spin->setValue(value);
    return spin;
  };
  printMarginLeftSpin_ = makeMarginSpin(printCfg.marginLeftMm);
  printMarginRightSpin_ = makeMarginSpin(printCfg.marginRightMm);
  printMarginTopSpin_ = makeMarginSpin(printCfg.marginTopMm);
  printMarginBottomSpin_ = makeMarginSpin(printCfg.marginBottomMm);

  printFontSpin_ = new QDoubleSpinBox(page);
  printFontSpin_->setRange(8.0, 14.0);
  printFontSpin_->setSingleStep(0.5);
  printFontSpin_->setDecimals(1);
  printFontSpin_->setSuffix(QStringLiteral(" pt"));
  printFontSpin_->setValue(printCfg.fontPt);

  printWatermarkEdit_ = new QLineEdit(page);
  printWatermarkEdit_->setPlaceholderText(QStringLiteral("ЧЕРНЕТКА"));
  printWatermarkEdit_->setText(printCfg.watermarkText);
  printWatermarkEdit_->setClearButtonEnabled(true);

  printPageFilterCombo_ = new QComboBox(page);
  printPageFilterCombo_->addItem(QStringLiteral("Усі сторінки"), 0);
  printPageFilterCombo_->addItem(QStringLiteral("Лише непарні"), 1);
  printPageFilterCombo_->addItem(QStringLiteral("Лише парні"), 2);
  {
    int idx = printPageFilterCombo_->findData(printCfg.pageFilter);
    if (idx < 0)
      idx = 0;
    printPageFilterCombo_->setCurrentIndex(idx);
  }

  QFormLayout * form = new QFormLayout;
  form->addRow(QStringLiteral("Відступ зліва:"), printMarginLeftSpin_);
  form->addRow(QStringLiteral("Відступ справа:"), printMarginRightSpin_);
  form->addRow(QStringLiteral("Відступ зверху:"), printMarginTopSpin_);
  form->addRow(QStringLiteral("Відступ знизу:"), printMarginBottomSpin_);
  form->addRow(QStringLiteral("Розмір шрифту:"), printFontSpin_);
  form->addRow(QStringLiteral("Водяний знак:"), printWatermarkEdit_);
  form->addRow(QStringLiteral("Сторінки:"), printPageFilterCombo_);

  layout->addSpacing(10);
  layout->addWidget(printPageNumbersCheck_);
  layout->addWidget(printRepeatHeadersCheck_);
  layout->addSpacing(10);
  layout->addLayout(form);

  QLabel * hint =
      new QLabel(QStringLiteral("<i>Ці параметри застосовуються до попереднього перегляду друку "
                                "та збереження PDF для звітів і документів. Порожній водяний знак "
                                "не друкується. Для альбомної орієнтації шрифт трохи менший.</i>"),
                 page);
  hint->setStyleSheet(QStringLiteral("color: #666;"));
  hint->setWordWrap(true);
  layout->addSpacing(10);
  layout->addWidget(hint);

  layout->addStretch();
  return page;
}

QWidget * SettingsDialog::createBackupPage()
{
  QWidget * page = new QWidget(this);
  QVBoxLayout * layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);

  QLabel * title = new QLabel(QStringLiteral("<h2>Безпека та Резервне копіювання</h2>"), page);
  layout->addWidget(title);

  QSettings settings;
  suggestBackupCheck_ =
      new QCheckBox(QStringLiteral("Пропонувати створити резервну копію при виході"), page);
  suggestBackupCheck_->setChecked(
      settings.value(QStringLiteral("security/suggest_backup"), true).toBool());

  autoLockSpinBox_ = new QSpinBox(page);
  autoLockSpinBox_->setRange(0, 120);
  autoLockSpinBox_->setSuffix(QStringLiteral(" хв"));
  autoLockSpinBox_->setSpecialValueText(QStringLiteral("Вимкнено"));
  autoLockSpinBox_->setValue(
      settings.value(QStringLiteral("security/auto_lock_minutes"), 0).toInt());
  const Result<bool> needPass = UserRepository(session_.handle()).passwordRequired();
  if (!needPass || !needPass.value()) {
    autoLockSpinBox_->setEnabled(false);
    autoLockSpinBox_->setToolTip(
        QStringLiteral("Автоблокування доступне лише коли в системі є паролі"));
    if (autoLockSpinBox_->value() > 0) {
      autoLockSpinBox_->setValue(0);
    }
  }

  QFormLayout * form = new QFormLayout;
  form->addRow(QStringLiteral("Автоматичне блокування екрану через:"), autoLockSpinBox_);

  layout->addWidget(suggestBackupCheck_);
  layout->addSpacing(10);
  layout->addLayout(form);

  QLabel * hint =
      new QLabel(QStringLiteral("<i>Регулярне резервне копіювання збереже ваші дані від втрати. "
                                "Автоблокування вимагає встановлених паролів користувачів.</i>"),
                 page);
  hint->setStyleSheet(QStringLiteral("color: #666;"));
  layout->addWidget(hint);

  layout->addStretch();
  return page;
}

QWidget * SettingsDialog::createAutomationPage()
{
  QWidget * page = new QWidget(this);
  QVBoxLayout * layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);

  QLabel * title = new QLabel(QStringLiteral("<h2>Автоматизація та Звуки</h2>"), page);
  layout->addWidget(title);

  QSettings settings;
  syncNbuCheck_ = new QCheckBox(
      QStringLiteral("Автоматично завантажувати офіційні курси валют НБУ (щодня)"), page);
  syncNbuCheck_->setChecked(settings.value(QStringLiteral("automation/sync_nbu"), true).toBool());

  soundEnabledCheck_ = new QCheckBox(
      QStringLiteral("Відтворювати звуковий сигнал при успішному проведенні документів"), page);
  soundEnabledCheck_->setChecked(settings.value(QStringLiteral("ui/sound_enabled"), true).toBool());

  layout->addSpacing(10);
  layout->addWidget(syncNbuCheck_);
  layout->addWidget(soundEnabledCheck_);

  QLabel * hint = new QLabel(QStringLiteral("<i>Оновлення курсів потребує підключення до "
                                            "Інтернету при запуску програми.</i>"),
                             page);
  hint->setStyleSheet(QStringLiteral("color: #666;"));
  layout->addWidget(hint);

  layout->addStretch();
  return page;
}

void SettingsDialog::browseDbPath()
{
  const QString dir = dbPathEdit_->text().isEmpty()
                          ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                          : QFileInfo(dbPathEdit_->text()).absolutePath();

  const QString path =
      QFileDialog::getSaveFileName(this, QStringLiteral("Виберіть файл бази даних"), dir,
                                   QStringLiteral("База даних SQLite (*.sqlite);;Всі файли (*.*)"));

  if (!path.isEmpty()) {
    dbPathEdit_->setText(path);
  }
}

void SettingsDialog::resetDbPath()
{
  dbPathEdit_->setText(defaultDatabaseFilePath());
}

void SettingsDialog::resetUiState()
{
  if (QMessageBox::question(
          this, QStringLiteral("Скидання налаштувань"),
          QStringLiteral("Ви впевнені, що бажаєте скинути всі налаштування розмірів вікон "
                         "та колонок?\nЦе не вплине на ваші облікові дані.")) == QMessageBox::Yes) {

    QSettings settings;
    // Ми не використовуємо clear(), щоб не видалити налаштування БД чи шрифту.
    // Видаляємо лише ключі геометрії та стану таблиць
    QStringList keys = settings.allKeys();
    for (const QString & key : keys) {
      if (key == QStringLiteral("geometry") || key.endsWith(QStringLiteral("/geometry")) ||
          key.endsWith(QStringLiteral("/state")) || key.endsWith(QStringLiteral("/splitter"))) {
        settings.remove(key);
      }
    }
    QMessageBox::information(this, QStringLiteral("Готово"),
                             QStringLiteral("Налаштування вікон успішно скинуто."));
  }
}

void SettingsDialog::acceptChanges()
{
  QSettings settings;
  bool requiresRestart = false;

  int newFontSize = fontSizeCombo_->currentText().toInt();
  if (settings.value(QStringLiteral("ui/font_size"), 10).toInt() != newFontSize) {
    settings.setValue(QStringLiteral("ui/font_size"), newFontSize);
    requiresRestart = true;
  }

  settings.setValue(QStringLiteral("ui/confirm_exit"), confirmExitCheck_->isChecked());
  settings.setValue(QStringLiteral("ui/restore_tabs"), restoreTabsCheck_->isChecked());
  settings.setValue(QStringLiteral("ui/theme"), themeCombo_->currentData());
  settings.setValue(QStringLiteral("ui/style"), styleCombo_->currentData());
  settings.setValue(QStringLiteral("ui/hide_sidebar"), hideSidebarCheck_->isChecked());
  settings.setValue(QStringLiteral("ui/enter_moves_down"), enterBehaviorCombo_->currentData());

  ui::CalendarConfig calCfg;
  calCfg.firstDayOfWeek = calFirstDayCombo_->currentData().toInt();
  calCfg.dayNamesFormat = static_cast<QCalendarWidget::HorizontalHeaderFormat>(
      calHeaderFormatCombo_->currentData().toInt());
  calCfg.showWeekNumbers = calWeekNumbersCheck_->isChecked();
  calCfg.showGrid = calGridCheck_->isChecked();
  calCfg.highlightWeekends = calHighlightWeekendsCheck_->isChecked();
  calCfg.showNavigation = calNavVisibleCheck_->isChecked();
  ui::saveCalendarConfig(calCfg);
  ui::reapplyCalendarConfigToAll();

  ui::PrintConfig printCfg;
  printCfg.pageNumbers = printPageNumbersCheck_->isChecked();
  printCfg.repeatTableHeaders = printRepeatHeadersCheck_->isChecked();
  printCfg.marginLeftMm = printMarginLeftSpin_->value();
  printCfg.marginRightMm = printMarginRightSpin_->value();
  printCfg.marginTopMm = printMarginTopSpin_->value();
  printCfg.marginBottomMm = printMarginBottomSpin_->value();
  printCfg.fontPt = printFontSpin_->value();
  printCfg.watermarkText = printWatermarkEdit_->text().trimmed();
  printCfg.pageFilter = printPageFilterCombo_->currentData().toInt();
  ui::savePrintConfig(printCfg);

  int autoLockMinutes = autoLockSpinBox_->value();
  if (autoLockMinutes > 0) {
    const Result<bool> needPass = UserRepository(session_.handle()).passwordRequired();
    if (!needPass || !needPass.value()) {
      QMessageBox::warning(
          this, QStringLiteral("Автоблокування"),
          QStringLiteral("Неможливо увімкнути автоблокування: у системі немає паролів. "
                         "Спочатку встановіть пароль користувача."));
      return;
    }
  }
  settings.setValue(QStringLiteral("security/suggest_backup"), suggestBackupCheck_->isChecked());
  settings.setValue(QStringLiteral("security/auto_lock_minutes"), autoLockMinutes);

  settings.setValue(QStringLiteral("automation/sync_nbu"), syncNbuCheck_->isChecked());
  settings.setValue(QStringLiteral("ui/sound_enabled"), soundEnabledCheck_->isChecked());

  if (allowNegativeStockCheck_->isEnabled()) {
    Result<CompanyProfile> company = session_.company().load();
    if (!company) {
      ui::showError(this, company.error());
      return;
    }
    CompanyProfile profile = company.value();
    if (profile.allowNegativeStock != allowNegativeStockCheck_->isChecked()) {
      profile.allowNegativeStock = allowNegativeStockCheck_->isChecked();
      const Status saved = session_.company().save(profile);
      if (!saved) {
        ui::showError(this, saved);
        return;
      }
    }
  }
  settings.setValue(QStringLiteral("business/prefix_strategy"),
                    prefixStrategyCombo_->currentData());
  settings.setValue(QStringLiteral("business/doc_prefix"), docPrefixEdit_->text().trimmed());

  QString newPath = dbPathEdit_->text();
  const QString defaultPath = defaultDatabaseFilePath();

  QString settingValue = (newPath == defaultPath) ? QString() : newPath;
  if (settingValue != originalPath_) {
    settings.setValue(QStringLiteral("database/custom_path"), settingValue);
    requiresRestart = true;
  }

  settings.sync();

  if (requiresRestart) {
    QMessageBox::information(this, QStringLiteral("Налаштування збережено"),
                             QStringLiteral("Деякі з ваших змін (розмір шрифту або шлях до БД)\n"
                                            "потребують перезапуску програми для застосування."));
  }

  accept();
}

void SettingsDialog::updateCalendarPreview()
{
  if (calPreview_ == nullptr) {
    return;
  }
  ui::CalendarConfig cfg;
  cfg.firstDayOfWeek = calFirstDayCombo_->currentData().toInt();
  cfg.dayNamesFormat = static_cast<QCalendarWidget::HorizontalHeaderFormat>(
      calHeaderFormatCombo_->currentData().toInt());
  cfg.showWeekNumbers = calWeekNumbersCheck_->isChecked();
  cfg.showGrid = calGridCheck_->isChecked();
  cfg.highlightWeekends = calHighlightWeekendsCheck_->isChecked();
  cfg.showNavigation = calNavVisibleCheck_->isChecked();
  ui::applyCalendarConfig(calPreview_, cfg);
}

} // namespace fin
