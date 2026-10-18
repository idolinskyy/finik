#include "ui/MainWindow.h"
#include "ui/SettingsDialog.h"

#include "OpeningBalancesWizard.h"
#include "PayrollDialog.h"
#include "ui/AccountAnalysisPage.h"
#include "ui/AccountCardPage.h"
#include "ui/AccountPage.h"
#include "ui/AuthDialogs.h"
#include "ui/BomPage.h"
#include "ui/CashBookPage.h"
#include "ui/CashFlowPage.h"
#include "ui/CompanyDialog.h"
#include "ui/ConsolidatedTaxPage.h"
#include "ui/CounterpartyPage.h"
#include "ui/ContractPage.h"
#include "ui/CounterpartyTurnoverPage.h"
#include "ui/CurrencyPage.h"
#include "ui/DashboardPage.h"
#include "ui/DocumentDialog.h"
#include "ui/DocumentJournalPage.h"
#include "ui/EmployeePage.h"
#include "ui/FinancialStatementPage.h"
#include "ui/FixedAssetPage.h"
#include "ui/HelpBrowser.h"
#include "ui/IncomeBookPage.h"
#include "ui/IntangiblePage.h"
#include "ui/LowValuePage.h"
#include "ui/NomenclaturePage.h"
#include "ui/PeriodDialog.h"
#include "ui/ReconciliationPage.h"
#include "ui/SearchDialog.h"
#include "ui/StockPage.h"
#include "ui/TaxInvoiceDialog.h"
#include "ui/TimesheetPage.h"
#include "ui/TrialBalancePage.h"
#include "ui/SubcontoTrialBalancePage.h"
#include "ui/UiSupport.h"
#include "ui/VatStatementPage.h"
#include "ui/WarehousePage.h"

#include "fin/auth/Access.h"
#include "fin/currency/NbuRateClient.h"
#include "fin/db/UserRepository.h"
#include "fin/demo/DemoSeed.h"
#include "fin/domain/ChartCodes.h"
#include "fin/edo/TaxInvoiceXml.h"
#include "fin/integrity/IntegrityChecker.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDate>
#include <QDateTime>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QIODevice>
#include <QKeySequence>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QStringList>
#include <QTabWidget>
#include <QTimer>
#include <QWhatsThis>

#include <functional>

namespace fin {

class AutoLockFilter : public QObject
{
 public:
  AutoLockFilter(LedgerSession & session, std::function<void()> onUnlocked,
                 QObject * parent = nullptr)
      : QObject(parent), session_(session), onUnlocked_(std::move(onUnlocked))
  {
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &AutoLockFilter::lockApp);
    updateInterval();
  }

  void updateInterval()
  {
    QSettings settings;
    const int minutes = settings.value(QStringLiteral("security/auto_lock_minutes"), 0).toInt();
    UserRepository users(session_.handle());
    const Result<bool> needPass = users.passwordRequired();
    if (minutes > 0 && needPass && needPass.value()) {
      timer_.setInterval(minutes * 60 * 1000);
      timer_.start();
    } else {
      timer_.stop();
    }
  }

 protected:
  bool eventFilter(QObject * obj, QEvent * event) override
  {
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::MouseMove ||
        event->type() == QEvent::MouseButtonPress) {
      if (timer_.isActive()) {
        timer_.start(); // restart timer
      }
    }
    return QObject::eventFilter(obj, event);
  }

 private:
  void lockApp()
  {
    if (isLocking_) {
      return;
    }
    isLocking_ = true;
    UserRepository users(session_.handle());
    const Result<bool> needPass = users.passwordRequired();
    if (!needPass || !needPass.value()) {
      isLocking_ = false;
      updateInterval();
      return;
    }
    const Status cleared = users.clearCurrentUser();
    if (!cleared) {
      isLocking_ = false;
      updateInterval();
      return;
    }
    fin::LoginDialog login(session_);
    login.setWindowFlags(login.windowFlags() | Qt::WindowStaysOnTopHint);
    if (login.exec() != QDialog::Accepted || !login.acceptedLogin()) {
      QApplication::quit();
      isLocking_ = false;
      return;
    }
    if (onUnlocked_) {
      onUnlocked_();
    }
    isLocking_ = false;
    updateInterval();
  }

  LedgerSession & session_;
  std::function<void()> onUnlocked_;
  QTimer timer_;
  bool isLocking_ = false;
};

class EnterBehaviorFilter : public QObject
{
 public:
  explicit EnterBehaviorFilter(QObject * parent = nullptr) : QObject(parent) {}

