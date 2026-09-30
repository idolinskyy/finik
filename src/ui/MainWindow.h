#pragma once

// Головне вікно: меню, навігація, вкладки, пошук, курси НБУ.

#include "fin/search/GlobalSearch.h"
#include "fin/session/LedgerSession.h"

#include <QMainWindow>
#include <vector>

class QAction;
class QTreeWidget;
class QTimer;

namespace fin {

class AutoLockFilter;

class NbuRateClient;
class AccountAnalysisPage;
class AccountCardPage;
class AccountPage;
class SubcontoTrialBalancePage;
class CounterpartyPage;
class ContractPage;
class CurrencyPage;
class DocumentJournalPage;
class EmployeePage;
class FixedAssetPage;
class IntangiblePage;
class LowValuePage;
class NomenclaturePage;
class BomPage;
class WarehousePage;
class TimesheetPage;
class DashboardPage;

class MainWindow : public QMainWindow
{
  Q_OBJECT

 public:
  explicit MainWindow(LedgerSession & session, QWidget * parent = nullptr);
  void selectNav(int row);
  void applySettings();
  void applyAccessGate();

 protected:
  void closeEvent(QCloseEvent * event) override;

 private:
  void backupDatabase();
  void restoreDatabase();
  void editCompany();
  void editPassword();
  void editUsers();
  void showAudit();
  void showAbout();
  void verifyDatabase();
  void editPeriod();
  void seedDemo();
  void openSettings();
  void openSearch();
  void importTaxInvoice();
  void openOpeningBalancesWizard();

  void handleSelectionChanged();
  void openHit(const SearchHit & hit);
  void refreshStatus();
  void openHelp();
  void openHelpFrom1c();
  void syncDailyRates();

  LedgerSession & session_;
  AutoLockFilter * autoLockFilter_ = nullptr;
  NbuRateClient * nbuClient_ = nullptr;
  QTimer * dailyRateTimer_ = nullptr;
  QTreeWidget * nav_ = nullptr;
  QTabWidget * tabs_ = nullptr;
  QAction * usersAction_ = nullptr;
  QAction * restoreAction_ = nullptr;
  QAction * seedAction_ = nullptr;
  QAction * backupAction_ = nullptr;
  std::vector<QWidget *> pages_;
  AccountPage * accounts_ = nullptr;
  CounterpartyPage * counterparties_ = nullptr;
  ContractPage * contracts_ = nullptr;
  NomenclaturePage * nomenclature_ = nullptr;
  BomPage * bom_ = nullptr;
  WarehousePage * warehouses_ = nullptr;
  FixedAssetPage * assets_ = nullptr;
  IntangiblePage * intangibles_ = nullptr;
  LowValuePage * lowValue_ = nullptr;
  EmployeePage * employees_ = nullptr;
  CurrencyPage * currencies_ = nullptr;
  DocumentJournalPage * journal_ = nullptr;
  DashboardPage * dashboard_ = nullptr;
  AccountAnalysisPage * analysis_ = nullptr;
  SubcontoTrialBalancePage * subcontoOsv_ = nullptr;
  AccountCardPage * card_ = nullptr;
  TimesheetPage * timesheet_ = nullptr;
  class ConsolidatedTaxPage * consolidatedTax_ = nullptr;
};

} // namespace fin
