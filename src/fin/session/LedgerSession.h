#pragma once

// Сеанс: з’єднання з БД, поточний користувач, доступ до репозиторіїв.

#include "fin/db/AccountRepository.h"
#include "fin/db/CompanyRepository.h"
#include "fin/db/CounterpartyRepository.h"
#include "fin/db/Database.h"
#include "fin/db/DocumentRepository.h"
#include "fin/db/EmployeeRepository.h"
#include "fin/db/HrOrderRepository.h"
#include "fin/db/TimesheetRepository.h"
#include "fin/db/FixedAssetRepository.h"
#include "fin/db/IntangibleAssetRepository.h"
#include "fin/db/LowValueRepository.h"
#include "fin/db/InventoryRepository.h"
#include "fin/db/LedgerRepository.h"
#include "fin/db/NomenclatureRepository.h"
#include "fin/db/PeriodRepository.h"
#include "fin/db/SubcontoRepository.h"
#include "fin/db/WarehouseRepository.h"
#include "fin/db/ContractRepository.h"
#include "fin/db/CurrencyRepository.h"
#include "fin/db/BomRepository.h"
#include "fin/posting/PostingEngine.h"
#include "fin/reports/Reports.h"

namespace fin {

class LedgerSession
{
 public:
  // Відкриває або створює файл БД SQLite; виконує міграції схеми.
  static Result<LedgerSession> openFile(const QString & path);
  // Відкриває тимчасову БД в пам'яті (для тестів і демо-режиму).
  static Result<LedgerSession> openMemory();

  // Довідники
  AccountRepository & accounts() { return accounts_; }
  const AccountRepository & accounts() const { return accounts_; }

  CounterpartyRepository & counterparties() { return counterparties_; }
  const CounterpartyRepository & counterparties() const { return counterparties_; }

  WarehouseRepository & warehouses() { return warehouses_; }
  const WarehouseRepository & warehouses() const { return warehouses_; }

  ContractRepository & contracts() { return contracts_; }
  const ContractRepository & contracts() const { return contracts_; }

  CurrencyRepository & currencies() { return currencies_; }
  const CurrencyRepository & currencies() const { return currencies_; }

  BomRepository & bom() { return bom_; }
  const BomRepository & bom() const { return bom_; }

  DocumentRepository & documents() { return documents_; }
  const DocumentRepository & documents() const { return documents_; }

  NomenclatureRepository & nomenclature() { return nomenclature_; }
  const NomenclatureRepository & nomenclature() const { return nomenclature_; }

  FixedAssetRepository & fixedAssets() { return fixedAssets_; }
  const FixedAssetRepository & fixedAssets() const { return fixedAssets_; }

  IntangibleAssetRepository & intangibles() { return intangibles_; }
  const IntangibleAssetRepository & intangibles() const { return intangibles_; }

  LowValueRepository & lowValue() { return lowValue_; }
  const LowValueRepository & lowValue() const { return lowValue_; }

  EmployeeRepository & employees() { return employees_; }
  const EmployeeRepository & employees() const { return employees_; }

  HrOrderRepository & hrOrders() { return hrOrders_; }
  const HrOrderRepository & hrOrders() const { return hrOrders_; }

  TimesheetRepository & timesheets() { return timesheets_; }
  const TimesheetRepository & timesheets() const { return timesheets_; }

  SubcontoRepository & subconto() { return subconto_; }
  const SubcontoRepository & subconto() const { return subconto_; }

  // Облікові регістри
  LedgerRepository & ledger() { return ledger_; }
  InventoryRepository & inventory() { return inventory_; }

  // Рушій проведення
  PostingEngine & posting() { return posting_; }

  // Звіти
  TrialBalanceReport & trialBalance() { return trialBalance_; }
  AccountCardReport & accountCard() { return accountCard_; }
  CounterpartyTurnoverReport & counterpartyTurnover() { return counterpartyTurnover_; }
  SubcontoTrialBalanceReport & subcontoTrialBalance() { return subcontoTrialBalance_; }
  AccountAnalysisReport & accountAnalysis() { return accountAnalysis_; }
  StockReport & stock() { return stock_; }
  CashBookReport & cashBook() { return cashBook_; }

  // Управління обліковими місяцями
  PeriodRepository & period() { return period_; }
  const PeriodRepository & period() const { return period_; }

  // Реквізити компанії
  CompanyRepository & company() { return company_; }
  const CompanyRepository & company() const { return company_; }

  // Перевіряє цілісність БД через IntegrityChecker (глобальний баланс Гол. книги).
  Status verify() const;

  // Повертає дескриптор Qt SQL з'єднання (для передачі в конструктори
  // підсистем, що не зберігаються в LedgerSession — наприклад, TaxInvoiceXml).
  QSqlDatabase handle() const { return db_.handle(); }
  QString databaseName() const { return db_.databaseName(); }

  // Гаряче резервне копіювання: SQLite VACUUM INTO або WAL backup API.
  Result<QString> backupTo(const QString & destinationPath) const;
  // Підготовка до відновлення: валідує файл бекапу й переміщує на місце поточного.
  Status stageRestoreFrom(const QString & backupPath) const;

 private:
  explicit LedgerSession(Database db);

  // Порядок полів важливий: db_ ініціалізується першим,
  // після чого всі репозиторії та звіти отримують db_.handle() у конструкторі.
  Database db_;
  AccountRepository accounts_;
  CounterpartyRepository counterparties_;
  WarehouseRepository warehouses_;
  ContractRepository contracts_;
  CurrencyRepository currencies_;
  BomRepository bom_;
  DocumentRepository documents_;
  NomenclatureRepository nomenclature_;
  FixedAssetRepository fixedAssets_;
  IntangibleAssetRepository intangibles_;
  LowValueRepository lowValue_;
  EmployeeRepository employees_;
  HrOrderRepository hrOrders_;
  TimesheetRepository timesheets_;
  SubcontoRepository subconto_;
  LedgerRepository ledger_;
  InventoryRepository inventory_;
  PeriodRepository period_;
  CompanyRepository company_;
  PostingEngine posting_;
  TrialBalanceReport trialBalance_;
  AccountCardReport accountCard_;
  CounterpartyTurnoverReport counterpartyTurnover_;
  SubcontoTrialBalanceReport subcontoTrialBalance_;
  AccountAnalysisReport accountAnalysis_;
  StockReport stock_;
  CashBookReport cashBook_;
};

} // namespace fin
