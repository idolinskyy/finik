#include "fin/db/Database.h"

#include "fin/core/Sql.h"
#include "fin/core/Transaction.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QSqlError>
#include <QStringList>
#include <QUuid>

namespace fin {
namespace {

const QStringList kMigrationV1 = {
    QStringLiteral("CREATE TABLE accounts ("
                   "  id INTEGER PRIMARY KEY,"
                   "  code TEXT NOT NULL UNIQUE COLLATE NOCASE,"
                   "  name TEXT NOT NULL,"
                   "  parent_id INTEGER REFERENCES accounts(id) ON DELETE RESTRICT,"
                   "  account_class INTEGER NOT NULL,"
                   "  normal_side TEXT NOT NULL CHECK(normal_side IN ('debit','credit')),"
                   "  has_counterparty INTEGER NOT NULL DEFAULT 0 CHECK(has_counterparty IN (0,1)),"
                   "  is_folder INTEGER NOT NULL DEFAULT 0 CHECK(is_folder IN (0,1)),"
                   "  is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0,1))"
                   ")"),
    QStringLiteral("CREATE TABLE counterparties ("
                   "  id INTEGER PRIMARY KEY,"
                   "  name TEXT NOT NULL,"
                   "  edrpou TEXT,"
                   "  is_customer INTEGER NOT NULL DEFAULT 1 CHECK(is_customer IN (0,1)),"
                   "  is_supplier INTEGER NOT NULL DEFAULT 1 CHECK(is_supplier IN (0,1)),"
                   "  notes TEXT,"
                   "  CHECK(length(trim(name)) > 0)"
                   ")"),
    QStringLiteral("CREATE TABLE documents ("
                   "  id INTEGER PRIMARY KEY,"
                   "  type TEXT NOT NULL,"
                   "  number TEXT NOT NULL,"
                   "  date TEXT NOT NULL,"
                   "  counterparty_id INTEGER REFERENCES counterparties(id) ON DELETE RESTRICT,"
                   "  comment TEXT,"
                   "  posted INTEGER NOT NULL DEFAULT 0 CHECK(posted IN (0,1)),"
                   "  posted_at TEXT,"
                   "  UNIQUE(type, number)"
                   ")"),
    QStringLiteral(
        "CREATE TABLE document_lines ("
        "  id INTEGER PRIMARY KEY,"
        "  document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
        "  line_no INTEGER NOT NULL,"
        "  debit_account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE RESTRICT,"
        "  credit_account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE RESTRICT,"
        "  amount_cents INTEGER NOT NULL CHECK(amount_cents > 0),"
        "  description TEXT,"
        "  UNIQUE(document_id, line_no)"
        ")"),
    QStringLiteral(
        "CREATE TABLE ledger_entries ("
        "  id INTEGER PRIMARY KEY,"
        "  document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE RESTRICT,"
        "  date TEXT NOT NULL,"
        "  debit_account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE RESTRICT,"
        "  credit_account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE RESTRICT,"
        "  amount_cents INTEGER NOT NULL CHECK(amount_cents > 0),"
        "  counterparty_id INTEGER REFERENCES counterparties(id) ON DELETE RESTRICT,"
        "  description TEXT"
        ")"),
    QStringLiteral("CREATE TABLE document_sequences ("
                   "  type TEXT PRIMARY KEY,"
                   "  last_number INTEGER NOT NULL"
                   ")"),
    QStringLiteral("CREATE INDEX idx_ledger_date ON ledger_entries(date)"),
    QStringLiteral("CREATE INDEX idx_ledger_debit ON ledger_entries(debit_account_id)"),
    QStringLiteral("CREATE INDEX idx_ledger_credit ON ledger_entries(credit_account_id)"),
    QStringLiteral("CREATE INDEX idx_ledger_document ON ledger_entries(document_id)"),
    QStringLiteral("CREATE INDEX idx_documents_date ON documents(date)")};

const QStringList kMigrationV2 = {
    QStringLiteral("CREATE TABLE nomenclature ("
                   "  id INTEGER PRIMARY KEY,"
                   "  name TEXT NOT NULL,"
                   "  sku TEXT UNIQUE,"
                   "  unit TEXT NOT NULL DEFAULT 'шт',"
                   "  is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0,1)),"
                   "  CHECK(length(trim(name)) > 0)"
                   ")"),
    QStringLiteral("ALTER TABLE document_lines ADD COLUMN nomenclature_id INTEGER "
                   "REFERENCES nomenclature(id) ON DELETE RESTRICT"),
    QStringLiteral("ALTER TABLE document_lines ADD COLUMN quantity_milli INTEGER NOT NULL "
                   "DEFAULT 0 CHECK(quantity_milli >= 0)"),
    QStringLiteral("ALTER TABLE document_lines ADD COLUMN price_cents INTEGER NOT NULL "
                   "DEFAULT 0 CHECK(price_cents >= 0)"),
    QStringLiteral(
        "CREATE TABLE inventory_movements ("
        "  id INTEGER PRIMARY KEY,"
        "  document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE RESTRICT,"
        "  date TEXT NOT NULL,"
        "  nomenclature_id INTEGER NOT NULL REFERENCES nomenclature(id) ON DELETE RESTRICT,"
        "  quantity_milli INTEGER NOT NULL,"
        "  amount_cents INTEGER NOT NULL,"
        "  description TEXT"
        ")"),
    QStringLiteral("CREATE INDEX idx_inventory_date ON inventory_movements(date)"),
    QStringLiteral(
        "CREATE INDEX idx_inventory_nomenclature ON inventory_movements(nomenclature_id)"),
    QStringLiteral("CREATE INDEX idx_inventory_document ON inventory_movements(document_id)")};

const QStringList kMigrationV3 = {
    QStringLiteral("CREATE TABLE accounting_period ("
                   "  id INTEGER PRIMARY KEY CHECK(id = 1),"
                   "  closed_through TEXT,"
                   "  first_closed_from TEXT,"
                   "  closed_at TEXT"
                   ")"),
    QStringLiteral(
        "INSERT INTO accounting_period(id, closed_through, first_closed_from, closed_at) "
        "VALUES(1, NULL, NULL, NULL)")};

const QStringList kMigrationV4 = {
    QStringLiteral("CREATE TABLE company_profile ("
                   "  id INTEGER PRIMARY KEY CHECK(id = 1),"
                   "  name TEXT NOT NULL,"
                   "  edrpou TEXT,"
                   "  address TEXT,"
                   "  accountant TEXT,"
                   "  cashier TEXT,"
                   "  CHECK(length(trim(name)) > 0)"
                   ")"),
    QStringLiteral("INSERT INTO company_profile(id, name, edrpou, address, accountant, cashier) "
                   "VALUES(1, 'Підприємство', '', '', '', '')")};

const QStringList kMigrationV5 = {
    QStringLiteral("ALTER TABLE company_profile ADD COLUMN iban TEXT"),
    QStringLiteral("ALTER TABLE company_profile ADD COLUMN mfo TEXT"),
    QStringLiteral("ALTER TABLE company_profile ADD COLUMN bank_name TEXT"),
    QStringLiteral("ALTER TABLE counterparties ADD COLUMN iban TEXT"),
    QStringLiteral("ALTER TABLE counterparties ADD COLUMN bank_name TEXT"),
    QStringLiteral("ALTER TABLE documents ADD COLUMN received_by TEXT"),
    QStringLiteral("ALTER TABLE documents ADD COLUMN identity_doc TEXT")};

const QStringList kMigrationV6 = {
    QStringLiteral("ALTER TABLE company_profile ADD COLUMN tax_system TEXT NOT NULL "
                   "DEFAULT 'simplified'"),
    QStringLiteral("ALTER TABLE company_profile ADD COLUMN single_tax_rate_bp INTEGER NOT NULL "
                   "DEFAULT 500"),
    QStringLiteral("CREATE TABLE users ("
                   "  id INTEGER PRIMARY KEY,"
                   "  login TEXT NOT NULL UNIQUE COLLATE NOCASE,"
                   "  display_name TEXT NOT NULL,"
                   "  password_hash BLOB,"
                   "  password_salt BLOB,"
                   "  iterations INTEGER NOT NULL DEFAULT 100000,"
                   "  is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0,1)),"
                   "  CHECK(length(trim(login)) > 0),"
                   "  CHECK(length(trim(display_name)) > 0)"
                   ")"),
    QStringLiteral("INSERT INTO users(id, login, display_name, password_hash, password_salt, "
                   "iterations, is_active) "
                   "VALUES(1, 'admin', 'Адміністратор', NULL, NULL, 100000, 1)"),
    QStringLiteral("CREATE TABLE app_context ("
                   "  id INTEGER PRIMARY KEY CHECK(id = 1),"
                   "  user_id INTEGER REFERENCES users(id)"
                   ")"),
    QStringLiteral("INSERT INTO app_context(id, user_id) VALUES(1, 1)"),
    QStringLiteral("CREATE TABLE audit_log ("
                   "  id INTEGER PRIMARY KEY,"
                   "  at TEXT NOT NULL,"
                   "  user_id INTEGER REFERENCES users(id),"
                   "  action TEXT NOT NULL,"
                   "  entity TEXT NOT NULL,"
                   "  entity_id INTEGER,"
                   "  details TEXT"
                   ")"),
    QStringLiteral("CREATE INDEX idx_audit_at ON audit_log(at)")};

const QStringList kMigrationV7 = {
    QStringLiteral("ALTER TABLE documents ADD COLUMN based_on_id INTEGER "
                   "REFERENCES documents(id) ON DELETE SET NULL")};

const QStringList kMigrationV8 = {
    QStringLiteral("ALTER TABLE users ADD COLUMN role TEXT NOT NULL DEFAULT 'admin'")};

const QStringList kMigrationV9 = {
    QStringLiteral(
        "CREATE TABLE fixed_assets ("
        "  id INTEGER PRIMARY KEY,"
        "  inventory_no TEXT NOT NULL UNIQUE COLLATE NOCASE,"
        "  name TEXT NOT NULL,"
        "  asset_account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE RESTRICT,"
        "  expense_account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE RESTRICT,"
        "  commissioned_on TEXT,"
        "  original_cents INTEGER NOT NULL CHECK(original_cents > 0),"
        "  salvage_cents INTEGER NOT NULL DEFAULT 0 CHECK(salvage_cents >= 0),"
        "  opening_accumulated_cents INTEGER NOT NULL DEFAULT 0 "
        "    CHECK(opening_accumulated_cents >= 0),"
        "  accumulated_cents INTEGER NOT NULL DEFAULT 0 CHECK(accumulated_cents >= 0),"
        "  useful_life_months INTEGER NOT NULL CHECK(useful_life_months > 0),"
        "  status TEXT NOT NULL DEFAULT 'draft' CHECK(status IN ('draft','in_use')),"
        "  is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0,1)),"
        "  CHECK(length(trim(inventory_no)) > 0),"
        "  CHECK(length(trim(name)) > 0),"
        "  CHECK(salvage_cents < original_cents),"
        "  CHECK(opening_accumulated_cents <= original_cents - salvage_cents)"
        ")"),
    QStringLiteral("ALTER TABLE document_lines ADD COLUMN fixed_asset_id INTEGER "
                   "REFERENCES fixed_assets(id) ON DELETE RESTRICT")};

const QStringList kMigrationV10 = {
    QStringLiteral("CREATE TABLE low_value_items ("
                   "  id INTEGER PRIMARY KEY,"
                   "  inventory_no TEXT NOT NULL UNIQUE COLLATE NOCASE,"
                   "  name TEXT NOT NULL,"
                   "  unit TEXT NOT NULL DEFAULT 'шт',"
                   "  quantity_milli INTEGER NOT NULL DEFAULT 0,"
                   "  amount_cents INTEGER NOT NULL DEFAULT 0,"
                   "  is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0,1)),"
                   "  CHECK(length(trim(inventory_no)) > 0),"
                   "  CHECK(length(trim(name)) > 0),"
                   "  CHECK(length(trim(unit)) > 0),"
                   "  CHECK(quantity_milli >= 0),"
                   "  CHECK(amount_cents >= 0)"
                   ")"),
    QStringLiteral("ALTER TABLE document_lines ADD COLUMN low_value_id INTEGER "
                   "REFERENCES low_value_items(id) ON DELETE RESTRICT")};

const QStringList kMigrationV12 = {
    QStringLiteral(
        "CREATE TABLE intangible_assets ("
        "  id INTEGER PRIMARY KEY,"
        "  inventory_no TEXT NOT NULL UNIQUE COLLATE NOCASE,"
        "  name TEXT NOT NULL,"
        "  asset_account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE RESTRICT,"
        "  expense_account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE RESTRICT,"
        "  commissioned_on TEXT,"
        "  disposed_on TEXT,"
        "  original_cents INTEGER NOT NULL CHECK(original_cents > 0),"
        "  salvage_cents INTEGER NOT NULL DEFAULT 0 CHECK(salvage_cents >= 0),"
        "  opening_accumulated_cents INTEGER NOT NULL DEFAULT 0 "
        "    CHECK(opening_accumulated_cents >= 0),"
        "  accumulated_cents INTEGER NOT NULL DEFAULT 0 CHECK(accumulated_cents >= 0),"
        "  useful_life_months INTEGER NOT NULL CHECK(useful_life_months > 0),"
        "  status TEXT NOT NULL DEFAULT 'draft' "
        "    CHECK(status IN ('draft','in_use','disposed')),"
        "  is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0,1)),"
        "  CHECK(length(trim(inventory_no)) > 0),"
        "  CHECK(length(trim(name)) > 0),"
        "  CHECK(salvage_cents < original_cents),"
        "  CHECK(opening_accumulated_cents <= original_cents - salvage_cents),"
        "  CHECK(disposed_on IS NULL OR status = 'disposed'),"
        "  CHECK(status <> 'disposed' OR (commissioned_on IS NOT NULL AND "
        "        disposed_on IS NOT NULL))"
        ")"),
    QStringLiteral("ALTER TABLE document_lines ADD COLUMN intangible_id INTEGER "
                   "REFERENCES intangible_assets(id) ON DELETE RESTRICT")};

const QStringList kMigrationV13 = {
    QStringLiteral("ALTER TABLE company_profile ADD COLUMN vat_payer INTEGER NOT NULL "
                   "DEFAULT 0"),
    QStringLiteral("ALTER TABLE company_profile ADD COLUMN vat_rate_bp INTEGER NOT NULL "
                   "DEFAULT 2000"),
    QStringLiteral("ALTER TABLE documents ADD COLUMN vat_registration_date TEXT"),
    QStringLiteral("ALTER TABLE documents ADD COLUMN vat_offset_cents INTEGER NOT NULL "
                   "DEFAULT 0"),
    QStringLiteral("ALTER TABLE document_lines ADD COLUMN vat_rate_bp INTEGER NOT NULL "
                   "DEFAULT 0"),
    QStringLiteral("ALTER TABLE document_lines ADD COLUMN vat_cents INTEGER NOT NULL "
                   "DEFAULT 0"),
    QStringLiteral("ALTER TABLE document_lines ADD COLUMN posting_date TEXT")};

const QStringList kMigrationV14 = {
    QStringLiteral("ALTER TABLE documents ADD COLUMN edo_xml TEXT"),
    QStringLiteral("ALTER TABLE documents ADD COLUMN edo_sha256 TEXT"),
    QStringLiteral("ALTER TABLE documents ADD COLUMN edo_imported INTEGER NOT NULL "
                   "DEFAULT 0 CHECK(edo_imported IN (0,1))")};

const QStringList kMigrationV15 = {
    QStringLiteral("CREATE TABLE employees ("
                   "  id INTEGER PRIMARY KEY,"
                   "  tax_id TEXT NOT NULL UNIQUE COLLATE NOCASE,"
                   "  name TEXT NOT NULL,"
                   "  hired_on TEXT NOT NULL,"
                   "  salary_cents INTEGER NOT NULL CHECK(salary_cents > 0),"
                   "  disabled_ssc INTEGER NOT NULL DEFAULT 0 CHECK(disabled_ssc IN (0,1)),"
                   "  is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0,1)),"
                   "  CHECK(length(trim(tax_id)) = 10),"
                   "  CHECK(length(trim(name)) > 0)"
                   ")"),
    QStringLiteral("ALTER TABLE document_lines ADD COLUMN employee_id INTEGER "
                   "REFERENCES employees(id) ON DELETE RESTRICT")};

const QStringList kMigrationV16 = {
    QStringLiteral("CREATE TABLE subconto_kinds ("
                   "  id TEXT PRIMARY KEY,"
                   "  name_ua TEXT NOT NULL,"
                   "  source_table TEXT NOT NULL,"
                   "  id_column TEXT NOT NULL,"
                   "  name_column TEXT NOT NULL,"
                   "  CHECK(length(trim(id)) > 0),"
                   "  CHECK(length(trim(name_ua)) > 0)"
                   ")"),
    QStringLiteral(
        "CREATE TABLE account_subconto ("
        "  account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
        "  line_no INTEGER NOT NULL CHECK(line_no BETWEEN 1 AND 3),"
        "  subconto_kind_id TEXT NOT NULL REFERENCES subconto_kinds(id) ON DELETE RESTRICT,"
        "  is_required INTEGER NOT NULL DEFAULT 1 CHECK(is_required IN (0,1)),"
        "  PRIMARY KEY (account_id, line_no)"
        ")"),
    QStringLiteral("ALTER TABLE ledger_entries ADD COLUMN dt_subconto1_kind TEXT"),
    QStringLiteral("ALTER TABLE ledger_entries ADD COLUMN dt_subconto1_id INTEGER"),
    QStringLiteral("ALTER TABLE ledger_entries ADD COLUMN dt_subconto2_kind TEXT"),
    QStringLiteral("ALTER TABLE ledger_entries ADD COLUMN dt_subconto2_id INTEGER"),
    QStringLiteral("ALTER TABLE ledger_entries ADD COLUMN dt_subconto3_kind TEXT"),
    QStringLiteral("ALTER TABLE ledger_entries ADD COLUMN dt_subconto3_id INTEGER"),
    QStringLiteral("ALTER TABLE ledger_entries ADD COLUMN ct_subconto1_kind TEXT"),
    QStringLiteral("ALTER TABLE ledger_entries ADD COLUMN ct_subconto1_id INTEGER"),
    QStringLiteral("ALTER TABLE ledger_entries ADD COLUMN ct_subconto2_kind TEXT"),
    QStringLiteral("ALTER TABLE ledger_entries ADD COLUMN ct_subconto2_id INTEGER"),
    QStringLiteral("ALTER TABLE ledger_entries ADD COLUMN ct_subconto3_kind TEXT"),
    QStringLiteral("ALTER TABLE ledger_entries ADD COLUMN ct_subconto3_id INTEGER"),
    QStringLiteral("CREATE INDEX idx_ledger_dt_subconto1 ON ledger_entries(debit_account_id, "
                   "dt_subconto1_kind, dt_subconto1_id)"),
    QStringLiteral("CREATE INDEX idx_ledger_ct_subconto1 ON ledger_entries(credit_account_id, "
                   "ct_subconto1_kind, ct_subconto1_id)"),
    QStringLiteral(
        "INSERT INTO subconto_kinds(id, name_ua, source_table, id_column, name_column) VALUES "
        "('counterparty', 'Контрагенти', 'counterparties', 'id', 'name'),"
        "('nomenclature', 'Номенклатура', 'nomenclature', 'id', 'name'),"
        "('fixed_asset', 'Основні засоби', 'fixed_assets', 'id', 'name'),"
        "('intangible_asset', 'Нематеріальні активи', 'intangible_assets', 'id', 'name'),"
        "('low_value', 'МШП', 'low_value_items', 'id', 'name'),"
        "('employee', 'Працівники', 'employees', 'id', 'name')"),
    QStringLiteral(
        "INSERT OR IGNORE INTO account_subconto(account_id, line_no, subconto_kind_id, "
        "is_required) "
        "SELECT id, 1, 'counterparty', 1 FROM accounts WHERE code IN ('361', '631', '371', '681')"),
    QStringLiteral("INSERT OR IGNORE INTO account_subconto(account_id, line_no, subconto_kind_id, "
                   "is_required) "
                   "SELECT id, 1, 'nomenclature', 1 FROM accounts WHERE code IN ('281')"),
    QStringLiteral("INSERT OR IGNORE INTO account_subconto(account_id, line_no, subconto_kind_id, "
                   "is_required) "
                   "SELECT id, 1, 'fixed_asset', 1 FROM accounts WHERE code IN ('104', '105', "
                   "'109', '131', '152')"),
    QStringLiteral("INSERT OR IGNORE INTO account_subconto(account_id, line_no, subconto_kind_id, "
                   "is_required) "
                   "SELECT id, 1, 'intangible_asset', 1 FROM accounts WHERE code IN ('125', '127', "
                   "'133', '154')"),
    QStringLiteral("INSERT OR IGNORE INTO account_subconto(account_id, line_no, subconto_kind_id, "
                   "is_required) "
                   "SELECT id, 1, 'low_value', 1 FROM accounts WHERE code IN ('22')"),
    QStringLiteral("INSERT OR IGNORE INTO account_subconto(account_id, line_no, subconto_kind_id, "
                   "is_required) "
                   "SELECT id, 1, 'employee', 1 FROM accounts WHERE code IN ('661')"),
    QStringLiteral("UPDATE ledger_entries SET "
                   "  dt_subconto1_kind = 'counterparty', "
                   "  dt_subconto1_id = counterparty_id "
                   "WHERE counterparty_id IS NOT NULL AND debit_account_id IN (SELECT id FROM "
                   "accounts WHERE code IN ('361', '631', '371', '681'))"),
    QStringLiteral("UPDATE ledger_entries SET "
                   "  ct_subconto1_kind = 'counterparty', "
                   "  ct_subconto1_id = counterparty_id "
                   "WHERE counterparty_id IS NOT NULL AND credit_account_id IN (SELECT id FROM "
                   "accounts WHERE code IN ('361', '631', '371', '681'))")};

const QStringList kMigrationV17 = {
    QStringLiteral("CREATE TABLE warehouses ("
                   "  id INTEGER PRIMARY KEY,"
                   "  code TEXT NOT NULL UNIQUE COLLATE NOCASE,"
                   "  name TEXT NOT NULL,"
                   "  address TEXT,"
                   "  is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0,1)),"
                   "  CHECK(length(trim(code)) > 0),"
                   "  CHECK(length(trim(name)) > 0)"
                   ")"),
    QStringLiteral("INSERT INTO warehouses(id, code, name, address, is_active) "
                   "VALUES(1, 'MAIN', 'Основний склад', 'м. Київ, вул. Облікова, 1', 1)"),
    QStringLiteral(
        "CREATE TABLE contracts ("
        "  id INTEGER PRIMARY KEY,"
        "  counterparty_id INTEGER NOT NULL REFERENCES counterparties(id) ON DELETE RESTRICT,"
        "  number TEXT NOT NULL,"
        "  name TEXT NOT NULL,"
        "  date TEXT NOT NULL,"
        "  contract_type TEXT NOT NULL DEFAULT 'other' "
        "    CHECK(contract_type IN ('with_customer', 'with_supplier', 'other')),"
        "  currency TEXT NOT NULL DEFAULT 'UAH',"
        "  is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0,1)),"
        "  CHECK(length(trim(number)) > 0)"
        ")"),
    QStringLiteral("CREATE INDEX idx_contracts_counterparty ON contracts(counterparty_id)"),
    QStringLiteral("ALTER TABLE documents ADD COLUMN warehouse_id INTEGER "
                   "REFERENCES warehouses(id) ON DELETE RESTRICT"),
    QStringLiteral("ALTER TABLE documents ADD COLUMN target_warehouse_id INTEGER "
                   "REFERENCES warehouses(id) ON DELETE RESTRICT"),
    QStringLiteral("ALTER TABLE documents ADD COLUMN contract_id INTEGER "
                   "REFERENCES contracts(id) ON DELETE RESTRICT"),
    QStringLiteral("ALTER TABLE documents ADD COLUMN order_status TEXT"),
    QStringLiteral("ALTER TABLE document_lines ADD COLUMN warehouse_id INTEGER "
                   "REFERENCES warehouses(id) ON DELETE RESTRICT"),
    QStringLiteral("ALTER TABLE inventory_movements ADD COLUMN warehouse_id INTEGER "
                   "REFERENCES warehouses(id) ON DELETE RESTRICT"),
    QStringLiteral("UPDATE inventory_movements SET warehouse_id = 1 WHERE warehouse_id IS NULL"),
    QStringLiteral("UPDATE documents SET warehouse_id = 1 WHERE warehouse_id IS NULL"),
    QStringLiteral("CREATE INDEX idx_inventory_warehouse ON inventory_movements(warehouse_id)"),
    QStringLiteral(
        "INSERT INTO subconto_kinds(id, name_ua, source_table, id_column, name_column) VALUES "
        "('contract', 'Договори контрагентів', 'contracts', 'id', 'name')"),
    QStringLiteral(
        "INSERT OR IGNORE INTO account_subconto(account_id, line_no, subconto_kind_id, "
        "is_required) "
        "SELECT id, 2, 'contract', 0 FROM accounts WHERE code IN ('361', '631', '371', '681')")};

const QStringList kMigrationV18 = {
    QStringLiteral("CREATE TABLE currencies ("
                   "  code TEXT PRIMARY KEY,"
                   "  numeric_code INTEGER NOT NULL,"
                   "  name_ua TEXT NOT NULL,"
                   "  symbol TEXT NOT NULL,"
                   "  is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0,1)),"
                   "  CHECK(length(trim(code)) = 3),"
                   "  CHECK(length(trim(name_ua)) > 0)"
                   ")"),
    QStringLiteral(
        "INSERT OR IGNORE INTO currencies(code, numeric_code, name_ua, symbol, is_active) VALUES "
        "('UAH', 980, 'Гривня', '₴', 1),"
        "('USD', 840, 'Долар США', '$', 1),"
        "('EUR', 978, 'Євро', '€', 1),"
        "('PLN', 985, 'Польський злотий', 'zł', 1)"),
    QStringLiteral("CREATE TABLE currency_rates ("
                   "  currency_code TEXT NOT NULL REFERENCES currencies(code) ON DELETE CASCADE,"
                   "  rate_date TEXT NOT NULL,"
                   "  nominal INTEGER NOT NULL DEFAULT 1 CHECK(nominal > 0),"
                   "  rate_micros INTEGER NOT NULL CHECK(rate_micros > 0),"
                   "  PRIMARY KEY(currency_code, rate_date)"
                   ")"),
    QStringLiteral(
        "CREATE INDEX idx_rates_code_date ON currency_rates(currency_code, rate_date DESC)"),
    QStringLiteral(
        "ALTER TABLE documents ADD COLUMN currency_code TEXT REFERENCES currencies(code)"),
    QStringLiteral("ALTER TABLE documents ADD COLUMN currency_rate_micros INTEGER"),
    QStringLiteral("ALTER TABLE documents ADD COLUMN currency_nominal INTEGER DEFAULT 1"),
    QStringLiteral(
        "ALTER TABLE ledger_entries ADD COLUMN currency_code TEXT REFERENCES currencies(code)"),
    QStringLiteral("ALTER TABLE ledger_entries ADD COLUMN currency_amount_cents INTEGER"),
    QStringLiteral("ALTER TABLE ledger_entries ADD COLUMN currency_rate_micros INTEGER"),
    QStringLiteral(
        "INSERT OR IGNORE INTO accounts (code, name, parent_id, account_class, normal_side, "
        "has_counterparty, is_folder, is_active) VALUES "
        "('312', 'Поточні рахунки в іноземній валюті', (SELECT id FROM accounts WHERE code = "
        "'31'), 3, 'debit', 0, 0, 1),"
        "('302', 'Готівка в іноземній валюті', (SELECT id FROM accounts WHERE code = '30'), 3, "
        "'debit', 0, 0, 1),"
        "('334', 'Грошові кошти в дорозі в іноземній валюті', NULL, 3, 'debit', 0, 0, 1),"
        "('362', 'Розрахунки з іноземними покупцями', (SELECT id FROM accounts WHERE code = '36'), "
        "3, 'debit', 1, 0, 1),"
        "('3712', 'Аванси видані в іноземній валюті', (SELECT id FROM accounts WHERE code = '37'), "
        "3, 'debit', 1, 0, 1),"
        "('632', 'Розрахунки з іноземними постачальниками', (SELECT id FROM accounts WHERE code = "
        "'63'), 4, 'credit', 1, 0, 1),"
        "('6812', 'Аванси одержані в іноземній валюті', (SELECT id FROM accounts WHERE code = "
        "'68'), 6, 'credit', 1, 0, 1),"
        "('714', 'Дохід від операційної курсової різниці', NULL, 7, 'credit', 0, 0, 1),"
        "('944', 'Втрати від операційної курсової різниці', NULL, 9, 'debit', 0, 0, 1)"),
    QStringLiteral("INSERT OR IGNORE INTO account_subconto(account_id, line_no, subconto_kind_id, "
                   "is_required) "
                   "SELECT id, 1, 'counterparty', 1 FROM accounts WHERE code IN ('362', '632', "
                   "'3712', '6812')"),
    QStringLiteral(
        "INSERT OR IGNORE INTO account_subconto(account_id, line_no, subconto_kind_id, "
        "is_required) "
        "SELECT id, 2, 'contract', 0 FROM accounts WHERE code IN ('362', '632', '3712', '6812')")};

const QStringList kMigrationV19 = {
    // 1. Специфікації виробів (Технологічні карти / BOM)
    QStringLiteral(
        "CREATE TABLE bill_of_materials ("
        "  id INTEGER PRIMARY KEY,"
        "  product_id INTEGER NOT NULL REFERENCES nomenclature(id) ON DELETE RESTRICT,"
        "  name TEXT NOT NULL,"
        "  output_quantity_milli INTEGER NOT NULL DEFAULT 1000 CHECK(output_quantity_milli > 0),"
        "  is_default INTEGER NOT NULL DEFAULT 1 CHECK(is_default IN (0,1)),"
        "  is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0,1)),"
        "  comment TEXT,"
        "  CHECK(length(trim(name)) > 0)"
        ")"),
    QStringLiteral("CREATE INDEX idx_bom_product ON bill_of_materials(product_id)"),
    QStringLiteral("CREATE TABLE bom_lines ("
                   "  id INTEGER PRIMARY KEY,"
                   "  bom_id INTEGER NOT NULL REFERENCES bill_of_materials(id) ON DELETE CASCADE,"
                   "  line_no INTEGER NOT NULL,"
                   "  component_id INTEGER NOT NULL REFERENCES nomenclature(id) ON DELETE RESTRICT,"
                   "  quantity_milli INTEGER NOT NULL CHECK(quantity_milli > 0),"
                   "  comment TEXT"
                   ")"),
    QStringLiteral("CREATE INDEX idx_bom_lines_bom ON bom_lines(bom_id)"),
    // 2. Додавання bom_id до документів
    QStringLiteral("ALTER TABLE documents ADD COLUMN bom_id INTEGER REFERENCES "
                   "bill_of_materials(id) ON DELETE RESTRICT"),
    // 3. Додавання виробничих субрахунків у План рахунків
    QStringLiteral(
        "INSERT OR IGNORE INTO accounts (code, name, parent_id, account_class, normal_side, "
        "has_counterparty, is_folder, is_active) VALUES "
        "('20', 'Виробничі запаси', NULL, 2, 'debit', 0, 1, 1),"
        "('201', 'Сировина й матеріали', (SELECT id FROM accounts WHERE code = '20'), 2, 'debit', "
        "0, 0, 1),"
        "('202', 'Купівельні напівфабрикати та комплектуючі', (SELECT id FROM accounts WHERE code "
        "= '20'), 2, 'debit', 0, 0, 1),"
        "('203', 'Паливо', (SELECT id FROM accounts WHERE code = '20'), 2, 'debit', 0, 0, 1),"
        "('23', 'Виробництво', NULL, 2, 'debit', 0, 1, 1),"
        "('231', 'Основне виробництво', (SELECT id FROM accounts WHERE code = '23'), 2, 'debit', "
        "0, 0, 1),"
        "('26', 'Готова продукція', NULL, 2, 'debit', 0, 0, 1),"
        "('91', 'Загальновиробничі витрати', NULL, 9, 'debit', 0, 0, 1),"
        "('901', 'Собівартість реалізованої готової продукції', NULL, 9, 'debit', 0, 0, 1)"),
    // 4. Субконто для виробничих рахунків
    QStringLiteral("INSERT OR IGNORE INTO account_subconto(account_id, line_no, subconto_kind_id, "
                   "is_required) "
                   "SELECT id, 1, 'nomenclature', 1 FROM accounts WHERE code IN ('201', '202', "
                   "'203', '231', '26')")};

const QStringList kMigrationV20 = {
    // 1. Додавання employee_id до документів (для авансового звіту, виплати зарплати, підзвіту)
    QStringLiteral("ALTER TABLE documents ADD COLUMN employee_id INTEGER REFERENCES employees(id) "
                   "ON DELETE RESTRICT"),
    // 2. Додавання субрахунків для розрахунків з підзвітними особами та витрат на збут
    QStringLiteral("INSERT OR IGNORE INTO accounts (code, name, parent_id, account_class, "
                   "normal_side, has_counterparty, is_folder, is_active) VALUES "
                   "('372', 'Розрахунки з підзвітними особами', (SELECT id FROM accounts WHERE "
                   "code = '37'), 3, 'debit', 0, 1, 1),"
                   "('3721', 'Розрахунки з підзвітними особами (UAH)', (SELECT id FROM accounts "
                   "WHERE code = '372'), 3, 'debit', 0, 0, 1),"
                   "('3722', 'Розрахунки з підзвітними особами (валюта)', (SELECT id FROM accounts "
                   "WHERE code = '372'), 3, 'debit', 0, 0, 1),"
                   "('6442', 'Податковий кредит за чеками / без отримання ПН', (SELECT id FROM "
                   "accounts WHERE code = '64'), 6, 'debit', 0, 0, 1),"
                   "('93', 'Витрати на збут', NULL, 9, 'debit', 0, 0, 1)"),
    // 3. Субконто для рахунків підзвітних осіб (Працівник)
    QStringLiteral("INSERT OR IGNORE INTO account_subconto(account_id, line_no, subconto_kind_id, "
                   "is_required) "
                   "SELECT id, 1, 'employee', 1 FROM accounts WHERE code IN ('3721', '3722')")};

Status recordMigration(QSqlDatabase db, int version)
{
  sql::Query insert(db);
  Status status =
      insert.prepare(QStringLiteral("INSERT INTO schema_migrations(version, applied_at) "
                                    "VALUES(:v, datetime('now'))"));
  if (!status) {
    return status;
  }
  insert.bind(QStringLiteral(":v"), version);
  return insert.exec();
}

Status applyStatements(QSqlDatabase db, const QStringList & statements)
{
  for (const QString & statement : statements) {
    sql::Query step(db);
    Status status = step.exec(statement);
    if (!status) {
      return status;
    }
  }
  return Status::ok();
}

Status applyFixedAssetWriteOffMigration(QSqlDatabase db)
{
  sql::Query pragma(db);
  Status status = pragma.exec(QStringLiteral("PRAGMA foreign_keys = OFF"));
  if (!status) {
    return status;
  }
  Transaction txn(db);
  if (!txn.started()) {
    static_cast<void>(pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON")));
    return txn.startError();
  }
  const QStringList statements = {
      QStringLiteral(
          "CREATE TABLE fixed_assets_new ("
          "  id INTEGER PRIMARY KEY,"
          "  inventory_no TEXT NOT NULL UNIQUE COLLATE NOCASE,"
          "  name TEXT NOT NULL,"
          "  asset_account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE RESTRICT,"
          "  expense_account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE RESTRICT,"
          "  commissioned_on TEXT,"
          "  disposed_on TEXT,"
          "  original_cents INTEGER NOT NULL CHECK(original_cents > 0),"
          "  salvage_cents INTEGER NOT NULL DEFAULT 0 CHECK(salvage_cents >= 0),"
          "  opening_accumulated_cents INTEGER NOT NULL DEFAULT 0 "
          "    CHECK(opening_accumulated_cents >= 0),"
          "  accumulated_cents INTEGER NOT NULL DEFAULT 0 CHECK(accumulated_cents >= 0),"
          "  useful_life_months INTEGER NOT NULL CHECK(useful_life_months > 0),"
          "  status TEXT NOT NULL DEFAULT 'draft' "
          "    CHECK(status IN ('draft','in_use','disposed')),"
          "  is_active INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0,1)),"
          "  CHECK(length(trim(inventory_no)) > 0),"
          "  CHECK(length(trim(name)) > 0),"
          "  CHECK(salvage_cents < original_cents),"
          "  CHECK(opening_accumulated_cents <= original_cents - salvage_cents),"
          "  CHECK(disposed_on IS NULL OR status = 'disposed'),"
          "  CHECK(status <> 'disposed' OR (commissioned_on IS NOT NULL AND "
          "        disposed_on IS NOT NULL))"
          ")"),
      QStringLiteral("INSERT INTO fixed_assets_new("
                     "  id, inventory_no, name, asset_account_id, expense_account_id, "
                     "  commissioned_on, disposed_on, original_cents, salvage_cents, "
                     "  opening_accumulated_cents, accumulated_cents, useful_life_months, "
                     "  status, is_active) "
                     "SELECT id, inventory_no, name, asset_account_id, expense_account_id, "
                     "  commissioned_on, NULL, original_cents, salvage_cents, "
                     "  opening_accumulated_cents, accumulated_cents, useful_life_months, "
                     "  status, is_active FROM fixed_assets"),
      QStringLiteral("DROP TABLE fixed_assets"),
      QStringLiteral("ALTER TABLE fixed_assets_new RENAME TO fixed_assets")};
  status = applyStatements(db, statements);
  if (!status) {
    static_cast<void>(pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON")));
    return status;
  }
  status = recordMigration(db, 11);
  if (!status) {
    static_cast<void>(pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON")));
    return status;
  }
  status = txn.commit();
  const Status keysOn = pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
  if (!status) {
    return status;
  }
  if (!keysOn) {
    return keysOn;
  }
  sql::Query check(db);
  status = check.exec(QStringLiteral("PRAGMA foreign_key_check"));
  if (!status) {
    return status;
  }
  if (check.next()) {
    return Status::fail(
        Error::database(QStringLiteral("після міграції ОЗ залишились порушення зовнішніх ключів")));
  }
  return Status::ok();
}

Status applyIntangibleAssetsMigration(QSqlDatabase db)
{
  Transaction txn(db);
  if (!txn.started()) {
    return txn.startError();
  }
  Status status = applyStatements(db, kMigrationV12);
  if (!status) {
    return status;
  }
  status = recordMigration(db, 12);
  if (!status) {
    return status;
  }
  return txn.commit();
}

Status applyVatMigration(QSqlDatabase db)
{
  Transaction txn(db);
  if (!txn.started()) {
    return txn.startError();
  }
  Status status = applyStatements(db, kMigrationV13);
  if (!status) {
    return status;
  }
  status = recordMigration(db, 13);
  if (!status) {
    return status;
  }
  return txn.commit();
}

Status applyEdoMigration(QSqlDatabase db)
{
  Transaction txn(db);
  if (!txn.started()) {
    return txn.startError();
  }
  Status status = applyStatements(db, kMigrationV14);
  if (!status) {
    return status;
  }
  status = recordMigration(db, 14);
  if (!status) {
    return status;
  }
  return txn.commit();
}

Status applyPayrollMigration(QSqlDatabase db)
{
  Transaction txn(db);
  if (!txn.started()) {
    return txn.startError();
  }
  Status status = applyStatements(db, kMigrationV15);
  if (!status) {
    return status;
  }
  status = recordMigration(db, 15);
  if (!status) {
    return status;
  }
  return txn.commit();
}

Status applySubcontoMigration(QSqlDatabase db)
{
  Transaction txn(db);
  if (!txn.started()) {
    return txn.startError();
  }
  Status status = applyStatements(db, kMigrationV16);
  if (!status) {
    return status;
  }
  status = recordMigration(db, 16);
  if (!status) {
    return status;
  }
  return txn.commit();
}

Status applyTradeWarehouseMigration(QSqlDatabase db)
{
  Transaction txn(db);
  if (!txn.started()) {
    return txn.startError();
  }
  Status status = applyStatements(db, kMigrationV17);
  if (!status) {
    return status;
  }
  status = recordMigration(db, 17);
  if (!status) {
    return status;
  }
  return txn.commit();
}

Status applyCurrencyMigration(QSqlDatabase db)
{
  Transaction txn(db);
  if (!txn.started()) {
    return txn.startError();
  }
  Status status = applyStatements(db, kMigrationV18);
  if (!status) {
    return status;
  }
  status = recordMigration(db, 18);
  if (!status) {
    return status;
  }
  return txn.commit();
}

Status applyProductionMigration(QSqlDatabase db)
{
  Transaction txn(db);
  if (!txn.started()) {
    return txn.startError();
  }
  Status status = applyStatements(db, kMigrationV19);
  if (!status) {
    return status;
  }
  status = recordMigration(db, 19);
  if (!status) {
    return status;
  }
  return txn.commit();
}

Status applyExpenseReportMigration(QSqlDatabase db)
{
  Transaction txn(db);
  if (!txn.started()) {
    return txn.startError();
  }
  Status status = applyStatements(db, kMigrationV20);
  if (!status) {
    return status;
  }
  status = recordMigration(db, 20);
  if (!status) {
    return status;
  }
  return txn.commit();
}

Status applyMonobankMigration(QSqlDatabase db)
{
  Transaction txn(db);
  if (!txn.started()) {
    return txn.startError();
  }
  const QStringList stmts = {
      QStringLiteral("ALTER TABLE company_profile ADD COLUMN monobank_token TEXT")};
  Status status = applyStatements(db, stmts);
  if (!status) {
    return status;
  }
  status = recordMigration(db, 22);
  if (!status) {
    return status;
  }
  return txn.commit();
}

Status applyCompanyFlagsMigration(QSqlDatabase db)
{
  Transaction txn(db);
  if (!txn.started()) {
    return txn.startError();
  }
  Status status = applyStatements(
      db, {QStringLiteral("ALTER TABLE company_profile ADD COLUMN allow_negative_stock "
                          "INTEGER NOT NULL DEFAULT 0 "
                          "CHECK(allow_negative_stock IN (0,1))")});
  if (!status) {
    return status;
  }
  // Перенесення локального QSettings-прапорця в картку підприємства.
  QSettings settings;
  if (settings.value(QStringLiteral("business/allow_negative_stock"), false).toBool()) {
    sql::Query query(db);
    status = query.exec(
        QStringLiteral("UPDATE company_profile SET allow_negative_stock = 1 WHERE id = 1"));
    if (!status) {
      return status;
    }
    settings.remove(QStringLiteral("business/allow_negative_stock"));
  }
  status = recordMigration(db, 23);
  if (!status) {
    return status;
  }
  return txn.commit();
}

Status applyPrroMigration(QSqlDatabase db)
{
  Transaction txn(db);
  if (!txn.started()) {
    return txn.startError();
  }
  const QStringList stmts = {
      QStringLiteral("ALTER TABLE company_profile ADD COLUMN prro_enabled INTEGER NOT NULL "
                     "DEFAULT 0 CHECK(prro_enabled IN (0,1))"),
      QStringLiteral("ALTER TABLE company_profile ADD COLUMN checkbox_license_key TEXT"),
      QStringLiteral("ALTER TABLE company_profile ADD COLUMN checkbox_cashier_login TEXT"),
      QStringLiteral("ALTER TABLE company_profile ADD COLUMN checkbox_cashier_pin TEXT"),
      QStringLiteral("ALTER TABLE company_profile ADD COLUMN prro_payment_cash INTEGER NOT NULL "
                     "DEFAULT 1 CHECK(prro_payment_cash IN (0,1))"),
      QStringLiteral("ALTER TABLE documents ADD COLUMN fiscal_receipt_id TEXT"),
      QStringLiteral("ALTER TABLE documents ADD COLUMN fiscal_code TEXT"),
      QStringLiteral("ALTER TABLE documents ADD COLUMN fiscal_status TEXT")};
  Status status = applyStatements(db, stmts);
  if (!status) {
    return status;
  }
  status = recordMigration(db, 24);
  if (!status) {
    return status;
  }
  return txn.commit();
}

Status applyDocumentLineCounterpartyMigration(QSqlDatabase db)
{
  Transaction txn(db);
  if (!txn.started()) {
    return txn.startError();
  }
  const QStringList stmts = {
      QStringLiteral("ALTER TABLE document_lines ADD COLUMN counterparty_id INTEGER "
                     "REFERENCES counterparties(id) ON DELETE RESTRICT")};
  Status status = applyStatements(db, stmts);
  if (!status) {
    return status;
  }
  status = recordMigration(db, 25);
  if (!status) {
    return status;
  }
  return txn.commit();
}

Status applyTaxOfficeMigration(QSqlDatabase db)
{
  Transaction txn(db);
  if (!txn.started()) {
    return txn.startError();
  }
  const QStringList stmts = {
      QStringLiteral("ALTER TABLE company_profile ADD COLUMN tax_office_region INTEGER NOT NULL "
                     "DEFAULT 0"),
      QStringLiteral("ALTER TABLE company_profile ADD COLUMN tax_office_district INTEGER NOT NULL "
                     "DEFAULT 0"),
      QStringLiteral("ALTER TABLE company_profile ADD COLUMN tax_office_code INTEGER NOT NULL "
                     "DEFAULT 0")};
  Status status = applyStatements(db, stmts);
  if (!status) {
    return status;
  }
  status = recordMigration(db, 26);
  if (!status) {
    return status;
  }
  return txn.commit();
}

// Міграція 21: розширений кадровий облік (1.5 дорожньої карти).
// Додає до таблиці employees поля посади, підрозділу, дати звільнення та
// прапорець Податкової соціальної пільги (ПСП, п. 169 ПКУ).
// Нові таблиці:
//   hr_orders          — кадрові накази (прийом, переведення, звільнення)
//   timesheets         — табелі обліку робочого часу (форма П-5)
//   vacation_records   — відпустки з розрахунком середньоденного (КМУ № 100)
//   sick_leave_records — лікарняні листки (перші 5 днів — роботодавець, решта — ПФУ)
Status applyHrExtensionMigration(QSqlDatabase db)
{
  Transaction txn(db);
  if (!txn.started()) {
    return txn.startError();
  }
  const QStringList stmts = {
      // Нові поля картки працівника
      QStringLiteral("ALTER TABLE employees ADD COLUMN position TEXT"),
      QStringLiteral("ALTER TABLE employees ADD COLUMN department TEXT"),
      QStringLiteral("ALTER TABLE employees ADD COLUMN fired_on TEXT"),
      QStringLiteral("ALTER TABLE employees ADD COLUMN has_pit_benefit INTEGER "
                     "NOT NULL DEFAULT 0 CHECK(has_pit_benefit IN (0,1))"),
      // Кадрові накази
      QStringLiteral(
          "CREATE TABLE hr_orders ("
          "  id INTEGER PRIMARY KEY,"
          "  document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
          "  employee_id INTEGER NOT NULL REFERENCES employees(id) ON DELETE RESTRICT,"
          "  order_type TEXT NOT NULL "
          "    CHECK(order_type IN ('hire','transfer','dismiss')),"
          "  effective_date TEXT NOT NULL,"
          "  new_position TEXT,"
          "  new_department TEXT,"
          "  new_salary_cents INTEGER CHECK(new_salary_cents IS NULL OR new_salary_cents > 0),"
          "  dismiss_reason TEXT,"
          "  UNIQUE(employee_id, effective_date, order_type)"
          ")"),
      // Табелі обліку робочого часу (форма П-5)
      QStringLiteral("CREATE TABLE timesheets ("
                     "  id INTEGER PRIMARY KEY,"
                     "  employee_id INTEGER NOT NULL REFERENCES employees(id) ON DELETE RESTRICT,"
                     "  year INTEGER NOT NULL CHECK(year >= 2000),"
                     "  month INTEGER NOT NULL CHECK(month BETWEEN 1 AND 12),"
                     "  days_worked INTEGER NOT NULL DEFAULT 0 CHECK(days_worked >= 0),"
                     "  hours_worked INTEGER NOT NULL DEFAULT 0 CHECK(hours_worked >= 0),"
                     "  vacation_days INTEGER NOT NULL DEFAULT 0 CHECK(vacation_days >= 0),"
                     "  sick_days INTEGER NOT NULL DEFAULT 0 CHECK(sick_days >= 0),"
                     "  absence_days INTEGER NOT NULL DEFAULT 0 CHECK(absence_days >= 0),"
                     "  working_days_norm INTEGER NOT NULL DEFAULT 0 CHECK(working_days_norm >= 0),"
                     "  UNIQUE(employee_id, year, month)"
                     ")"),
      QStringLiteral("CREATE INDEX idx_timesheets_ym ON timesheets(year, month)"),
      // Відпускні (постанова КМУ № 100)
      QStringLiteral("CREATE TABLE vacation_records ("
                     "  id INTEGER PRIMARY KEY,"
                     "  employee_id INTEGER NOT NULL REFERENCES employees(id) ON DELETE RESTRICT,"
                     "  document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
                     "  vacation_from TEXT NOT NULL,"
                     "  vacation_to TEXT NOT NULL,"
                     "  vacation_days INTEGER NOT NULL CHECK(vacation_days > 0),"
                     "  avg_daily_cents INTEGER NOT NULL CHECK(avg_daily_cents > 0),"
                     "  gross_cents INTEGER NOT NULL CHECK(gross_cents > 0)"
                     ")"),
      // Лікарняні листки
      QStringLiteral(
          "CREATE TABLE sick_leave_records ("
          "  id INTEGER PRIMARY KEY,"
          "  employee_id INTEGER NOT NULL REFERENCES employees(id) ON DELETE RESTRICT,"
          "  document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
          "  sick_from TEXT NOT NULL,"
          "  sick_to TEXT NOT NULL,"
          "  total_days INTEGER NOT NULL CHECK(total_days > 0),"
          "  employer_days INTEGER NOT NULL CHECK(employer_days >= 0),"
          "  pfu_days INTEGER NOT NULL CHECK(pfu_days >= 0),"
          "  employer_gross_cents INTEGER NOT NULL DEFAULT 0 CHECK(employer_gross_cents >= 0),"
          "  avg_daily_cents INTEGER NOT NULL CHECK(avg_daily_cents > 0),"
          "  experience_years INTEGER NOT NULL DEFAULT 0 CHECK(experience_years >= 0)"
          ")"),
  };
  Status status = applyStatements(db, stmts);
  if (!status) {
    return status;
  }
  status = recordMigration(db, 21);
  if (!status) {
    return status;
  }
  return txn.commit();
}

} // namespace

Database::Database(Database && other) noexcept
    : connectionName_(std::move(other.connectionName_)), db_(other.db_)
{
  other.db_ = QSqlDatabase();
  other.connectionName_.clear();
}

Database & Database::operator=(Database && other) noexcept
{
  if (this == &other) {
    return *this;
  }
  this->~Database();
  new (this) Database(std::move(other));
  return *this;
}

Database::~Database()
{
  if (connectionName_.isEmpty()) {
    return;
  }
  {
    QSqlDatabase db = db_;
    db_ = QSqlDatabase();
    if (db.isValid()) {
      if (db.databaseName() != QStringLiteral(":memory:")) {
        sql::Query checkpoint(db);
        const Status ignored = checkpoint.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
        (void)ignored;
      }
      db.close();
    }
  }
  QSqlDatabase::removeDatabase(connectionName_);
}

Result<Database> Database::openFile(const QString & path)
{
  const QFileInfo info(path);
  if (!info.dir().exists() && !QDir().mkpath(info.absolutePath())) {
    return Result<Database>::fail(Error::database(
        QStringLiteral("не вдалося створити каталог бази: %1").arg(info.absolutePath())));
  }
  const QString name =
      QStringLiteral("fin-file-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
  return open(name, info.absoluteFilePath());
}

Result<Database> Database::openMemory()
{
  const QString name =
      QStringLiteral("fin-mem-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
  return open(name, QStringLiteral(":memory:"));
}

Result<Database> Database::open(const QString & connectionName, const QString & databaseName)
{
  Database database;
  database.connectionName_ = connectionName;
  database.db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
  if (!database.db_.isValid()) {
    return Result<Database>::fail(Error::database(QStringLiteral("драйвер QSQLITE недоступний")));
  }
  database.db_.setDatabaseName(databaseName);
  if (!database.db_.open()) {
    const QString text = database.db_.lastError().text();
    database.db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
    return Result<Database>::fail(
        Error::database(QStringLiteral("не вдалося відкрити базу: %1").arg(text)));
  }

  Status status = database.applyPragmas();
  if (!status) {
    return Result<Database>::fail(status.error());
  }
  status = database.migrate();
  if (!status) {
    return Result<Database>::fail(status.error());
  }
  status = database.applyPragmas();
  if (!status) {
    return Result<Database>::fail(status.error());
  }
  status = database.checkStorage();
  if (!status) {
    return Result<Database>::fail(status.error());
  }
  return Result<Database>::ok(std::move(database));
}

Status Database::applyPragmas()
{
  sql::Query query(db_);
  Status status = query.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
  if (!status) {
    return status;
  }
  status = query.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
  if (!status) {
    return status;
  }
  status = query.exec(QStringLiteral("PRAGMA synchronous = NORMAL"));
  if (!status) {
    return status;
  }
  if (db_.databaseName() != QStringLiteral(":memory:")) {
    status = query.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    if (!status) {
      return status;
    }
  }
  return Status::ok();
}

Status Database::migrate()
{
  Status status = Status::ok();
  {
    sql::Query query(db_);
    status = query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS schema_migrations ("
                                       "  version INTEGER PRIMARY KEY,"
                                       "  applied_at TEXT NOT NULL)"));
    if (!status) {
      return status;
    }
  }

  int current = 0;
  {
    sql::Query query(db_);
    status = query.prepare(QStringLiteral("SELECT MAX(version) FROM schema_migrations"));
    if (!status) {
      return status;
    }
    status = query.exec();
    if (!status) {
      return status;
    }
    if (query.next()) {
      current = query.value(0).toInt();
    }
  }

  if (current >= kSchemaVersion) {
    return Status::ok();
  }

  Transaction txn(db_);
  if (!txn.started()) {
    return txn.startError();
  }

  if (current < 1) {
    status = applyStatements(db_, kMigrationV1);
    if (!status) {
      return status;
    }
    status = recordMigration(db_, 1);
    if (!status) {
      return status;
    }
  }
  if (current < 2) {
    status = applyStatements(db_, kMigrationV2);
    if (!status) {
      return status;
    }
    status = recordMigration(db_, 2);
    if (!status) {
      return status;
    }
  }
  if (current < 3) {
    status = applyStatements(db_, kMigrationV3);
    if (!status) {
      return status;
    }
    status = recordMigration(db_, 3);
    if (!status) {
      return status;
    }
  }
  if (current < 4) {
    status = applyStatements(db_, kMigrationV4);
    if (!status) {
      return status;
    }
    status = recordMigration(db_, 4);
    if (!status) {
      return status;
    }
  }
  if (current < 5) {
    status = applyStatements(db_, kMigrationV5);
    if (!status) {
      return status;
    }
    status = recordMigration(db_, 5);
    if (!status) {
      return status;
    }
  }
  if (current < 6) {
    status = applyStatements(db_, kMigrationV6);
    if (!status) {
      return status;
    }
    status = recordMigration(db_, 6);
    if (!status) {
      return status;
    }
  }
  if (current < 7) {
    status = applyStatements(db_, kMigrationV7);
    if (!status) {
      return status;
    }
    status = recordMigration(db_, 7);
    if (!status) {
      return status;
    }
  }
  if (current < 8) {
    status = applyStatements(db_, kMigrationV8);
    if (!status) {
      return status;
    }
    status = recordMigration(db_, 8);
    if (!status) {
      return status;
    }
  }
  if (current < 9) {
    status = applyStatements(db_, kMigrationV9);
    if (!status) {
      return status;
    }
    status = recordMigration(db_, 9);
    if (!status) {
      return status;
    }
  }
  if (current < 10) {
    status = applyStatements(db_, kMigrationV10);
    if (!status) {
      return status;
    }
    status = recordMigration(db_, 10);
    if (!status) {
      return status;
    }
  }

  status = txn.commit();
  if (!status) {
    return status;
  }
  if (current < 11) {
    status = applyFixedAssetWriteOffMigration(db_);
    if (!status) {
      return status;
    }
  }
  if (current < 12) {
    status = applyIntangibleAssetsMigration(db_);
    if (!status) {
      return status;
    }
  }
  if (current < 13) {
    status = applyVatMigration(db_);
    if (!status) {
      return status;
    }
  }
  if (current < 14) {
    status = applyEdoMigration(db_);
    if (!status) {
      return status;
    }
  }
  if (current < 15) {
    status = applyPayrollMigration(db_);
    if (!status) {
      return status;
    }
  }
  if (current < 16) {
    status = applySubcontoMigration(db_);
    if (!status) {
      return status;
    }
  }
  if (current < 17) {
    status = applyTradeWarehouseMigration(db_);
    if (!status) {
      return status;
    }
  }
  if (current < 18) {
    status = applyCurrencyMigration(db_);
    if (!status) {
      return status;
    }
  }
  if (current < 19) {
    status = applyProductionMigration(db_);
    if (!status) {
      return status;
    }
  }
  if (current < 20) {
    status = applyExpenseReportMigration(db_);
    if (!status) {
      return status;
    }
  }
  if (current < 21) {
    status = applyHrExtensionMigration(db_);
    if (!status) {
      return status;
    }
  }
  if (current < 22) {
    status = applyMonobankMigration(db_);
    if (!status) {
      return status;
    }
  }
  if (current < 23) {
    status = applyCompanyFlagsMigration(db_);
    if (!status) {
      return status;
    }
  }
  if (current < 24) {
    status = applyPrroMigration(db_);
    if (!status) {
      return status;
    }
  }
  if (current < 25) {
    status = applyDocumentLineCounterpartyMigration(db_);
    if (!status) {
      return status;
    }
  }
  if (current < 26) {
    status = applyTaxOfficeMigration(db_);
    if (!status) {
      return status;
    }
  }
  return Status::ok();
}

Status Database::checkStorage()
{
  sql::Query keys(db_);
  Status status = keys.exec(QStringLiteral("PRAGMA foreign_keys"));
  if (!status) {
    return status;
  }
  if (!keys.next() || keys.value(0).toInt() != 1) {
    return Status::fail(Error::database(QStringLiteral("не вдалося увімкнути foreign_keys")));
  }

  sql::Query check(db_);
  status = check.exec(QStringLiteral("PRAGMA quick_check"));
  if (!status) {
    return status;
  }
  if (!check.next() ||
      check.value(0).toString().compare(QStringLiteral("ok"), Qt::CaseInsensitive) != 0) {
    return Status::fail(Error::database(QStringLiteral("файл бази пошкоджено (quick_check)")));
  }

  sql::Query fk(db_);
  status = fk.exec(QStringLiteral("PRAGMA foreign_key_check"));
  if (!status) {
    return status;
  }
  if (fk.next()) {
    return Status::fail(Error::database(QStringLiteral("порушення зовнішніх ключів у базі")));
  }
  return Status::ok();
}

Result<QString> Database::backupTo(const QString & destinationPath) const
{
  const QString trimmed = destinationPath.trimmed();
  if (trimmed.isEmpty()) {
    return Result<QString>::fail(
        Error::invalidArgument(QStringLiteral("не вказано файл резервної копії")));
  }
  const QFileInfo dest(trimmed);
  if (dest.exists()) {
    return Result<QString>::fail(Error::conflict(
        QStringLiteral("файл резервної копії вже існує: %1").arg(dest.absoluteFilePath())));
  }
  if (!dest.dir().exists() && !QDir().mkpath(dest.absolutePath())) {
    return Result<QString>::fail(Error::database(
        QStringLiteral("не вдалося створити каталог копії: %1").arg(dest.absolutePath())));
  }
  const QString absolute = dest.absoluteFilePath();
  if (!db_.databaseName().isEmpty() && QFileInfo(db_.databaseName()).exists() &&
      QFileInfo(db_.databaseName()).absoluteFilePath() == absolute) {
    return Result<QString>::fail(
        Error::invalidArgument(QStringLiteral("не можна писати копію поверх відкритої бази")));
  }

  QString escaped = absolute;
  escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
  sql::Query vacuum(db_);
  const Status status = vacuum.exec(QStringLiteral("VACUUM INTO '%1'").arg(escaped));
  if (!status) {
    return Result<QString>::fail(status.error());
  }
  return Result<QString>::ok(absolute);
}

} // namespace fin