 protected:
  bool eventFilter(QObject * obj, QEvent * event) override
  {
    if (event->type() == QEvent::KeyPress) {
      QKeyEvent * keyEvent = static_cast<QKeyEvent *>(event);
      if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
        // Якщо натиснуто Enter у таблиці або її редакторі
        if (obj->inherits("QTableView") ||
            (obj->parent() && obj->parent()->inherits("QTableView")) ||
            (obj->parent() && obj->parent()->parent() &&
             obj->parent()->parent()->inherits("QTableView"))) {
          QSettings settings;
          if (!settings.value(QStringLiteral("ui/enter_moves_down"), true).toBool()) {
            // Емулюємо натискання Tab
            QKeyEvent tabEvent(QEvent::KeyPress, Qt::Key_Tab, keyEvent->modifiers(),
                               QStringLiteral("\t"));
            QApplication::sendEvent(obj, &tabEvent);
            return true;
          }
        }
      }
    }
    return QObject::eventFilter(obj, event);
  }
};

MainWindow::MainWindow(LedgerSession & session, QWidget * parent)
    : QMainWindow(parent), session_(session)
{
  setWindowTitle(QStringLiteral("Фінік — облік"));
  setMinimumSize(800, 600);
  QSettings settings;
  if (settings.contains(QStringLiteral("geometry"))) {
    restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
  } else {
    resize(1280, 760);
  }
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  autoLockFilter_ = new AutoLockFilter(
      session_,
      [this]() {
        applyAccessGate();
        refreshStatus();
      },
      this);
  qApp->installEventFilter(autoLockFilter_);

  qApp->installEventFilter(new EnterBehaviorFilter(this));

  auto * fileMenu = menuBar()->addMenu(QStringLiteral("Файл"));
  auto * searchAction = fileMenu->addAction(
      ui::themeIcon(QStringLiteral(":/help/icons/search.svg")), QStringLiteral("Пошук…"));
  searchAction->setShortcut(QKeySequence::Find);
  searchAction->setWhatsThis(QStringLiteral("Документи, довідники і рахунки. Відкриває знайдене."));
  auto * companyAction = fileMenu->addAction(
      ui::themeIcon(QStringLiteral(":/help/icons/desktop.svg")), QStringLiteral("Підприємство…"));
  auto * settingsAction = fileMenu->addAction(
      ui::themeIcon(QStringLiteral(":/help/icons/settings.svg")), QStringLiteral("Налаштування…"));
  auto * importPnAction =
      fileMenu->addAction(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                          QStringLiteral("Імпорт ПН (XML)…"));
  auto * passwordAction = fileMenu->addAction(
      ui::themeIcon(QStringLiteral(":/help/icons/settings.svg")), QStringLiteral("Пароль…"));
  usersAction_ = fileMenu->addAction(ui::themeIcon(QStringLiteral(":/help/icons/users.svg")),
                                     QStringLiteral("Користувачі…"));
  auto * auditAction = fileMenu->addAction(ui::themeIcon(QStringLiteral(":/help/icons/clock.svg")),
                                           QStringLiteral("Журнал змін…"));
  backupAction_ = fileMenu->addAction(ui::themeIcon(QStringLiteral(":/help/icons/warehouse.svg")),
                                      QStringLiteral("Резервна копія…"));
  restoreAction_ = fileMenu->addAction(ui::themeIcon(QStringLiteral(":/help/icons/wizard.svg")),
                                       QStringLiteral("Відновити з копії…"));
  auto * periodMenu = menuBar()->addMenu(QStringLiteral("Облік"));
  auto * periodAction = periodMenu->addAction(
      ui::themeIcon(QStringLiteral(":/help/icons/clock.svg")), QStringLiteral("Закритий період…"));
  auto * verifyAction =
      periodMenu->addAction(ui::themeIcon(QStringLiteral(":/help/icons/sparkles.svg")),
                            QStringLiteral("Перевірка бази…"));
  seedAction_ = periodMenu->addAction(ui::themeIcon(QStringLiteral(":/help/icons/cube.svg")),
                                      QStringLiteral("Демо-дані…"));
  auto * helpMenu = menuBar()->addMenu(QStringLiteral("Довідка"));
  auto * helpAction = helpMenu->addAction(
      ui::themeIcon(QStringLiteral(":/help/icons/document.svg")), QStringLiteral("Зміст"));
  helpAction->setShortcut(QKeySequence::HelpContents);
  auto * aboutAction = helpMenu->addAction(
      ui::themeIcon(QStringLiteral(":/help/icons/sparkles.svg")), QStringLiteral("Про програму…"));
  auto * aboutQtAction = helpMenu->addAction(QStringLiteral("Про Qt…"));
  auto * from1cAction = helpMenu->addAction(
      ui::themeIcon(QStringLiteral(":/help/icons/wizard.svg")), QStringLiteral("Якщо ви з 1С"));
  auto * whatsThisAction =
      helpMenu->addAction(ui::themeIcon(QStringLiteral(":/help/icons/dashboard.svg")),
                          QStringLiteral("Що це? (по кнопках)"));
  connect(searchAction, &QAction::triggered, this, &MainWindow::openSearch);
  connect(companyAction, &QAction::triggered, this, &MainWindow::editCompany);
  connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettings);
  connect(importPnAction, &QAction::triggered, this, &MainWindow::importTaxInvoice);
  connect(passwordAction, &QAction::triggered, this, &MainWindow::editPassword);
  connect(usersAction_, &QAction::triggered, this, &MainWindow::editUsers);
  connect(auditAction, &QAction::triggered, this, &MainWindow::showAudit);
  connect(backupAction_, &QAction::triggered, this, &MainWindow::backupDatabase);
  connect(restoreAction_, &QAction::triggered, this, &MainWindow::restoreDatabase);
  connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
  connect(aboutQtAction, &QAction::triggered, this, [this]() { QMessageBox::aboutQt(this); });
  connect(periodAction, &QAction::triggered, this, &MainWindow::editPeriod);
  connect(verifyAction, &QAction::triggered, this, &MainWindow::verifyDatabase);
  connect(seedAction_, &QAction::triggered, this, &MainWindow::seedDemo);
  connect(helpAction, &QAction::triggered, this, &MainWindow::openHelp);
  connect(from1cAction, &QAction::triggered, this, &MainWindow::openHelpFrom1c);
  connect(whatsThisAction, &QAction::triggered, this, &QWhatsThis::enterWhatsThisMode);

  applyAccessGate();

  auto * root = new QWidget(this);
  root->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  auto * layout = new QHBoxLayout(root);
  auto * nav = new QTreeWidget;
  nav->setHeaderHidden(true);
  nav->setAnimated(true);

  auto addGroup = [&](const QString & title) {
    auto * group = new QTreeWidgetItem(nav);
    group->setText(0, title);
    QFont f = group->font(0);
    f.setBold(true);
    group->setFont(0, f);
    group->setFlags(group->flags() & ~Qt::ItemIsSelectable);
    return group;
  };

  auto addItem = [&](QTreeWidgetItem * parentItem, const QString & title, int id,
                     const QString & whatsThis, const QString & iconPath = QString()) {
    auto * item = new QTreeWidgetItem(parentItem);
    item->setText(0, title);
    item->setData(0, Qt::UserRole, id);
    item->setWhatsThis(0, whatsThis);
    if (!iconPath.isEmpty()) {
      item->setIcon(0, ui::themeIcon(iconPath));
    }
    return item;
  };

  auto * gMain = addGroup(QStringLiteral("Головна"));
  addItem(gMain, QStringLiteral("Дашборд"), 23, QStringLiteral("Поточний стан підприємства"),
          QStringLiteral(":/help/icons/dashboard.svg"));

  auto * gService = addGroup(QStringLiteral("Сервіс"));
  addItem(gService, QStringLiteral("Майстер залишків"), 25,
          QStringLiteral("Введення початкових залишків"),
          QStringLiteral(":/help/icons/wizard.svg"));

  auto * gRef = addGroup(QStringLiteral("Довідники"));
  addItem(gRef, QStringLiteral("План рахунків"), 0,
          QStringLiteral("План рахунків. Папки не приймають проводок."),
          QStringLiteral(":/help/icons/account.svg"));
  addItem(gRef, QStringLiteral("Контрагенти"), 1, QStringLiteral(""),
          QStringLiteral(":/help/icons/counterparty.svg"));
  addItem(gRef, QStringLiteral("Договори"), 26,
          QStringLiteral("Договори з покупцями та постачальниками для аналітики документів."),
          QStringLiteral(":/help/icons/document.svg"));
  addItem(gRef, QStringLiteral("Номенклатура"), 2, QStringLiteral(""),
          QStringLiteral(":/help/icons/cube.svg"));
  addItem(gRef, QStringLiteral("Специфікації (BOM)"), 3,
          QStringLiteral("Специфікації виробів (BOM): нормативний склад "
                         "матеріалів для випуску продукції."),
          QStringLiteral(":/help/icons/bom.svg"));
  addItem(gRef, QStringLiteral("Склади"), 4,
          QStringLiteral("Довідник місць зберігання (складів) для обліку залишків товарів."),
          QStringLiteral(":/help/icons/warehouse.svg"));
  addItem(gRef, QStringLiteral("Основні засоби"), 5,
          QStringLiteral("Картки основних засобів: інвентарний номер, рахунок "
                         "104/105/109, знос."),
          QStringLiteral(":/help/icons/desktop.svg"));
  addItem(gRef, QStringLiteral("Нематеріальні активи"), 6,
          QStringLiteral("Нематеріальні активи: інвентарний номер, рахунок "
                         "125/127, амортизація 133."),
          QStringLiteral(":/help/icons/sparkles.svg"));
  addItem(gRef, QStringLiteral("МШП"), 7,
          QStringLiteral("Малоцінні та швидкозношувані предмети: картки рахунку 22."),
          QStringLiteral(":/help/icons/scissors.svg"));
  addItem(gRef, QStringLiteral("Працівники"), 8,
          QStringLiteral("Працівники: РНОКПП, оклад, нарахування зарплати (ЗП)."),
          QStringLiteral(":/help/icons/users.svg"));
  addItem(gRef, QStringLiteral("Валюти та курси"), 9,
          QStringLiteral("Довідник валют (USD, EUR, PLN) та історія курсів НБУ."),
          QStringLiteral(":/help/icons/currency.svg"));

  auto * gDoc = addGroup(QStringLiteral("Документи"));
  addItem(gDoc, QStringLiteral("Журнал документів"), 10,
          QStringLiteral("Журнал усіх документів. Створюйте надходження, "
                         "реалізацію і ПКО звідси."),
          QStringLiteral(":/help/icons/document.svg"));
  addItem(gDoc, QStringLiteral("Табель (П-5)"), 21, QStringLiteral(""),
          QStringLiteral(":/help/icons/clock.svg"));

  auto * gMoney = addGroup(QStringLiteral("Гроші та Банк"));
  addItem(gMoney, QStringLiteral("Касова книга"), 15,
          QStringLiteral("Касова книга по рахунку 301. Має сходитися з карткою каси."),
          QStringLiteral(":/help/icons/cash.svg"));
  addItem(gMoney, QStringLiteral("Виписка банку"), 16,
          QStringLiteral("Виписка по рахунку 311. Має сходитися з карткою банку."),
          QStringLiteral(":/help/icons/library.svg"));

  auto * gRep = addGroup(QStringLiteral("Звіти"));
  addItem(gRep, QStringLiteral("ОСВ"), 11,
          QStringLiteral("Оборотно-сальдова відомість за вибраний період."),
          QStringLiteral(":/help/icons/report.svg"));
  addItem(gRep, QStringLiteral("ОСВ по субконто"), 27,
          QStringLiteral("Оборотно-сальдова відомість рахунку в розрізі субконто."),
          QStringLiteral(":/help/icons/chart.svg"));
  addItem(gRep, QStringLiteral("Аналіз рахунку"), 24, QStringLiteral(""),
          QStringLiteral(":/help/icons/chart.svg"));
  addItem(gRep, QStringLiteral("Картка рахунку"), 12, QStringLiteral(""),
          QStringLiteral(":/help/icons/report.svg"));
  addItem(gRep, QStringLiteral("Обороти з контрагентами"), 13, QStringLiteral(""),
          QStringLiteral(":/help/icons/report.svg"));
  addItem(gRep, QStringLiteral("Залишки товарів"), 14, QStringLiteral(""),
          QStringLiteral(":/help/icons/report.svg"));
  addItem(gRep, QStringLiteral("Баланс і фінрезультат"), 17,
          QStringLiteral("Баланс на дату «по» і звіт про фінрезультати за період."),
          QStringLiteral(":/help/icons/balance.svg"));
  addItem(gRep, QStringLiteral("Рух коштів"), 28,
          QStringLiteral("Управлінський рух коштів по 301/302 і 311/312; рентабельність."),
          QStringLiteral(":/help/icons/cash.svg"));
  addItem(gRep, QStringLiteral("Книга доходів"), 18,
          QStringLiteral("Книга доходів спрощенця: кредит 701/703 і єдиний податок."),
          QStringLiteral(":/help/icons/report.svg"));
  addItem(gRep, QStringLiteral("ПДВ"), 19,
          QStringLiteral("Відомість ПДВ: обороти 641, 643 і 644 за період ОСВ."),
          QStringLiteral(":/help/icons/report.svg"));
  addItem(gRep, QStringLiteral("Об'єднана звітність"), 22,
          QStringLiteral("Об'єднана звітність з ПДФО та ЄСВ (XML)."),
          QStringLiteral(":/help/icons/report.svg"));
  addItem(gRep, QStringLiteral("Акт звірки"), 20,
          QStringLiteral("Акт звірки розрахунків з контрагентом по 361, 681, 631 або 371."),
          QStringLiteral(":/help/icons/report.svg"));

  nav->expandAll();
  nav->setFixedWidth(260);
  nav_ = nav;

  tabs_ = new QTabWidget;
  tabs_->setTabsClosable(true);
  connect(tabs_, &QTabWidget::tabCloseRequested, tabs_,
          [this](int index) { tabs_->removeTab(index); });

  // Store all pages in a vector so we can add them to tabs on demand
  pages_.resize(29);
  accounts_ = new AccountPage(session_);
  counterparties_ = new CounterpartyPage(session_);
  contracts_ = new ContractPage(session_);
  nomenclature_ = new NomenclaturePage(session_);
  bom_ = new BomPage(session_);
  warehouses_ = new WarehousePage(session_);
  assets_ = new FixedAssetPage(session_);
  intangibles_ = new IntangiblePage(session_);
  lowValue_ = new LowValuePage(session_);
  employees_ = new EmployeePage(session_);
  currencies_ = new CurrencyPage(session_);
  journal_ = new DocumentJournalPage(session_);
  auto openTab = [this](int id, const QString & title, const QIcon & icon = QIcon()) {
    QWidget * page = pages_[id];
    int index = tabs_->indexOf(page);
    if (index >= 0) {
      tabs_->setCurrentIndex(index);
    } else {
      index = tabs_->addTab(page, icon, title);
      tabs_->setCurrentIndex(index);
    }
  };

  auto * osv = new TrialBalancePage(session_);
  card_ = new AccountCardPage(session_);

  connect(osv, &TrialBalancePage::openAccountCard, this,
          [this, openTab](AccountId id, QDate from, QDate to) {
            card_->setAccountAndPeriod(id, from, to);
            openTab(12, QStringLiteral("Картка рахунку"),
                    ui::themeIcon(QStringLiteral(":/help/icons/report.svg")));
          });

  connect(osv, &TrialBalancePage::openAccountAnalysis, this,
          [this, openTab](AccountId id, QDate from, QDate to) {
            analysis_->setAccountAndPeriod(id, from, to);
            openTab(24, QStringLiteral("Аналіз рахунку"),
                    ui::themeIcon(QStringLiteral(":/help/icons/chart.svg")));
          });
  connect(osv, &TrialBalancePage::openSubcontoTrialBalance, this,
          [this, openTab](AccountId id, QDate from, QDate to) {
            subcontoOsv_->setAccountAndPeriod(id, from, to);
            openTab(27, QStringLiteral("ОСВ по субконто"),
                    ui::themeIcon(QStringLiteral(":/help/icons/chart.svg")));
          });
  auto * turnovers = new CounterpartyTurnoverPage(session_);
  auto * stock = new StockPage(session_);
  auto * cashBook = new CashBookPage(session_, chart::kCash, QStringLiteral("Касова книга"));
  auto * bankBook = new CashBookPage(session_, chart::kBank, QStringLiteral("Виписка банку"));
  auto * statements = new FinancialStatementPage(session_);
  auto * cashFlow = new CashFlowPage(session_);
  auto * incomeBook = new IncomeBookPage(session_);
  auto * vat = new VatStatementPage(session_);
  auto * reconciliation = new ReconciliationPage(session_);
  timesheet_ = new TimesheetPage(session_);
  consolidatedTax_ = new ConsolidatedTaxPage(session_);
  dashboard_ = new DashboardPage(session_);
  analysis_ = new AccountAnalysisPage(session_);
  subcontoOsv_ = new SubcontoTrialBalancePage(session_);

  pages_[0] = accounts_;
  pages_[1] = counterparties_;
  pages_[2] = nomenclature_;
  pages_[3] = bom_;
  pages_[4] = warehouses_;
  pages_[5] = assets_;
  pages_[6] = intangibles_;
  pages_[7] = lowValue_;
  pages_[8] = employees_;
  pages_[9] = currencies_;
  pages_[10] = journal_;
  pages_[11] = osv;
  pages_[12] = card_;
  pages_[13] = turnovers;
  pages_[14] = stock;
  pages_[15] = cashBook;
  pages_[16] = bankBook;
  pages_[17] = statements;
  pages_[28] = cashFlow;
  pages_[18] = incomeBook;
  pages_[19] = vat;
  pages_[20] = reconciliation;
  pages_[21] = timesheet_;
  pages_[22] = consolidatedTax_;
  pages_[23] = dashboard_;
  pages_[24] = analysis_;
  pages_[26] = contracts_;
  pages_[27] = subcontoOsv_;

  layout->addWidget(nav);
  layout->addWidget(tabs_, 1);
  setCentralWidget(root);

  connect(nav, &QTreeWidget::itemClicked, this, [=](QTreeWidgetItem * item, int /*column*/) {
    if (item->childCount() > 0)
      return; // Group header
    int row = item->data(0, Qt::UserRole).toInt();
    if (row == 25) {
      openOpeningBalancesWizard();
      return;
    }
    QWidget * page = pages_[row];
    int tabIndex = tabs_->indexOf(page);
    if (tabIndex == -1) {
      tabIndex = tabs_->addTab(page, item->icon(0), item->text(0));
    }
    tabs_->setCurrentIndex(tabIndex);
    switch (row) {
    case 0:
      accounts_->reload();
      break;
    case 1:
      counterparties_->reload();
      break;
    case 26:
      contracts_->reload();
      break;
    case 2:
      nomenclature_->reload();
      break;
    case 3:
      bom_->reload();
      break;
    case 4:
      warehouses_->reload();
      break;
    case 5:
      assets_->reload();
      break;
    case 6:
      intangibles_->reload();
      break;
    case 7:
      lowValue_->reload();
      break;
    case 8:
      employees_->reload();
      break;
    case 9:
      currencies_->reload();
      break;
    case 10:
      journal_->reload();
      break;
    case 11:
      osv->rebuild();
      break;
    case 12:
      card_->rebuild();
      break;
    case 13:
      turnovers->rebuild();
      break;
    case 14:
      stock->rebuild();
      break;
    case 15:
      cashBook->rebuild();
      break;
    case 16:
      bankBook->rebuild();
      break;
    case 17:
      statements->rebuild();
      break;
    case 28:
      cashFlow->rebuild();
      break;
    case 18:
      incomeBook->rebuild();
      break;
    case 19:
      vat->rebuild();
      break;
    case 20:
      reconciliation->rebuild();
      break;
    case 21:
      timesheet_->reload();
      break;
    default:
      break;
    }
  });
  selectNav(23); // Open Dashboard by default
  refreshStatus();

  nbuClient_ = new NbuRateClient(this);

  // Фонове оновлення курсів валют НБУ на поточну дату раз на добу
  syncDailyRates();

  // Періодичний таймер для оновлення курсів при зміні доби (кожні 2 години)
  dailyRateTimer_ = new QTimer(this);
  connect(dailyRateTimer_, &QTimer::timeout, this, &MainWindow::syncDailyRates);

  if (settings.value(QStringLiteral("automation/sync_nbu"), true).toBool()) {
    syncDailyRates();
    dailyRateTimer_->start(3 * 60 * 60 * 1000); // 3 години
  }

  // Restore open tabs
  if (settings.value(QStringLiteral("ui/restore_tabs"), true).toBool()) {
    QList<QVariant> savedTabs = settings.value(QStringLiteral("ui/open_tabs")).toList();
    for (const QVariant & v : savedTabs) {
      int row = v.toInt();
      selectNav(row);
    }
  } else {
    selectNav(23); // Dashboard
  }

  if (settings.value(QStringLiteral("ui/hide_sidebar"), false).toBool()) {
    nav_->hide();
  }

  refreshStatus();
}

void MainWindow::closeEvent(QCloseEvent * event)
{
  QSettings settings;
  if (settings.value(QStringLiteral("ui/confirm_exit"), false).toBool()) {
    QMessageBox::StandardButton resBtn =
        QMessageBox::question(this, QStringLiteral("Підтвердження виходу"),
                              QStringLiteral("Ви дійсно бажаєте вийти з програми?"),
                              QMessageBox::No | QMessageBox::Yes, QMessageBox::Yes);
    if (resBtn != QMessageBox::Yes) {
      event->ignore();
      return;
    }
  }

  if (settings.value(QStringLiteral("security/suggest_backup"), true).toBool()) {
    QMessageBox::StandardButton resBtn =
        QMessageBox::question(this, QStringLiteral("Резервна копія"),
                              QStringLiteral("Створити резервну копію бази даних перед виходом?"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (resBtn == QMessageBox::Yes) {
      backupDatabase();
    }
  }

  // Save open tabs
  QList<QVariant> openTabs;
  for (int i = 0; i < tabs_->count(); ++i) {
    QWidget * w = tabs_->widget(i);
    auto it = std::find(pages_.begin(), pages_.end(), w);
    if (it != pages_.end()) {
      openTabs.append(static_cast<int>(std::distance(pages_.begin(), it)));
    }
  }
  settings.setValue(QStringLiteral("ui/open_tabs"), openTabs);

  settings.setValue(QStringLiteral("geometry"), saveGeometry());
  QMainWindow::closeEvent(event);
}

void MainWindow::openSearch()
{
  SearchDialog dialog(session_, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  openHit(dialog.selected());
}

void MainWindow::openOpeningBalancesWizard()
{
  OpeningBalancesWizard wizard(session_, this);
  if (wizard.exec() == QDialog::Accepted) {
    ui::showToast(this, QStringLiteral("Залишки успішно збережено."));
    refreshStatus();
  }
}

void MainWindow::importTaxInvoice()
{
  const QString path =
      QFileDialog::getOpenFileName(this, QStringLiteral("Імпорт податкової накладної"), QString(),
                                   QStringLiteral("XML (*.xml);;Усі файли (*)"));
  if (path.isEmpty()) {
    return;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    ui::showError(this, Error::database(QStringLiteral("не вдалося прочитати файл XML")));
    return;
  }
  const QString xml = QString::fromUtf8(file.readAll());
  const Result<EdoImportResult> imported = importTaxInvoiceXml(session_.handle(), xml);
  if (!imported) {
    ui::showError(this, imported.error());
    return;
  }
  if (journal_ != nullptr) {
    selectNav(10);
    journal_->reload();
  }
  DocumentDialog::editExisting(session_, imported.value().documentId, this);
  if (journal_ != nullptr) {
    journal_->reload();
  }
}

void MainWindow::openHit(const SearchHit & hit)
{
  if (hit.objectId <= 0) {
    return;
  }
  switch (hit.kind) {
  case SearchKind::Document:
    DocumentDialog::editExisting(session_, DocumentId{hit.objectId}, this);
    if (journal_ != nullptr) {
      selectNav(10);
      journal_->reload();
    }
    return;
  case SearchKind::Counterparty:
    selectNav(1);
    counterparties_->reveal(CounterpartyId{hit.objectId});
    return;
  case SearchKind::Nomenclature:
    selectNav(2);
    nomenclature_->reveal(NomenclatureId{hit.objectId});
    return;
  case SearchKind::Account:
    if (hit.accountIsFolder) {
      selectNav(0);
      accounts_->reveal(AccountId{hit.objectId});
    } else {
      selectNav(12);
      card_->showAccount(AccountId{hit.objectId});
    }
    return;
  case SearchKind::FixedAsset:
    selectNav(5);
    assets_->reveal(FixedAssetId{hit.objectId});
    return;
  case SearchKind::Intangible:
    selectNav(6);
    intangibles_->reveal(IntangibleId{hit.objectId});
    return;
  case SearchKind::LowValue:
    selectNav(7);
    lowValue_->reveal(LowValueId{hit.objectId});
    return;
  case SearchKind::Employee:
    selectNav(8);
    employees_->reveal(EmployeeId{hit.objectId});
    return;
  }
}

void MainWindow::selectNav(int row)
{
  if (nav_ != nullptr) {
    QTreeWidgetItemIterator it(nav_);
    while (*it) {
      if ((*it)->data(0, Qt::UserRole).toInt() == row) {
        nav_->setCurrentItem(*it);
        emit nav_->itemClicked(*it, 0); // trigger the connection
        break;
      }
      ++it;
    }
  }
}

void MainWindow::openHelp()
{
  int row = 4;
  if (nav_ != nullptr) {
    if (auto * item = nav_->currentItem()) {
      if (item->childCount() == 0) {
        row = item->data(0, Qt::UserRole).toInt();
      }
    }
  }
  HelpBrowser::openTopic(this, HelpBrowser::topicForNavRow(row));
}

void MainWindow::openHelpFrom1c()
{
  HelpBrowser::openTopic(this, QStringLiteral("from-1c.html"));
}

void MainWindow::backupDatabase()
{
  const QString suggested =
      QStringLiteral("finik-%1.sqlite")
          .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
  const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Резервна копія"),
                                                    suggested, QStringLiteral("SQLite (*.sqlite)"));
  if (path.isEmpty()) {
    return;
  }
  if (QFile::exists(path) &&
      QMessageBox::question(this, QStringLiteral("Резервна копія"),
                            QStringLiteral("Файл існує. Замінити?")) != QMessageBox::Yes) {
    return;
  }
  if (QFile::exists(path) && !QFile::remove(path)) {
    ui::showError(this, QStringLiteral("не вдалося замінити існуючий файл"));
    return;
  }
  const Result<QString> saved = session_.backupTo(path);
  if (!saved) {
    ui::showError(this, saved.error());
    return;
  }
  QMessageBox::information(this, QStringLiteral("Резервна копія"),
                           QStringLiteral("Збережено: %1").arg(saved.value()));
}

void MainWindow::restoreDatabase()
{
  const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Відновити з копії"),
                                                    QString(), QStringLiteral("SQLite (*.sqlite)"));
  if (path.isEmpty()) {
    return;
  }
  if (QMessageBox::question(
          this, QStringLiteral("Відновлення"),
          QStringLiteral("Поточну базу буде збережено як копію безпеки, а після "
                         "перезапуску підставиться вибраний файл. Продовжити?")) !=
      QMessageBox::Yes) {
    return;
  }
  const Status status = session_.stageRestoreFrom(path);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  QMessageBox::information(
      this, QStringLiteral("Відновлення"),
      QStringLiteral("Копію підготовлено. Перезапустіть програму, щоб відкрити "
                     "відновлену базу. Попередній стан — файл "
                     "finik.sqlite.before-restore."));
  QApplication::quit();
}

void MainWindow::editCompany()
{
  CompanyDialog dialog(session_, this);
  if (dialog.exec() == QDialog::Accepted) {
    refreshStatus();
  }
}

void MainWindow::editPassword()
{
  PasswordDialog dialog(session_, this);
  dialog.exec();
}

void MainWindow::editUsers()
{
  UsersDialog dialog(session_, this);
  dialog.exec();
  refreshStatus();
}

void MainWindow::showAudit()
{
  AuditDialog dialog(session_, this);
  dialog.exec();
}

void MainWindow::showAbout()
{
  AboutDialog dialog(session_, this);
  dialog.exec();
}

void MainWindow::verifyDatabase()
{
  const Status status = IntegrityChecker(session_.handle()).verifyEngine();
  if (!status) {
    ui::showError(this, status);
    return;
  }
  QMessageBox::information(this, QStringLiteral("Перевірка бази"),
                           QStringLiteral("Перевірка цілісності пройшла успішно."));
}

void MainWindow::editPeriod()
{
  PeriodDialog dialog(session_, this);
  dialog.exec();
  refreshStatus();
}

void MainWindow::seedDemo()
{
  const Status access = Access::requireSeedDemo(session_.handle());
  if (!access) {
    ui::showError(this, access);
    return;
  }

  if (QMessageBox::question(
          this, QStringLiteral("Демо-дані"),
          QStringLiteral("Засіяти демо за поточний рік: контрагенти, товари, каса, банк, "
                         "послуги, рахунок, повернення, інвентаризація й основні "
                         "засоби.\n\n"
                         "Порожня база — повний набір. Якщо вже є демо «ТОВ «Демо-облік»» "
                         "без нових форм — досіються лише відсутні документи.")) !=
      QMessageBox::Yes) {
    return;
  }
  const Status status = seedDemoData(session_);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  QMessageBox::information(
      this, QStringLiteral("Демо-дані"),
      QStringLiteral("Засіяно. Перегляньте журнал, основні засоби, ОСВ, баланс і книгу "
                     "доходів."));
  int row = 4;
  if (nav_ != nullptr) {
    if (auto * item = nav_->currentItem()) {
      if (item->childCount() == 0) {
        row = item->data(0, Qt::UserRole).toInt();
      }
    }
  }
  selectNav(row == 4 ? 5 : 4);
  selectNav(row);
  refreshStatus();
}

void MainWindow::applyAccessGate()
{
  const UserRole role = Access::currentRoleOrViewer(session_.handle());
  if (usersAction_) {
    usersAction_->setEnabled(Access::canManageUsers(role));
    usersAction_->setToolTip(
        Access::canManageUsers(role)
            ? QString()
            : QStringLiteral("керувати користувачами може лише адміністратор"));
  }
  if (restoreAction_) {
    restoreAction_->setEnabled(Access::canRestore(role));
    restoreAction_->setToolTip(
        Access::canRestore(role)
            ? QString()
            : QStringLiteral("відновлення з копії доступне лише адміністратору"));
  }
  if (seedAction_) {
    seedAction_->setEnabled(Access::canSeedDemo(role));
    seedAction_->setToolTip(Access::canSeedDemo(role)
                                ? QString()
                                : QStringLiteral("демо-дані може засіяти лише адміністратор"));
  }
  if (backupAction_) {
    backupAction_->setEnabled(Access::canBackup(role));
    backupAction_->setToolTip(
        Access::canBackup(role)
            ? QString()
            : QStringLiteral("резервну копію може зробити бухгалтер або адміністратор"));
  }
}

void MainWindow::refreshStatus()
{
  const Result<CompanyProfile> company = session_.company().load();
  if (company) {
    setWindowTitle(QStringLiteral("Фінік — %1").arg(company.value().name));
  }
  QStringList parts;
  const Result<User> user = UserRepository(session_.handle()).currentUser();
  if (user) {
    QString who = user.value().displayName.trimmed();
    if (who.isEmpty()) {
      who = user.value().login;
    }
    parts.push_back(QStringLiteral("%1 (%2)").arg(who, userRoleLabelUk(user.value().role)));
  }
  const Result<QDate> closed = session_.period().closedThrough();
  if (!closed) {
    parts.push_back(closed.message());
    statusBar()->showMessage(parts.join(QStringLiteral("  ·  ")));
    return;
  }
  if (!closed.value().isValid()) {
    parts.push_back(QStringLiteral("Період не закрито"));
  } else {
    parts.push_back(
        QStringLiteral("Закрито по %1").arg(closed.value().toString(QStringLiteral("dd.MM.yyyy"))));
  }
  statusBar()->showMessage(parts.join(QStringLiteral("  ·  ")));
}

void MainWindow::syncDailyRates()
{
  const QDate today = QDate::currentDate();
  // Якщо на сьогодні курси вже завантажено, додатковий мережевий запит не
  // потрібен
  if (session_.currencies().hasRatesOnDate(today)) {
    return;
  }

  nbuClient_->syncRatesToRepository(session_.handle(), today, [this, today](Status status) {
    if (status) {
      if (statusBar()) {
        statusBar()->showMessage(QStringLiteral("Офіційні курси валют НБУ на %1 успішно оновлено.")
                                     .arg(today.toString(QStringLiteral("dd.MM.yyyy"))),
                                 4000);
      }
      if (currencies_) {
        currencies_->reload();
      }
    }
  });
}

void MainWindow::applySettings()
{
  QSettings settings;
  if (nav_) {
    nav_->setVisible(!settings.value(QStringLiteral("ui/hide_sidebar"), false).toBool());
  }
  if (autoLockFilter_) {
    autoLockFilter_->updateInterval();
  }
  if (dailyRateTimer_) {
    if (settings.value(QStringLiteral("automation/sync_nbu"), true).toBool()) {
      if (!dailyRateTimer_->isActive()) {
        dailyRateTimer_->start(3 * 60 * 60 * 1000);
      }
    } else {
      dailyRateTimer_->stop();
    }
  }
  ui::applyThemeAndStyle();
}

void MainWindow::openSettings()
{
  SettingsDialog dlg(session_, this);
  if (dlg.exec() == QDialog::Accepted) {
    applySettings();
  }
}

} // namespace fin
