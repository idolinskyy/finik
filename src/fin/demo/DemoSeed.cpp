#include "fin/demo/DemoSeed.h"

#include "fin/auth/Access.h"
#include "fin/db/CurrencyRepository.h"
#include "fin/db/UserRepository.h"
#include "fin/domain/BillOfMaterials.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/Contract.h"
#include "fin/domain/Currency.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/Employee.h"
#include "fin/domain/FixedAsset.h"
#include "fin/domain/IntangibleAsset.h"
#include "fin/domain/LowValueItem.h"
#include "fin/domain/User.h"
#include "fin/domain/Vat.h"
#include "fin/domain/Warehouse.h"

#include <QDate>

#include <random>

namespace fin {
namespace {

const QString kDemoCompanyName = QStringLiteral("ТОВ «Демо-облік»");

struct DemoRefs
{
  CounterpartyId buyer;
  CounterpartyId shop;
  CounterpartyId mill;
  CounterpartyId oilCo;
  CounterpartyId logistic;
  CounterpartyId foreignSupplier;
  NomenclatureId flour;
  NomenclatureId sugar;
  NomenclatureId oil;
  NomenclatureId coffee;
  NomenclatureId consult;
};

int between(std::mt19937 & rng, int lo, int hi)
{
  std::uniform_int_distribution<int> dist(lo, hi);
  return dist(rng);
}

Status postNew(LedgerSession & session, Document document)
{
  Result<DocumentId> id = session.documents().insert(document);
  if (!id) {
    return id.asStatus();
  }
  return session.posting().post(id.value());
}

Document numbered(LedgerSession & session, DocumentType type, QDate date)
{
  Document document;
  document.type = type;
  document.date = date;
  const Result<QString> number = session.documents().allocateNumber(type);
  if (number) {
    document.number = number.value();
  }
  return document;
}

bool hasType(LedgerSession & session, DocumentType type, int year)
{
  const Result<std::vector<DocumentHeader>> list =
      session.documents().list(QDate(year, 1, 1), QDate(year, 12, 31), type);
  return list && !list.value().empty();
}

Result<GoodsLine> priced(NomenclatureId id, std::int64_t milli, std::int64_t price,
                         const QString & description = QString())
{
  GoodsLine row;
  row.nomenclatureId = id;
  row.quantity = Quantity::fromMilli(milli);
  row.unitPrice = Money::fromCents(price);
  row.description = description;
  const Result<Money> amount = row.quantity.times(row.unitPrice);
  if (!amount) {
    return Result<GoodsLine>::fail(amount.error());
  }
  row.amount = amount.value();
  return Result<GoodsLine>::ok(row);
}

Result<CounterpartyId> ensureParty(LedgerSession & session, const QString & name, bool customer,
                                   bool supplier, const QString & edrpou, const QString & iban,
                                   const QString & bank)
{
  Result<Counterparty> found = session.counterparties().findByEdrpou(edrpou);
  if (found) {
    Counterparty row = found.value();
    bool dirty = false;
    if (row.iban.trimmed().isEmpty() && !iban.isEmpty()) {
      row.iban = iban;
      row.bankName = bank;
      dirty = true;
    }
    if (row.name != name) {
      row.name = name;
      dirty = true;
    }
    row.isCustomer = row.isCustomer || customer;
    row.isSupplier = row.isSupplier || supplier;
    if (dirty) {
      const Status status = session.counterparties().update(row);
      if (!status) {
        return Result<CounterpartyId>::fail(status.error());
      }
    }
    return Result<CounterpartyId>::ok(row.id);
  }
  if (found.error().code != ErrorCode::NotFound) {
    return Result<CounterpartyId>::fail(found.error());
  }
  Counterparty row;
  row.name = name;
  row.isCustomer = customer;
  row.isSupplier = supplier;
  row.edrpou = edrpou;
  row.iban = iban;
  row.bankName = bank;
  return session.counterparties().insert(row);
}

Result<NomenclatureId> ensureItem(LedgerSession & session, const QString & name,
                                  const QString & sku, const QString & unit)
{
  const Result<std::vector<Nomenclature>> items = session.nomenclature().listAll();
  if (!items) {
    return Result<NomenclatureId>::fail(items.error());
  }
  for (const Nomenclature & row : items.value()) {
    if (row.sku == sku) {
      return Result<NomenclatureId>::ok(row.id);
    }
  }
  Nomenclature row;
  row.name = name;
  row.sku = sku;
  row.unit = unit;
  return session.nomenclature().insert(row);
}

Status saveDemoCompany(LedgerSession & session)
{
  CompanyProfile company;
  company.name = kDemoCompanyName;
  company.edrpou = QStringLiteral("44556677");
  company.iban = QStringLiteral("UA953220010000000000000001234");
  company.bankName = QStringLiteral("Ощадбанк");
  company.address = QStringLiteral("м. Київ, вул. Облікова, 1");
  company.accountant = QStringLiteral("Коваль О.П.");
  company.cashier = QStringLiteral("Сидоренко І.М.");
  company.taxSystem = TaxSystem::Simplified;
  company.singleTaxRateBp = 500;
  company.vatPayer = true;
  company.vatRateBp = 2000;
  company.taxOfficeRegion = 26;
  company.taxOfficeDistrict = 50;
  company.taxOfficeCode = 2650;
  return session.company().save(company);
}

Result<DemoRefs> ensureDirectories(LedgerSession & session)
{
  DemoRefs refs;
  const Result<CounterpartyId> buyer = ensureParty(
      session, QStringLiteral("ТОВ Покупець"), true, false, QStringLiteral("11111111"),
      QStringLiteral("UA863052990000000000000004321"), QStringLiteral("Райффайзен Банк"));
  const Result<CounterpartyId> shop =
      ensureParty(session, QStringLiteral("ФОП Мельник"), true, false, QStringLiteral("2222222222"),
                  QStringLiteral("UA963204780000000000000001111"), QStringLiteral("ПУМБ"));
  const Result<CounterpartyId> mill = ensureParty(
      session, QStringLiteral("ТОВ ПостачЗерно"), false, true, QStringLiteral("33333333"),
      QStringLiteral("UA783003350000000000000000001"), QStringLiteral("ПриватБанк"));
  const Result<CounterpartyId> oilCo =
      ensureParty(session, QStringLiteral("ТОВ Олія-Опт"), true, true, QStringLiteral("44444444"),
                  QStringLiteral("UA573003350000000000000002222"), QStringLiteral("ПриватБанк"));
  const Result<CounterpartyId> logistic = ensureParty(
      session, QStringLiteral("ТОВ Логістик Плюс"), true, false, QStringLiteral("55555555"),
      QStringLiteral("UA003510050000000000000007777"), QStringLiteral("Укрсиббанк"));
  const Result<CounterpartyId> treasury =
      ensureParty(session, QStringLiteral("ГУ ДКСУ у м. Києві (податки)"), false, true,
                  QStringLiteral("37567890"), QString(), QStringLiteral("Казначейство України"));
  const Result<CounterpartyId> foreignSupplier =
      ensureParty(session, QStringLiteral("Global Cloud Services Inc."), false, true,
                  QStringLiteral("99887766"), QString(), QStringLiteral("JPMorgan Chase Bank"));
  if (!buyer || !shop || !mill || !oilCo || !logistic || !treasury || !foreignSupplier) {
    return Result<DemoRefs>::fail(
        Error::database(QStringLiteral("не вдалося створити контрагентів")));
  }
  refs.buyer = buyer.value();
  refs.shop = shop.value();
  refs.mill = mill.value();
  refs.oilCo = oilCo.value();
  refs.logistic = logistic.value();
  refs.foreignSupplier = foreignSupplier.value();

  const Result<NomenclatureId> flour = ensureItem(session, QStringLiteral("Борошно в/г"),
                                                  QStringLiteral("FL-01"), QStringLiteral("кг"));
  const Result<NomenclatureId> sugar = ensureItem(session, QStringLiteral("Цукор білий"),
                                                  QStringLiteral("SG-02"), QStringLiteral("кг"));
  const Result<NomenclatureId> oil = ensureItem(session, QStringLiteral("Олія соняшникова"),
                                                QStringLiteral("OL-03"), QStringLiteral("л"));
  const Result<NomenclatureId> coffee = ensureItem(session, QStringLiteral("Кава в зернах"),
                                                   QStringLiteral("CF-04"), QStringLiteral("кг"));
  const Result<NomenclatureId> consult =
      ensureItem(session, QStringLiteral("Консультація з обліку"), QStringLiteral("SV-05"),
                 QStringLiteral("послуга"));
  if (!flour || !sugar || !oil || !coffee || !consult) {
    return Result<DemoRefs>::fail(
        Error::database(QStringLiteral("не вдалося створити номенклатуру")));
  }
  refs.flour = flour.value();
  refs.sugar = sugar.value();
  refs.oil = oil.value();
  refs.coffee = coffee.value();
  refs.consult = consult.value();

  // Додатковий склад для демо
  const Result<std::vector<Warehouse>> whs = session.warehouses().listAll();
  if (whs && whs.value().size() < 2) {
    Warehouse wh2;
    wh2.code = QStringLiteral("WH-02");
    wh2.name = QStringLiteral("Склад № 2 (Роздрібний)");
    wh2.address = QStringLiteral("м. Київ, вул. Торгова, 15");
    wh2.isActive = true;
    (void)session.warehouses().insert(wh2);
  }

  // Договори контрагентів
  const Result<std::vector<Contract>> buyerContracts =
      session.contracts().listByCounterparty(refs.buyer);
  if (buyerContracts && buyerContracts.value().empty()) {
    Contract c;
    c.counterpartyId = refs.buyer;
    c.number = QStringLiteral("ДОГ-ПОК-2026/01");
    c.name = QStringLiteral("Основний договір поставки");
    c.contractType = ContractType::WithCustomer;
    c.date = QDate(2026, 1, 1);
    (void)session.contracts().insert(c);
  }

  const Result<std::vector<Contract>> millContracts =
      session.contracts().listByCounterparty(refs.mill);
  if (millContracts && millContracts.value().empty()) {
    Contract c;
    c.counterpartyId = refs.mill;
    c.number = QStringLiteral("ДОГ-ПОСТ-2026/01");
    c.name = QStringLiteral("Договір закупівлі зернових");
    c.contractType = ContractType::WithSupplier;
    c.date = QDate(2026, 1, 1);
    (void)session.contracts().insert(c);
  }

  // Офіційні курси валют для демо (2026 рік)
  const Result<CurrencyRate> usdCheck =
      session.currencies().getRate(QStringLiteral("USD"), QDate(2026, 1, 1));
  if (!usdCheck) {
    (void)session.currencies().setRate({QStringLiteral("USD"), QDate(2026, 1, 1), 1, 41'500'000});
    (void)session.currencies().setRate({QStringLiteral("USD"), QDate(2026, 2, 1), 1, 41'750'000});
    (void)session.currencies().setRate({QStringLiteral("USD"), QDate(2026, 3, 1), 1, 42'100'000});

    (void)session.currencies().setRate({QStringLiteral("EUR"), QDate(2026, 1, 1), 1, 45'200'000});
    (void)session.currencies().setRate({QStringLiteral("EUR"), QDate(2026, 2, 1), 1, 45'500'000});
    (void)session.currencies().setRate({QStringLiteral("EUR"), QDate(2026, 3, 1), 1, 45'800'000});

    (void)session.currencies().setRate({QStringLiteral("PLN"), QDate(2026, 1, 1), 1, 10'500'000});
    (void)session.currencies().setRate({QStringLiteral("PLN"), QDate(2026, 2, 1), 1, 10'600'000});
    (void)session.currencies().setRate({QStringLiteral("PLN"), QDate(2026, 3, 1), 1, 10'700'000});
  }

  return Result<DemoRefs>::ok(refs);
}

Status seedCore(LedgerSession & session, std::mt19937 & rng, int year,
                const std::vector<Account> & accounts, const DemoRefs & refs)
{
  const AccountId cash = accountIdByCode(accounts, chart::kCash);
  const AccountId bank = accountIdByCode(accounts, chart::kBank);
  const AccountId capital = accountIdByCode(accounts, chart::kCapital);
  const AccountId customers = accountIdByCode(accounts, chart::kCustomers);
  const AccountId expense = accountIdByCode(accounts, chart::kAdminExpense);
  if (!cash || !bank || !capital || !customers || !expense) {
    return Status::fail(Error::validation(QStringLiteral("неповний план рахунків")));
  }

  Document opening = numbered(session, DocumentType::OpeningBalance, QDate(year, 1, 1));
  opening.comment = QStringLiteral("демо: вхідні залишки");
  std::vector<OpeningMoneyLine> money(3);
  money[0].accountId = cash;
  money[0].debit = Money::fromCents(2500000);
  money[1].accountId = bank;
  money[1].debit = Money::fromCents(18000000);
  money[2].accountId = capital;
  const Result<GoodsLine> flourOpen = priced(refs.flour, 20000, 4000);
  if (!flourOpen) {
    return flourOpen.asStatus();
  }
  const Result<GoodsLine> sugarOpen = priced(refs.sugar, 15000, 2800);
  if (!sugarOpen) {
    return sugarOpen.asStatus();
  }
  const Result<GoodsLine> oilOpen = priced(refs.oil, 8000, 7200);
  if (!oilOpen) {
    return oilOpen.asStatus();
  }
  const Result<GoodsLine> coffeeOpen = priced(refs.coffee, 4000, 18000);
  if (!coffeeOpen) {
    return coffeeOpen.asStatus();
  }
  money[2].credit = Money::fromCents(
      20500000 + flourOpen.value().amount.cents() + sugarOpen.value().amount.cents() +
      oilOpen.value().amount.cents() + coffeeOpen.value().amount.cents());
  const Result<std::vector<DocumentLine>> openingLines = composeOpening(
      accounts, money, {flourOpen.value(), sugarOpen.value(), oilOpen.value(), coffeeOpen.value()});
  if (!openingLines) {
    return openingLines.asStatus();
  }
  opening.lines = openingLines.value();
  Status status = postNew(session, opening);
  if (!status) {
    return status;
  }

  auto buy = [&](QDate date, CounterpartyId party, NomenclatureId item, std::int64_t milli,
                 std::int64_t price) -> Status {
    Document document = numbered(session, DocumentType::Purchase, date);
    document.counterpartyId = party;
    const Result<GoodsLine> row = priced(item, milli, price);
    if (!row) {
      return row.asStatus();
    }
    const Result<std::vector<DocumentLine>> lines = composePurchase(accounts, {row.value()});
    if (!lines) {
      return lines.asStatus();
    }
    document.lines = lines.value();
    return postNew(session, document);
  };

  auto sell = [&](QDate date, CounterpartyId party, NomenclatureId item, std::int64_t milli,
                  std::int64_t price) -> Status {
    Document document = numbered(session, DocumentType::Sale, date);
    document.counterpartyId = party;
    const Result<GoodsLine> row = priced(item, milli, price);
    if (!row) {
      return row.asStatus();
    }
    const Result<std::vector<DocumentLine>> lines = composeSale(accounts, {row.value()});
    if (!lines) {
      return lines.asStatus();
    }
    document.lines = lines.value();
    return postNew(session, document);
  };

  auto cashMove = [&](bool incoming, QDate date, CounterpartyId party, AccountId corr,
                      std::int64_t cents, const QString & text) -> Status {
    Document document =
        numbered(session, incoming ? DocumentType::CashIn : DocumentType::CashOut, date);
    document.counterpartyId = party;
    document.comment = text;
    if (!incoming) {
      document.receivedBy = QStringLiteral("Сидоренко І.М.");
      document.identityDoc = QStringLiteral("паспорт АА 123456");
    }
    CashLine row;
    row.correspondenceId = corr;
    row.amount = Money::fromCents(cents);
    row.description = text;
    const Result<std::vector<DocumentLine>> lines =
        incoming ? composeCashIn(accounts, {row}) : composeCashOut(accounts, {row});
    if (!lines) {
      return lines.asStatus();
    }
    document.lines = lines.value();
    return postNew(session, document);
  };

  auto bankMove = [&](bool incoming, QDate date, CounterpartyId party, std::int64_t cents,
                      const QString & text, AccountId corr = {}) -> Status {
    Document document = numbered(session, DocumentType::BankPayment, date);
    document.counterpartyId = party;
    document.comment = text;
    BankLine row;
    row.amount = Money::fromCents(cents);
    row.description = text;
    row.correspondenceId = corr;
    const Result<std::vector<DocumentLine>> lines = composeBank(accounts, incoming, {row});
    if (!lines) {
      return lines.asStatus();
    }
    document.lines = lines.value();
    return postNew(session, document);
  };

  status = buy(QDate(year, 1, 12), refs.mill, refs.flour, 10000, 3800 + between(rng, 0, 400));
  if (!status) {
    return status;
  }
  status = buy(QDate(year, 2, 3), refs.oilCo, refs.oil, 6000, 7000 + between(rng, 0, 500));
  if (!status) {
    return status;
  }
  status = buy(QDate(year, 3, 18), refs.mill, refs.sugar, 8000, 2600);
  if (!status) {
    return status;
  }
  status = sell(QDate(year, 1, 20), refs.buyer, refs.flour, 5000, 6200);
  if (!status) {
    return status;
  }
  status = sell(QDate(year, 2, 14), refs.shop, refs.sugar, 4000, 4500);
  if (!status) {
    return status;
  }
  status = sell(QDate(year, 4, 7), refs.buyer, refs.oil, 3000, 9800);
  if (!status) {
    return status;
  }
  status = sell(QDate(year, 5, 22), refs.shop, refs.coffee, 1000, 26000);
  if (!status) {
    return status;
  }

  status = cashMove(true, QDate(year, 1, 22), refs.buyer, customers, 1200000,
                    QStringLiteral("оплата від покупця"));
  if (!status) {
    return status;
  }
  status = cashMove(false, QDate(year, 1, 25), {}, expense, 85000, QStringLiteral("канцтовари"));
  if (!status) {
    return status;
  }
  status = cashMove(true, QDate(year, 3, 5), refs.shop, customers, 450000,
                    QStringLiteral("роздрібна виручка"));
  if (!status) {
    return status;
  }
  status =
      bankMove(true, QDate(year, 1, 28), refs.buyer, 2500000, QStringLiteral("оплата на рахунок"));
  if (!status) {
    return status;
  }
  status = bankMove(false, QDate(year, 2, 6), refs.mill, 1800000,
                    QStringLiteral("оплата постачальнику"));
  if (!status) {
    return status;
  }
  status = bankMove(true, QDate(year, 6, 11), refs.oilCo, 900000,
                    QStringLiteral("повернення передоплати"));
  if (!status) {
    return status;
  }
  status =
      bankMove(false, QDate(year, 6, 15), refs.oilCo, 500000, QStringLiteral("доплата за олію"));
  if (!status) {
    return status;
  }

  Document draft = numbered(session, DocumentType::Sale, QDate(year, 8, 10));
  draft.counterpartyId = refs.buyer;
  const Result<GoodsLine> draftRow = priced(refs.flour, 2000, 6500);
  if (!draftRow) {
    return draftRow.asStatus();
  }
  const Result<std::vector<DocumentLine>> draftLines = composeSale(accounts, {draftRow.value()});
  if (!draftLines) {
    return draftLines.asStatus();
  }
  draft.lines = draftLines.value();
  draft.comment = QStringLiteral("демо: непроведена чернетка");
  const Result<DocumentId> draftId = session.documents().insert(draft);
  if (!draftId) {
    return draftId.asStatus();
  }
  return Status::ok();
}

Status seedFixedAssets(LedgerSession & session, int year, const std::vector<Account> & accounts,
                       const DemoRefs & refs)
{
  const AccountId machinery = accountIdByCode(accounts, chart::kMachinery);
  const AccountId expense = accountIdByCode(accounts, chart::kAdminExpense);
  const AccountId suppliers = accountIdByCode(accounts, chart::kSuppliers);
  if (!machinery || !expense || !suppliers) {
    return Status::fail(Error::validation(QStringLiteral("неповний план рахунків для ОЗ")));
  }

  if (!hasType(session, DocumentType::FaCommission, year)) {
    FixedAsset lathe;
    const Result<FixedAsset> existing =
        session.fixedAssets().findByInventoryNo(QStringLiteral("ОЗ-0001"));
    if (existing) {
      lathe = existing.value();
    } else if (existing.error().code != ErrorCode::NotFound) {
      return existing.asStatus();
    } else {
      lathe.inventoryNo = QStringLiteral("ОЗ-0001");
      lathe.name = QStringLiteral("Токарний верстат");
      lathe.assetAccountId = machinery;
      lathe.expenseAccountId = expense;
      lathe.original = Money::fromCents(12000000);
      lathe.usefulLifeMonths = 60;
      const Result<FixedAssetId> id = session.fixedAssets().insert(lathe);
      if (!id) {
        return id.asStatus();
      }
      lathe.id = id.value();
    }
    const Result<FixedAsset> loaded = session.fixedAssets().findById(lathe.id);
    if (!loaded) {
      return loaded.asStatus();
    }

    if (loaded.value().status == FixedAssetStatus::Draft) {
      Document commission = numbered(session, DocumentType::FaCommission, QDate(year, 3, 1));
      commission.counterpartyId = refs.mill;
      commission.comment = QStringLiteral("демо: введення верстата в експлуатацію");
      const Result<std::vector<DocumentLine>> lines =
          composeFaCommission(accounts, suppliers, {loaded.value()});
      if (!lines) {
        return lines.asStatus();
      }
      commission.lines = lines.value();
      Status status = postNew(session, commission);
      if (!status) {
        return status;
      }
    }

    const Result<Document> amort = buildFaDepreciationDocument(session.handle(), year, 4);
    if (!amort) {
      return amort.asStatus();
    }
    Document posted = amort.value();
    if (!posted.id) {
      const Result<DocumentId> id = session.documents().insert(posted);
      if (!id) {
        return id.asStatus();
      }
      Status status = session.posting().post(id.value());
      if (!status) {
        return status;
      }
    } else {
      Status status = session.posting().post(posted.id);
      if (!status) {
        return status;
      }
    }
  }

  if (hasType(session, DocumentType::FaWriteOff, year)) {
    return Status::ok();
  }

  FixedAsset printer;
  const Result<FixedAsset> existingPrinter =
      session.fixedAssets().findByInventoryNo(QStringLiteral("ОЗ-0002"));
  if (existingPrinter) {
    printer = existingPrinter.value();
  } else if (existingPrinter.error().code != ErrorCode::NotFound) {
    return existingPrinter.asStatus();
  } else {
    printer.inventoryNo = QStringLiteral("ОЗ-0002");
    printer.name = QStringLiteral("Принтер офісний");
    printer.assetAccountId = machinery;
    printer.expenseAccountId = expense;
    printer.original = Money::fromCents(800000);
    printer.usefulLifeMonths = 24;
    const Result<FixedAssetId> id = session.fixedAssets().insert(printer);
    if (!id) {
      return id.asStatus();
    }
    printer.id = id.value();
  }
  Result<FixedAsset> loadedPrinter = session.fixedAssets().findById(printer.id);
  if (!loadedPrinter) {
    return loadedPrinter.asStatus();
  }
  if (loadedPrinter.value().status == FixedAssetStatus::Draft) {
    Document commission = numbered(session, DocumentType::FaCommission, QDate(year, 5, 1));
    commission.counterpartyId = refs.mill;
    commission.comment = QStringLiteral("демо: введення принтера");
    const Result<std::vector<DocumentLine>> lines =
        composeFaCommission(accounts, suppliers, {loadedPrinter.value()});
    if (!lines) {
      return lines.asStatus();
    }
    commission.lines = lines.value();
    Status status = postNew(session, commission);
    if (!status) {
      return status;
    }
    loadedPrinter = session.fixedAssets().findById(printer.id);
    if (!loadedPrinter) {
      return loadedPrinter.asStatus();
    }
  }
  if (loadedPrinter.value().status != FixedAssetStatus::InUse) {
    return Status::ok();
  }
  Document writeOff = numbered(session, DocumentType::FaWriteOff, QDate(year, 6, 12));
  writeOff.comment = QStringLiteral("демо: списання принтера");
  const Result<std::vector<DocumentLine>> writeLines =
      composeFaWriteOff(session.handle(), accounts, {loadedPrinter.value()}, writeOff.date);
  if (!writeLines) {
    return writeLines.asStatus();
  }
  writeOff.lines = writeLines.value();
  return postNew(session, writeOff);
}

Status seedIntangibles(LedgerSession & session, int year, const std::vector<Account> & accounts,
                       const DemoRefs & refs)
{
  const AccountId software = accountIdByCode(accounts, chart::kSoftware);
  const AccountId other = accountIdByCode(accounts, chart::kOtherIntangibles);
  const AccountId expense = accountIdByCode(accounts, chart::kAdminExpense);
  const AccountId suppliers = accountIdByCode(accounts, chart::kSuppliers);
  if (!software || !other || !expense || !suppliers) {
    return Status::fail(Error::validation(QStringLiteral("неповний план рахунків для НМА")));
  }

  if (!hasType(session, DocumentType::IaCommission, year)) {
    IntangibleAsset program;
    const Result<IntangibleAsset> existing =
        session.intangibles().findByInventoryNo(QStringLiteral("НМА-0001"));
    if (existing) {
      program = existing.value();
    } else if (existing.error().code != ErrorCode::NotFound) {
      return existing.asStatus();
    } else {
      program.inventoryNo = QStringLiteral("НМА-0001");
      program.name = QStringLiteral("Програма бухгалтерська");
      program.assetAccountId = software;
      program.expenseAccountId = expense;
      program.original = Money::fromCents(1800000);
      program.usefulLifeMonths = 36;
      const Result<IntangibleId> id = session.intangibles().insert(program);
      if (!id) {
        return id.asStatus();
      }
      program.id = id.value();
    }
    const Result<IntangibleAsset> loaded = session.intangibles().findById(program.id);
    if (!loaded) {
      return loaded.asStatus();
    }

    if (loaded.value().status == IntangibleStatus::Draft) {
      Document commission = numbered(session, DocumentType::IaCommission, QDate(year, 3, 1));
      commission.counterpartyId = refs.mill;
      commission.comment = QStringLiteral("демо: введення програми в експлуатацію");
      const Result<std::vector<DocumentLine>> lines =
          composeIaCommission(accounts, suppliers, {loaded.value()});
      if (!lines) {
        return lines.asStatus();
      }
      commission.lines = lines.value();
      Status status = postNew(session, commission);
      if (!status) {
        return status;
      }
    }

    const Result<Document> amort = buildIaDepreciationDocument(session.handle(), year, 4);
    if (!amort) {
      return amort.asStatus();
    }
    Document posted = amort.value();
    if (!posted.id) {
      const Result<DocumentId> id = session.documents().insert(posted);
      if (!id) {
        return id.asStatus();
      }
      Status status = session.posting().post(id.value());
      if (!status) {
        return status;
      }
    } else {
      Status status = session.posting().post(posted.id);
      if (!status) {
        return status;
      }
    }
  }

  if (hasType(session, DocumentType::IaWriteOff, year)) {
    return Status::ok();
  }

  IntangibleAsset license;
  const Result<IntangibleAsset> existingLicense =
      session.intangibles().findByInventoryNo(QStringLiteral("НМА-0002"));
  if (existingLicense) {
    license = existingLicense.value();
  } else if (existingLicense.error().code != ErrorCode::NotFound) {
    return existingLicense.asStatus();
  } else {
    license.inventoryNo = QStringLiteral("НМА-0002");
    license.name = QStringLiteral("Ліцензія антивірус");
    license.assetAccountId = other;
    license.expenseAccountId = expense;
    license.original = Money::fromCents(360000);
    license.usefulLifeMonths = 12;
    const Result<IntangibleId> id = session.intangibles().insert(license);
    if (!id) {
      return id.asStatus();
    }
    license.id = id.value();
  }
  Result<IntangibleAsset> loadedLicense = session.intangibles().findById(license.id);
  if (!loadedLicense) {
    return loadedLicense.asStatus();
  }
  if (loadedLicense.value().status == IntangibleStatus::Draft) {
    Document commission = numbered(session, DocumentType::IaCommission, QDate(year, 5, 1));
    commission.counterpartyId = refs.mill;
    commission.comment = QStringLiteral("демо: введення ліцензії");
    const Result<std::vector<DocumentLine>> lines =
        composeIaCommission(accounts, suppliers, {loadedLicense.value()});
    if (!lines) {
      return lines.asStatus();
    }
    commission.lines = lines.value();
    Status status = postNew(session, commission);
    if (!status) {
      return status;
    }
    loadedLicense = session.intangibles().findById(license.id);
    if (!loadedLicense) {
      return loadedLicense.asStatus();
    }
  }
  if (loadedLicense.value().status != IntangibleStatus::InUse) {
    return Status::ok();
  }
  Document writeOff = numbered(session, DocumentType::IaWriteOff, QDate(year, 6, 12));
  writeOff.comment = QStringLiteral("демо: списання ліцензії");
  const Result<std::vector<DocumentLine>> writeLines =
      composeIaWriteOff(session.handle(), accounts, {loadedLicense.value()}, writeOff.date);
  if (!writeLines) {
    return writeLines.asStatus();
  }
  writeOff.lines = writeLines.value();
  return postNew(session, writeOff);
}

Status seedLowValue(LedgerSession & session, int year, const std::vector<Account> & accounts,
                    const DemoRefs & refs)
{
  if (hasType(session, DocumentType::MsReceipt, year)) {
    return Status::ok();
  }
  const AccountId stock = accountIdByCode(accounts, chart::kLowValue);
  const AccountId suppliers = accountIdByCode(accounts, chart::kSuppliers);
  const AccountId expense = accountIdByCode(accounts, chart::kAdminExpense);
  if (!stock || !suppliers || !expense) {
    return Status::fail(Error::validation(QStringLiteral("неповний план рахунків для МШП")));
  }

  LowValueItem gloves;
  const Result<LowValueItem> existing =
      session.lowValue().findByInventoryNo(QStringLiteral("МШ-0001"));
  if (existing) {
    gloves = existing.value();
  } else if (existing.error().code != ErrorCode::NotFound) {
    return existing.asStatus();
  } else {
    gloves.inventoryNo = QStringLiteral("МШ-0001");
    gloves.name = QStringLiteral("Рукавички робочі");
    gloves.unit = QStringLiteral("шт");
    const Result<LowValueId> id = session.lowValue().insert(gloves);
    if (!id) {
      return id.asStatus();
    }
    gloves.id = id.value();
  }

  MsLine receiptRow;
  receiptRow.itemId = gloves.id;
  receiptRow.quantity = Quantity::fromMilli(10000);
  receiptRow.unitPrice = Money::fromCents(15000);
  receiptRow.amount = Money::fromCents(150000);
  receiptRow.description = QStringLiteral("демо: рукавички");
  Document receipt = numbered(session, DocumentType::MsReceipt, QDate(year, 5, 6));
  receipt.counterpartyId = refs.mill;
  receipt.comment = QStringLiteral("демо: надходження МШП від постачальника");
  const Result<std::vector<DocumentLine>> receiptLines = composeMsReceipt(accounts, {receiptRow});
  if (!receiptLines) {
    return receiptLines.asStatus();
  }
  receipt.lines = receiptLines.value();
  Status status = postNew(session, receipt);
  if (!status) {
    return status;
  }

  const Result<LowValueItem> afterReceipt = session.lowValue().findById(gloves.id);
  if (!afterReceipt) {
    return afterReceipt.asStatus();
  }
  MsLine issueRow;
  issueRow.itemId = gloves.id;
  issueRow.quantity = Quantity::fromMilli(2000);
  Document issue = numbered(session, DocumentType::MsIssue, QDate(year, 5, 12));
  issue.comment = QStringLiteral("демо: передача рукавичок в експлуатацію");
  const Result<std::vector<DocumentLine>> issueLines =
      composeMsIssue(accounts, {afterReceipt.value()}, {issueRow});
  if (!issueLines) {
    return issueLines.asStatus();
  }
  issue.lines = issueLines.value();
  return postNew(session, issue);
}

Status seedServicePurchases(LedgerSession & session, int year,
                            const std::vector<Account> & accounts, const DemoRefs & refs)
{
  if (hasType(session, DocumentType::ServicePurchase, year)) {
    return Status::ok();
  }
  const AccountId expense = accountIdByCode(accounts, chart::kAdminExpense);
  if (!expense) {
    return Status::fail(
        Error::validation(QStringLiteral("неповний план рахунків для надходження послуг")));
  }
  CashLine rent;
  rent.correspondenceId = expense;
  rent.amount = Money::fromCents(800000);
  rent.description = QStringLiteral("оренда офісу");
  Document document = numbered(session, DocumentType::ServicePurchase, QDate(year, 5, 15));
  document.counterpartyId = refs.mill;
  document.comment = QStringLiteral("демо: надходження послуг");
  const Result<std::vector<DocumentLine>> lines = composeServicePurchase(accounts, {rent});
  if (!lines) {
    return lines.asStatus();
  }
  document.lines = lines.value();
  return postNew(session, document);
}

Status seedEmployeesAndPayroll(LedgerSession & session, int year,
                               const std::vector<Account> & accounts)
{
  auto ensureEmployee = [&](const QString & taxId, const QString & name, QDate hiredOn,
                            std::int64_t salaryCents, bool disabledSsc) -> Result<EmployeeId> {
    const Result<std::vector<Employee>> all = session.employees().listAll();
    if (all) {
      for (const auto & emp : all.value()) {
        if (emp.taxId == taxId) {
          return Result<EmployeeId>::ok(emp.id);
        }
      }
    }
    Employee emp;
    emp.taxId = taxId;
    emp.name = name;
    emp.hiredOn = hiredOn;
    emp.salary = Money::fromCents(salaryCents);
    emp.disabledSsc = disabledSsc;
    emp.isActive = true;
    return session.employees().insert(emp);
  };

  const auto emp1 =
      ensureEmployee(QStringLiteral("2987654321"), QStringLiteral("Коваль Олександр Петрович"),
                     QDate(year - 1, 1, 10), 2500000, false);
  const auto emp2 =
      ensureEmployee(QStringLiteral("3123456789"), QStringLiteral("Сидоренко Ірина Михайлівна"),
                     QDate(year - 1, 2, 1), 1800000, false);
  const auto emp3 =
      ensureEmployee(QStringLiteral("2789012345"), QStringLiteral("Іваненко Іван Петрович"),
                     QDate(year, 1, 15), 2000000, false);
  const auto emp4 =
      ensureEmployee(QStringLiteral("2654321098"), QStringLiteral("Петренко Василь Сергійович"),
                     QDate(year, 2, 1), 1500000, true);

  if (!emp1 || !emp2 || !emp3 || !emp4) {
    return Status::fail(Error::database(QStringLiteral("не вдалося створити співробітників")));
  }

  if (hasType(session, DocumentType::Payroll, year)) {
    return Status::ok();
  }

  const Result<std::vector<Employee>> staff = session.employees().listActive();
  if (!staff || staff.value().empty()) {
    return Status::fail(Error::database(QStringLiteral("немає активних співробітників")));
  }

  const AccountId salaryAcc = accountIdByCode(accounts, chart::kSalaryPayable);
  const AccountId pitAcc = accountIdByCode(accounts, chart::kPitPayable);
  const AccountId militaryAcc = accountIdByCode(accounts, chart::kMilitaryLevy);
  const AccountId sscAcc = accountIdByCode(accounts, chart::kSscPayable);

  auto createPayrollMonth = [&](int month) -> Status {
    const QDate monthEnd(year, month, QDate(year, month, 1).daysInMonth());
    std::vector<PayrollLine> pLines;
    Money totalPit = Money::zero();
    Money totalMilitary = Money::zero();
    Money totalSsc = Money::zero();

    for (const Employee & emp : staff.value()) {
      if (emp.hiredOn <= monthEnd) {
        const Result<PayrollLine> pLine = payrollLineFor(emp, monthEnd);
        if (!pLine) {
          return pLine.asStatus();
        }
        pLines.push_back(pLine.value());
        const Result<Money> nextPit = totalPit.plus(pLine.value().pit);
        if (nextPit) {
          totalPit = nextPit.value();
        }
        const Result<Money> nextMilitary = totalMilitary.plus(pLine.value().military);
        if (nextMilitary) {
          totalMilitary = nextMilitary.value();
        }
        const Result<Money> nextSsc = totalSsc.plus(pLine.value().ssc);
        if (nextSsc) {
          totalSsc = nextSsc.value();
        }
      }
    }
    const Result<std::vector<DocumentLine>> docLines = composePayroll(accounts, pLines);
    if (!docLines) {
      return docLines.asStatus();
    }
    Document payroll = numbered(session, DocumentType::Payroll, monthEnd);
    payroll.comment = QStringLiteral("демо: нарахування заробітної плати за %1.%2")
                          .arg(month, 2, 10, QLatin1Char('0'))
                          .arg(year);
    payroll.lines = docLines.value();
    Status status = postNew(session, payroll);
    if (!status) {
      return status;
    }

    const Money netPayable = payrollNetPayable(payroll.lines, accounts);
    if (netPayable.isPositive() && salaryAcc) {
      const QDate payDate = monthEnd.addDays(5);
      Document payDoc = numbered(session, DocumentType::BankPayment, payDate);
      payDoc.comment = QStringLiteral("демо: виплата заробітної плати за %1.%2")
                           .arg(QString::number(month), QString::number(year));
      BankLine bLine;
      bLine.correspondenceId = salaryAcc;
      bLine.amount = netPayable;
      bLine.description = QStringLiteral("виплата заробітної плати на карткові рахунки за %1.%2")
                              .arg(QString::number(month), QString::number(year));
      const Result<std::vector<DocumentLine>> bLines = composeBank(accounts, false, {bLine});
      if (bLines) {
        payDoc.lines = bLines.value();
        {
      Status seedPost = postNew(session, payDoc);
      if (!seedPost)
        return seedPost;
    }
      }
    }

    const QDate taxPayDate = monthEnd.addDays(5);
    std::vector<BankLine> taxLines;
    if (totalPit.isPositive() && pitAcc) {
      BankLine l;
      l.correspondenceId = pitAcc;
      l.amount = totalPit;
      l.description = QStringLiteral("сплата ПДФО 18% за %1.%2")
                          .arg(QString::number(month), QString::number(year));
      taxLines.push_back(l);
    }
    if (totalMilitary.isPositive() && militaryAcc) {
      BankLine l;
      l.correspondenceId = militaryAcc;
      l.amount = totalMilitary;
      l.description = QStringLiteral("сплата військового збору 5% за %1.%2")
                          .arg(QString::number(month), QString::number(year));
      taxLines.push_back(l);
    }
    if (totalSsc.isPositive() && sscAcc) {
      BankLine l;
      l.correspondenceId = sscAcc;
      l.amount = totalSsc;
      l.description = QStringLiteral("сплата ЄСВ 22% за %1.%2")
                          .arg(QString::number(month), QString::number(year));
      taxLines.push_back(l);
    }
    if (!taxLines.empty()) {
      Document taxDoc = numbered(session, DocumentType::BankPayment, taxPayDate);
      taxDoc.comment = QStringLiteral("демо: сплата податків із заробітної плати за %1.%2")
                           .arg(QString::number(month), QString::number(year));
      const Result<std::vector<DocumentLine>> tLines = composeBank(accounts, false, taxLines);
      if (tLines) {
        taxDoc.lines = tLines.value();
        {
      Status seedPost = postNew(session, taxDoc);
      if (!seedPost)
        return seedPost;
    }
      }
    }

    return Status::ok();
  };

  Status st = createPayrollMonth(1);
  if (!st)
    return st;
  st = createPayrollMonth(2);
  if (!st)
    return st;
  return createPayrollMonth(3);
}

Status seedExpenseReports(LedgerSession & session, int year, const std::vector<Account> & accounts,
                          const DemoRefs & refs)
{
  if (hasType(session, DocumentType::ExpenseReport, year)) {
    return Status::ok();
  }
  const Result<std::vector<Employee>> staff = session.employees().listActive();
  if (!staff || staff.value().empty()) {
    return Status::ok();
  }
  const EmployeeId empId = staff.value().front().id;
  const AccountId cashAcc = accountIdByCode(accounts, chart::kCash);
  const AccountId accountableAcc = accountIdByCode(accounts, chart::kAccountablePersonsUah);
  const AccountId adminExp = accountIdByCode(accounts, chart::kAdminExpense);
  const AccountId rawMaterials = accountIdByCode(accounts, chart::kRawMaterials);

  if (!cashAcc || !accountableAcc || !adminExp) {
    return Status::ok();
  }

  // 1. Видача коштів під звіт через ВКО (2500 грн)
  CashLine advanceRow;
  advanceRow.correspondenceId = accountableAcc;
  advanceRow.amount = Money::fromCents(250000);
  advanceRow.description = QStringLiteral("демо: аванс на відрядження та канцтовари");
  Document vko = numbered(session, DocumentType::CashOut, QDate(year, 2, 10));
  vko.employeeId = empId;
  vko.comment = QStringLiteral("демо: видача коштів під звіт");
  const Result<std::vector<DocumentLine>> vkoLines = composeCashOut(accounts, {advanceRow});
  if (vkoLines) {
    vko.lines = vkoLines.value();
    {
      Status seedPost = postNew(session, vko);
      if (!seedPost)
        return seedPost;
    }
  }

  // 2. Авансовий звіт (АЗ)
  std::vector<ExpenseLine> expLines;

  // Рядок 1: Канцтовари (800 грн + 160 грн ПДВ)
  ExpenseLine el1;
  el1.documentRef = QStringLiteral("Чек № 1042 від 12.02.2026");
  el1.expenseAccountId = adminExp;
  el1.amount = Money::fromCents(80000);
  el1.vatRateBp = 2000;
  el1.vatCents = 16000;
  el1.description = QStringLiteral("Канцтовари та папір А4");
  expLines.push_back(el1);

  // Рядок 2: Матеріали/ТМЦ на склад (1000 грн + 200 грн ПДВ)
  ExpenseLine el2;
  el2.documentRef = QStringLiteral("Чек АЗС № 8841 від 14.02.2026");
  el2.expenseAccountId = rawMaterials ? rawMaterials : adminExp;
  el2.nomenclatureId = refs.sugar;
  el2.quantity = Quantity::fromMilli(20000);
  el2.unitPrice = Money::fromCents(5000);
  el2.amount = Money::fromCents(100000);
  el2.vatRateBp = 2000;
  el2.vatCents = 20000;
  el2.description = QStringLiteral("Матеріали для виробництва");
  expLines.push_back(el2);

  // Рядок 3: Добові у відрядженні (300 грн, без ПДВ)
  ExpenseLine el3;
  el3.documentRef = QStringLiteral("Наказ № 12 від 09.02.2026");
  el3.expenseAccountId = adminExp;
  el3.amount = Money::fromCents(30000);
  el3.vatRateBp = 0;
  el3.vatCents = 0;
  el3.description = QStringLiteral("Добові витрати на відрядження");
  expLines.push_back(el3);

  Document rep = numbered(session, DocumentType::ExpenseReport, QDate(year, 2, 16));
  rep.employeeId = empId;
  rep.comment = QStringLiteral("демо: звіт про відрядження та закупівлю канцтоварів");
  const Result<std::vector<DocumentLine>> repLines =
      composeExpenseReport(accounts, expLines, accountableAcc);
  if (repLines) {
    rep.lines = repLines.value();
    {
      Status seedPost = postNew(session, rep);
      if (!seedPost)
        return seedPost;
    }
  }

  // 3. Повернення невикористаного залишку авансу через ПКО (2500 - 2460 = 40 грн)
  CashLine returnRow;
  returnRow.correspondenceId = accountableAcc;
  returnRow.amount = Money::fromCents(4000);
  returnRow.description = QStringLiteral("демо: повернення залишку авансу за звітом");
  Document pko = numbered(session, DocumentType::CashIn, QDate(year, 2, 17));
  pko.employeeId = empId;
  pko.comment = QStringLiteral("демо: повернення залишку підзвітних коштів");
  const Result<std::vector<DocumentLine>> pkoLines = composeCashIn(accounts, {returnRow});
  if (pkoLines) {
    pko.lines = pkoLines.value();
    {
      Status seedPost = postNew(session, pko);
      if (!seedPost)
        return seedPost;
    }
  }

  return Status::ok();
}

Status seedTaxInvoices(LedgerSession & session, int year, const std::vector<Account> & accounts)
{
  if (hasType(session, DocumentType::TaxInvoice, year)) {
    return Status::ok();
  }

  const Result<CompanyProfile> companyRes = session.company().load();
  if (!companyRes) {
    return companyRes.asStatus();
  }
  const CompanyProfile company = companyRes.value();

  // Вихідна податкова накладна (Sale)
  const Result<std::vector<DocumentHeader>> sales =
      session.documents().list(QDate(year, 1, 1), QDate(year, 12, 31), DocumentType::Sale);
  if (sales && !sales.value().empty()) {
    for (const auto & header : sales.value()) {
      if (!header.posted)
        continue;
      const Result<Document> saleDoc = session.documents().findById(header.id);
      if (saleDoc) {
        const Result<Document> tiRes = composeTaxInvoice(
            session.handle(), accounts, saleDoc.value(), company, saleDoc.value().date);
        if (tiRes) {
          Document ti = tiRes.value();
          const Result<QString> num = session.documents().allocateNumber(DocumentType::TaxInvoice);
          if (num) {
            ti.number = num.value();
          }
          const Result<DocumentId> tiId = session.documents().insert(ti);
          if (tiId) {
            Status seedPost = session.posting().post(tiId.value());
            if (!seedPost)
              return seedPost;
            Status seedReg = session.posting().registerTaxInvoice(tiId.value(), ti.date);
            if (!seedReg)
              return seedReg;
          }
        }
        break;
      }
    }
  }

  // Вхідна податкова накладна (Purchase)
  const Result<std::vector<DocumentHeader>> purchases =
      session.documents().list(QDate(year, 1, 1), QDate(year, 12, 31), DocumentType::Purchase);
  if (purchases && !purchases.value().empty()) {
    for (const auto & header : purchases.value()) {
      if (!header.posted)
        continue;
      const Result<Document> buyDoc = session.documents().findById(header.id);
      if (buyDoc) {
        const Result<Document> tiRes = composeTaxInvoice(session.handle(), accounts, buyDoc.value(),
                                                         company, buyDoc.value().date);
        if (tiRes) {
          Document ti = tiRes.value();
          const Result<QString> num = session.documents().allocateNumber(DocumentType::TaxInvoice);
          if (num) {
            ti.number = num.value();
          }
          const Result<DocumentId> tiId = session.documents().insert(ti);
          if (tiId) {
            Status seedPost = session.posting().post(tiId.value());
            if (!seedPost)
              return seedPost;
            Status seedReg = session.posting().registerTaxInvoice(tiId.value(), ti.date);
            if (!seedReg)
              return seedReg;
          }
        }
        break;
      }
    }
  }

  return Status::ok();
}

Status seedDemoUsers(LedgerSession & session)
{
  UserRepository users(session.handle());
  const Result<std::vector<User>> list = users.listAll();
  auto hasLogin = [&](const QString & login) {
    if (!list)
      return false;
    for (const auto & u : list.value()) {
      if (u.login == login)
        return true;
    }
    return false;
  };

  if (!hasLogin(QStringLiteral("buhgalter"))) {
    User accountant;
    accountant.login = QStringLiteral("buhgalter");
    accountant.displayName = QStringLiteral("Коваль О.П. (Головний бухгалтер)");
    accountant.role = UserRole::Accountant;
    (void)users.insert(accountant);
  }

  if (!hasLogin(QStringLiteral("kasa"))) {
    User cashier;
    cashier.login = QStringLiteral("kasa");
    cashier.displayName = QStringLiteral("Сидоренко І.М. (Касир)");
    cashier.role = UserRole::Cashier;
    (void)users.insert(cashier);
  }

  if (!hasLogin(QStringLiteral("oglyad"))) {
    User viewer;
    viewer.login = QStringLiteral("oglyad");
    viewer.displayName = QStringLiteral("Аудитор (Перегляд)");
    viewer.role = UserRole::Viewer;
    (void)users.insert(viewer);
  }

  return Status::ok();
}

Status seedExtended(LedgerSession & session, int year, const std::vector<Account> & accounts,
                    const DemoRefs & refs)
{
  const AccountId cash = accountIdByCode(accounts, chart::kCash);
  const AccountId expense = accountIdByCode(accounts, chart::kAdminExpense);
  const AccountId received = accountIdByCode(accounts, chart::kAdvancesReceived);
  const AccountId issued = accountIdByCode(accounts, chart::kAdvancesIssued);
  if (!cash || !expense) {
    return Status::fail(Error::validation(QStringLiteral("неповний план рахунків")));
  }

  if (received) {
    Document advanceIn = numbered(session, DocumentType::BankPayment, QDate(year, 8, 1));
    advanceIn.counterpartyId = refs.buyer;
    advanceIn.comment = QStringLiteral("демо: передоплата від покупця");
    BankLine row;
    row.amount = Money::fromCents(200000);
    row.description = QStringLiteral("аванс");
    row.correspondenceId = received;
    const Result<std::vector<DocumentLine>> lines = composeBank(accounts, true, {row});
    if (!lines) {
      return lines.asStatus();
    }
    advanceIn.lines = lines.value();
    Status status = postNew(session, advanceIn);
    if (!status) {
      return status;
    }
    Document sale = numbered(session, DocumentType::Sale, QDate(year, 8, 4));
    sale.counterpartyId = refs.buyer;
    sale.comment = QStringLiteral("демо: реалізація в залік авансу");
    const Result<GoodsLine> rowSale = priced(refs.flour, 3000, 6500);
    if (!rowSale) {
      return rowSale.asStatus();
    }
    const Result<std::vector<DocumentLine>> saleLines = composeSale(accounts, {rowSale.value()});
    if (!saleLines) {
      return saleLines.asStatus();
    }
    sale.lines = saleLines.value();
    status = postNew(session, sale);
    if (!status) {
      return status;
    }
  }
  if (issued) {
    Document advanceOut = numbered(session, DocumentType::BankPayment, QDate(year, 8, 2));
    advanceOut.counterpartyId = refs.mill;
    advanceOut.comment = QStringLiteral("демо: аванс постачальнику");
    BankLine row;
    row.amount = Money::fromCents(150000);
    row.description = QStringLiteral("передоплата");
    row.correspondenceId = issued;
    const Result<std::vector<DocumentLine>> lines = composeBank(accounts, false, {row});
    if (!lines) {
      return lines.asStatus();
    }
    advanceOut.lines = lines.value();
    Status status = postNew(session, advanceOut);
    if (!status) {
      return status;
    }
    Document buy = numbered(session, DocumentType::Purchase, QDate(year, 8, 6));
    buy.counterpartyId = refs.mill;
    buy.comment = QStringLiteral("демо: надходження в залік авансу");
    const Result<GoodsLine> rowBuy = priced(refs.sugar, 5000, 2600);
    if (!rowBuy) {
      return rowBuy.asStatus();
    }
    const Result<std::vector<DocumentLine>> buyLines = composePurchase(accounts, {rowBuy.value()});
    if (!buyLines) {
      return buyLines.asStatus();
    }
    buy.lines = buyLines.value();
    status = postNew(session, buy);
    if (!status) {
      return status;
    }
  }

  GoodsLine consult;
  consult.nomenclatureId = refs.consult;
  consult.quantity = Quantity::fromMilli(1000);
  consult.unitPrice = Money::fromCents(1500000);
  consult.amount = Money::fromCents(1500000);
  consult.description = QStringLiteral("облік за I квартал");
  Document service = numbered(session, DocumentType::ServiceSale, QDate(year, 3, 10));
  service.counterpartyId = refs.logistic;
  service.comment = QStringLiteral("демо: реалізація послуг");
  const Result<std::vector<DocumentLine>> serviceLines = composeServiceSale(accounts, {consult});
  if (!serviceLines) {
    return serviceLines.asStatus();
  }
  service.lines = serviceLines.value();
  Status status = postNew(session, service);
  if (!status) {
    return status;
  }

  GoodsLine delivery;
  delivery.description = QStringLiteral("доставка замовлення");
  delivery.amount = Money::fromCents(320000);
  Document service2 = numbered(session, DocumentType::ServiceSale, QDate(year, 7, 3));
  service2.counterpartyId = refs.shop;
  const Result<std::vector<DocumentLine>> service2Lines = composeServiceSale(accounts, {delivery});
  if (!service2Lines) {
    return service2Lines.asStatus();
  }
  service2.lines = service2Lines.value();
  status = postNew(session, service2);
  if (!status) {
    return status;
  }

  status = seedServicePurchases(session, year, accounts, refs);
  if (!status) {
    return status;
  }

  Document invoice = numbered(session, DocumentType::CustomerInvoice, QDate(year, 8, 5));
  invoice.counterpartyId = refs.buyer;
  invoice.comment = QStringLiteral("демо: рахунок, без проведення");
  const Result<GoodsLine> invoiceRow = priced(refs.flour, 3000, 6500);
  if (!invoiceRow) {
    return invoiceRow.asStatus();
  }
  const Result<std::vector<DocumentLine>> invoiceLines =
      composeCustomerInvoice(accounts, {invoiceRow.value()});
  if (!invoiceLines) {
    return invoiceLines.asStatus();
  }
  invoice.lines = invoiceLines.value();
  const Result<DocumentId> invoiceId = session.documents().insert(invoice);
  if (!invoiceId) {
    return invoiceId.asStatus();
  }

  Document saleReturn = numbered(session, DocumentType::SaleReturn, QDate(year, 2, 16));
  saleReturn.counterpartyId = refs.buyer;
  saleReturn.comment = QStringLiteral("демо: повернення від покупця");
  const Result<GoodsLine> retFlour = priced(refs.flour, 1000, 6200);
  if (!retFlour) {
    return retFlour.asStatus();
  }
  const Result<std::vector<DocumentLine>> retSaleLines =
      composeSaleReturn(accounts, {retFlour.value()});
  if (!retSaleLines) {
    return retSaleLines.asStatus();
  }
  saleReturn.lines = retSaleLines.value();
  status = postNew(session, saleReturn);
  if (!status) {
    return status;
  }

  Document purchaseReturn = numbered(session, DocumentType::PurchaseReturn, QDate(year, 3, 25));
  purchaseReturn.counterpartyId = refs.mill;
  purchaseReturn.comment = QStringLiteral("демо: повернення постачальнику");
  GoodsLine retSugar;
  retSugar.nomenclatureId = refs.sugar;
  retSugar.quantity = Quantity::fromMilli(2000);
  const Result<std::vector<DocumentLine>> retBuyLines = composePurchaseReturn(accounts, {retSugar});
  if (!retBuyLines) {
    return retBuyLines.asStatus();
  }
  purchaseReturn.lines = retBuyLines.value();
  status = postNew(session, purchaseReturn);
  if (!status) {
    return status;
  }

  Document writeOff = numbered(session, DocumentType::WriteOff, QDate(year, 6, 20));
  writeOff.comment = QStringLiteral("демо: списання зіпсованої кави");
  GoodsLine scrap;
  scrap.nomenclatureId = refs.coffee;
  scrap.quantity = Quantity::fromMilli(500);
  const Result<std::vector<DocumentLine>> woLines = composeWriteOff(accounts, {scrap});
  if (!woLines) {
    return woLines.asStatus();
  }
  writeOff.lines = woLines.value();
  status = postNew(session, writeOff);
  if (!status) {
    return status;
  }

  const Result<std::vector<StockBalance>> onHand = session.inventory().balances();
  if (!onHand) {
    return onHand.asStatus();
  }
  GoodsLine counted;
  counted.nomenclatureId = refs.flour;
  for (const StockBalance & row : onHand.value()) {
    if (row.nomenclatureId == refs.flour) {
      counted.quantity = Quantity::fromMilli(row.quantity.milli() + 500);
      break;
    }
  }
  if (!counted.quantity.isPositive()) {
    counted.quantity = Quantity::fromMilli(500);
    counted.amount = Money::fromCents(2000);
  }
  Document stocktake = numbered(session, DocumentType::Stocktake, QDate(year, 7, 15));
  stocktake.comment = QStringLiteral("демо: надлишок борошна");
  const Result<std::vector<DocumentLine>> takeLines =
      composeStocktake(accounts, {counted}, onHand.value());
  if (!takeLines) {
    return takeLines.asStatus();
  }
  stocktake.lines = takeLines.value();
  status = postNew(session, stocktake);
  if (!status) {
    return status;
  }

  Document toCash = numbered(session, DocumentType::BankPayment, QDate(year, 7, 8));
  toCash.comment = QStringLiteral("демо: зняття на касу");
  BankLine cashLine;
  cashLine.amount = Money::fromCents(500000);
  cashLine.description = QStringLiteral("зняття готівки");
  cashLine.correspondenceId = cash;
  const Result<std::vector<DocumentLine>> toCashLines = composeBank(accounts, false, {cashLine});
  if (!toCashLines) {
    return toCashLines.asStatus();
  }
  toCash.lines = toCashLines.value();
  status = postNew(session, toCash);
  if (!status) {
    return status;
  }

  Document fee = numbered(session, DocumentType::BankPayment, QDate(year, 7, 9));
  fee.comment = QStringLiteral("демо: комісія банку");
  BankLine feeLine;
  feeLine.amount = Money::fromCents(25000);
  feeLine.description = QStringLiteral("комісія за розрахунково-касове");
  feeLine.correspondenceId = expense;
  const Result<std::vector<DocumentLine>> feeLines = composeBank(accounts, false, {feeLine});
  if (!feeLines) {
    return feeLines.asStatus();
  }
  fee.lines = feeLines.value();
  status = postNew(session, fee);
  if (!status) {
    return status;
  }
  status = seedFixedAssets(session, year, accounts, refs);
  if (!status) {
    return status;
  }
  status = seedIntangibles(session, year, accounts, refs);
  if (!status) {
    return status;
  }
  status = seedLowValue(session, year, accounts, refs);
  if (!status) {
    return status;
  }
  status = seedEmployeesAndPayroll(session, year, accounts);
  if (!status) {
    return status;
  }
  status = seedTaxInvoices(session, year, accounts);
  if (!status) {
    return status;
  }
  return Status::ok();
}

Status seedTradeOperations(LedgerSession & session, int year, const std::vector<Account> & accounts,
                           const DemoRefs & refs)
{
  WarehouseId wh1{1};
  WarehouseId wh2{1};
  const Result<std::vector<Warehouse>> whList = session.warehouses().listAll();
  if (whList) {
    for (const auto & w : whList.value()) {
      if (w.code == QStringLiteral("WH-02")) {
        wh2 = w.id;
        break;
      }
    }
  }

  ContractId buyerContract;
  const Result<std::vector<Contract>> bc = session.contracts().listByCounterparty(refs.buyer);
  if (bc && !bc.value().empty()) {
    buyerContract = bc.value()[0].id;
  }
  ContractId supplierContract;
  const Result<std::vector<Contract>> sc = session.contracts().listByCounterparty(refs.mill);
  if (sc && !sc.value().empty()) {
    supplierContract = sc.value()[0].id;
  }

  // 1. Замовлення покупця (CustomerOrder)
  if (!hasType(session, DocumentType::CustomerOrder, year)) {
    Document custOrder = numbered(session, DocumentType::CustomerOrder, QDate(year, 2, 10));
    custOrder.counterpartyId = refs.buyer;
    custOrder.contractId = buyerContract;
    custOrder.warehouseId = wh1;
    custOrder.orderStatus = QStringLiteral("confirmed");
    custOrder.comment = QStringLiteral("демо: замовлення покупця на борошно та цукор");
    const Result<GoodsLine> co1 = priced(refs.flour, 20000, 6500);
    const Result<GoodsLine> co2 = priced(refs.sugar, 10000, 4800);
    if (co1 && co2) {
      auto coLines = composeCustomerOrder(accounts, {co1.value(), co2.value()});
      if (coLines) {
        custOrder.lines = coLines.value();
        (void)session.documents().insert(custOrder);
      }
    }
  }

  // 2. Замовлення постачальнику (SupplierOrder)
  if (!hasType(session, DocumentType::SupplierOrder, year)) {
    Document supOrder = numbered(session, DocumentType::SupplierOrder, QDate(year, 2, 12));
    supOrder.counterpartyId = refs.mill;
    supOrder.contractId = supplierContract;
    supOrder.warehouseId = wh1;
    supOrder.orderStatus = QStringLiteral("confirmed");
    supOrder.comment = QStringLiteral("демо: замовлення борошна у постачальника");
    const Result<GoodsLine> so1 = priced(refs.flour, 50000, 3200);
    if (so1) {
      auto soLines = composeSupplierOrder(accounts, {so1.value()});
      if (soLines) {
        supOrder.lines = soLines.value();
        (void)session.documents().insert(supOrder);
      }
    }
  }

  // 3. Переміщення між складами (StockTransfer)
  if (!hasType(session, DocumentType::StockTransfer, year) && wh2.value > 0 && wh2 != wh1) {
    Document transfer = numbered(session, DocumentType::StockTransfer, QDate(year, 3, 1));
    transfer.warehouseId = wh1;
    transfer.targetWarehouseId = wh2;
    transfer.comment = QStringLiteral("демо: переміщення на Склад № 2 (Роздрібний)");
    GoodsLine tr1;
    tr1.nomenclatureId = refs.flour;
    tr1.quantity = Quantity::fromMilli(2000); // 2 кг — лишаємо запас для виробництва
    GoodsLine tr2;
    tr2.nomenclatureId = refs.sugar;
    tr2.quantity = Quantity::fromMilli(2000); // 2 кг
    auto trLines = composeStockTransfer(accounts, {tr1, tr2});
    if (trLines) {
      transfer.lines = trLines.value();
      {
      Status seedPost = postNew(session, transfer);
      if (!seedPost)
        return seedPost;
    }
    }
  }

  // 4. Валютні операції (НП(С)БО 21)
  const AccountId bank312 = accountIdByCode(accounts, chart::kForeignBank);
  const AccountId transit = accountIdByCode(accounts, chart::kForeignTransit);
  const AccountId supp632 = accountIdByCode(accounts, chart::kForeignSuppliers);
  const AccountId admin92 = accountIdByCode(accounts, chart::kAdminExpense);

  if (bank312 && transit && supp632 && admin92) {
    const Result<std::vector<DocumentHeader>> bankDocs =
        session.documents().list(QDate(year, 2, 1), QDate(year, 2, 28), DocumentType::BankPayment);
    bool hasUsdBank = false;
    if (bankDocs) {
      for (const auto & h : bankDocs.value()) {
        if (h.comment.contains(QStringLiteral("USD"))) {
          hasUsdBank = true;
          break;
        }
      }
    }
    if (!hasUsdBank) {
      // Зарахування купленої валюти 2 000.00 USD на рахунок 312 через 334
      Document bankUsd = numbered(session, DocumentType::BankPayment, QDate(year, 2, 1));
      bankUsd.currencyCode = QStringLiteral("USD");
      bankUsd.currencyRateMicros = 41'500'000;
      bankUsd.currencyNominal = 1;
      bankUsd.comment =
          QStringLiteral("демо: зарахування купленої валюти 2 000.00 USD на рахунок 312");
      DocumentLine bLine;
      bLine.debitAccountId = bank312;
      bLine.creditAccountId = transit;
      bLine.amount = Money::fromCents(200000); // 2000 USD
      bLine.description = QStringLiteral("Купівля іноземної валюти");
      bankUsd.lines.push_back(bLine);
      {
      Status seedPost = postNew(session, bankUsd);
      if (!seedPost)
        return seedPost;
    }

      // Акт отриманих послуг від нерезидента на $500.00
      Document spDoc = numbered(session, DocumentType::ServicePurchase, QDate(year, 2, 15));
      spDoc.counterpartyId = refs.foreignSupplier;
      spDoc.currencyCode = QStringLiteral("USD");
      spDoc.currencyRateMicros = 41'750'000;
      spDoc.currencyNominal = 1;
      spDoc.comment = QStringLiteral("демо: хмарні сервери AWS (Global Cloud Services Inc.)");
      DocumentLine spLine;
      spLine.debitAccountId = admin92;
      spLine.creditAccountId = supp632;
      spLine.amount = Money::fromCents(50000); // 500 USD
      spLine.description = QStringLiteral("Послуги хмарного хостингу за лютий");
      spDoc.lines.push_back(spLine);
      {
      Status seedPost = postNew(session, spDoc);
      if (!seedPost)
        return seedPost;
    }

      // Оплата нерезиденту з валютного рахунку 312 ($500.00)
      Document payDoc = numbered(session, DocumentType::BankPayment, QDate(year, 2, 20));
      payDoc.counterpartyId = refs.foreignSupplier;
      payDoc.currencyCode = QStringLiteral("USD");
      payDoc.currencyRateMicros = 41'750'000;
      payDoc.currencyNominal = 1;
      payDoc.comment = QStringLiteral("демо: оплата послуг постачальника з рахунку 312");
      DocumentLine payLine;
      payLine.debitAccountId = supp632;
      payLine.creditAccountId = bank312;
      payLine.amount = Money::fromCents(50000); // 500 USD
      payLine.description =
          QStringLiteral("Оплата послуг постачальника Global Cloud Services Inc.");
      payDoc.lines.push_back(payLine);
      {
      Status seedPost = postNew(session, payDoc);
      if (!seedPost)
        return seedPost;
    }
    }
  }

  return Status::ok();
}

Status seedProduction(LedgerSession & session, int year, const std::vector<Account> & accounts,
                      const DemoRefs & refs)
{
  (void)refs;
  const AccountId supp631 = accountIdByCode(accounts, chart::kSuppliers);
  const AccountId prod231 = accountIdByCode(accounts, chart::kProduction);
  const AccountId mat201 = accountIdByCode(accounts, chart::kRawMaterials);
  const AccountId goods26 = accountIdByCode(accounts, chart::kFinishedGoods);
  const AccountId cust361 = accountIdByCode(accounts, chart::kCustomers);
  const AccountId rev701 = accountIdByCode(accounts, chart::kRevenue);
  const AccountId cogs901 = accountIdByCode(accounts, chart::kProductionCogs);
  const AccountId ovhd91 = accountIdByCode(accounts, chart::kManufacturingOverhead);
  if (!supp631 || !prod231 || !mat201 || !goods26 || !cust361 || !rev701 || !cogs901 || !ovhd91) {
    return Status::fail(
        Error::validation(QStringLiteral("у плані рахунків немає виробничих рахунків")));
  }

  // 1. Постачальник сировини та матеріалів
  const Result<CounterpartyId> furnSupplier =
      ensureParty(session, QStringLiteral("ТОВ «Меблева фурнітура та плити»"), false, true,
                  QStringLiteral("55443322"), QString(), QStringLiteral("ПриватБанк"));
  if (!furnSupplier) {
    return furnSupplier.asStatus();
  }

  // 2. Номенклатура: матеріали (201) та готова продукція (26)
  const Result<NomenclatureId> dsp = ensureItem(session, QStringLiteral("ДСП ламіноване 16мм Бук"),
                                                QStringLiteral("МАТ-ДСП16"), QStringLiteral("м2"));
  const Result<NomenclatureId> edge = ensureItem(session, QStringLiteral("Кромка ПВХ 2мм"),
                                                 QStringLiteral("МАТ-КР2"), QStringLiteral("м"));
  const Result<NomenclatureId> screws =
      ensureItem(session, QStringLiteral("Конфірмати меблеві 7х50"), QStringLiteral("МАТ-КНФ"),
                 QStringLiteral("упак"));
  const Result<NomenclatureId> table = ensureItem(session, QStringLiteral("Стіл офісний «Комфорт»"),
                                                  QStringLiteral("ПРД-СТ1"), QStringLiteral("шт"));
  const Result<NomenclatureId> wardrobe =
      ensureItem(session, QStringLiteral("Шафа для документів «Практик»"),
                 QStringLiteral("ПРД-ШФ1"), QStringLiteral("шт"));

  if (!dsp || !edge || !screws || !table || !wardrobe) {
    return Status::fail(
        Error::validation(QStringLiteral("не вдалося створити виробничу номенклатуру")));
  }

  // 3. Специфікації виробів (BOM)
  const Result<std::vector<BillOfMaterials>> existingBoms =
      session.bom().listForProduct(table.value());
  BomId tableBomId;
  if (existingBoms && !existingBoms.value().empty()) {
    tableBomId = existingBoms.value().front().id;
  } else {
    BillOfMaterials bom;
    bom.name = QStringLiteral("Стіл офісний «Комфорт» (стандартна специфікація)");
    bom.productId = table.value();
    bom.outputQuantity = Quantity::fromMilli(1000); // 1 шт
    bom.isDefault = true;
    bom.lines = {
        {1, dsp.value(), Quantity::fromMilli(2500), QStringLiteral("2.5 м2 ДСП на 1 стіл")},
        {2, edge.value(), Quantity::fromMilli(5000), QStringLiteral("5 м кромки на 1 стіл")},
        {3, screws.value(), Quantity::fromMilli(1000), QStringLiteral("1 упак конфірматів")}};
    const Result<BomId> insertedBom = session.bom().insert(bom);
    if (!insertedBom) {
      return insertedBom.asStatus();
    }
    tableBomId = insertedBom.value();
  }

  // 4. Закупівля матеріалів від постачальника (Дт 201 Кт 631)
  Document docP = numbered(session, DocumentType::Purchase, QDate(year, 4, 2));
  docP.counterpartyId = furnSupplier.value();
  docP.comment = QStringLiteral("демо: надходження матеріалів для виробництва меблів");
  DocumentLine l1;
  l1.debitAccountId = mat201;
  l1.creditAccountId = supp631;
  l1.nomenclatureId = dsp.value();
  l1.quantity = Quantity::fromMilli(50000); // 50 м2
  l1.unitPrice = Money::fromCents(25000);   // 250.00 грн/м2
  l1.amount = Money::fromCents(1250000);    // 12 500.00 грн
  l1.description = QStringLiteral("ДСП 16мм 50 м2");

  DocumentLine l2;
  l2.debitAccountId = mat201;
  l2.creditAccountId = supp631;
  l2.nomenclatureId = edge.value();
  l2.quantity = Quantity::fromMilli(200000); // 200 м
  l2.unitPrice = Money::fromCents(1000);     // 10.00 грн/м
  l2.amount = Money::fromCents(200000);      // 2 000.00 грн
  l2.description = QStringLiteral("Кромка ПВХ 200 м");

  DocumentLine l3;
  l3.debitAccountId = mat201;
  l3.creditAccountId = supp631;
  l3.nomenclatureId = screws.value();
  l3.quantity = Quantity::fromMilli(30000); // 30 упак
  l3.unitPrice = Money::fromCents(5000);    // 50.00 грн/упак
  l3.amount = Money::fromCents(150000);     // 1 500.00 грн
  l3.description = QStringLiteral("Конфірмати 30 упак");

  docP.lines = {l1, l2, l3};
  {
      Status seedPost = postNew(session, docP);
      if (!seedPost)
        return seedPost;
    }

  // 5. Вимога-накладна (списання матеріалів у цех: Дт 231 Кт 201)
  Document docIssue = numbered(session, DocumentType::MaterialIssue, QDate(year, 4, 10));
  docIssue.comment = QStringLiteral("демо: відпуск матеріалів у столярний цех за специфікацією");
  DocumentLine i1;
  i1.debitAccountId = prod231;
  i1.creditAccountId = mat201;
  i1.nomenclatureId = dsp.value();
  i1.quantity = Quantity::fromMilli(25000); // 25 м2
  i1.amount = Money::fromCents(100);
  i1.description = QStringLiteral("Списання ДСП у виробництво");

  DocumentLine i2;
  i2.debitAccountId = prod231;
  i2.creditAccountId = mat201;
  i2.nomenclatureId = edge.value();
  i2.quantity = Quantity::fromMilli(50000); // 50 м
  i2.amount = Money::fromCents(100);
  i2.description = QStringLiteral("Списання кромки у виробництво");

  DocumentLine i3;
  i3.debitAccountId = prod231;
  i3.creditAccountId = mat201;
  i3.nomenclatureId = screws.value();
  i3.quantity = Quantity::fromMilli(10000); // 10 упак
  i3.amount = Money::fromCents(100);
  i3.description = QStringLiteral("Списання кріплення у виробництво");

  docIssue.lines = {i1, i2, i3};
  {
      Status seedPost = postNew(session, docIssue);
      if (!seedPost)
        return seedPost;
    }

  // 6. Загальновиробничі витрати: послуги з електропостачання цеху (Дт 91 Кт 631)
  Document docOvhd = numbered(session, DocumentType::ServicePurchase, QDate(year, 4, 12));
  docOvhd.counterpartyId = furnSupplier.value();
  docOvhd.comment = QStringLiteral("демо: електропостачання столярного цеху");
  DocumentLine ovhdLine;
  ovhdLine.debitAccountId = ovhd91;
  ovhdLine.creditAccountId = supp631;
  ovhdLine.amount = Money::fromCents(300000); // 3 000.00 грн
  ovhdLine.description = QStringLiteral("Електроенергія виробничого обладнання цеху");
  docOvhd.lines = {ovhdLine};
  {
      Status seedPost = postNew(session, docOvhd);
      if (!seedPost)
        return seedPost;
    }

  // 7. Звіт виробництва за зміну: випуск готової продукції (Дт 26 Кт 231)
  Document docProd = numbered(session, DocumentType::ProductionReport, QDate(year, 4, 15));
  docProd.bomId = tableBomId;
  docProd.comment = QStringLiteral("демо: випуск 10 офісних столів «Комфорт»");
  DocumentLine prLine;
  prLine.debitAccountId = goods26;
  prLine.creditAccountId = prod231;
  prLine.nomenclatureId = table.value();
  prLine.quantity = Quantity::fromMilli(10000); // 10 шт
  prLine.unitPrice = Money::fromCents(80000); // 800.00 грн планова собівартість
  prLine.amount = Money::fromCents(800000);   // 8 000.00 грн
  prLine.description = QStringLiteral("Випуск столів «Комфорт» на склад готової продукції");
  docProd.lines = {prLine};
  {
      Status seedPost = postNew(session, docProd);
      if (!seedPost)
        return seedPost;
    }

  // 8. Реалізація готової продукції покупцю (Дт 361 Кт 701, Дт 901 Кт 26)
  const Result<Counterparty> buyerParty =
      session.counterparties().findByEdrpou(QStringLiteral("11111111"));
  if (buyerParty) {
    Document docSale = numbered(session, DocumentType::Sale, QDate(year, 4, 20));
    docSale.counterpartyId = buyerParty.value().id;
    docSale.comment = QStringLiteral("демо: реалізація 5 столів офісних «Комфорт» покупцю");
    DocumentLine sLine1;
    sLine1.debitAccountId = cust361;
    sLine1.creditAccountId = rev701;
    sLine1.amount = Money::fromCents(750000); // 7 500.00 грн виручка (по 1500 грн/шт)
    sLine1.description = QStringLiteral("Реалізація готової продукції (столи)");

    DocumentLine sLine2;
    sLine2.debitAccountId = cogs901;
    sLine2.creditAccountId = goods26;
    sLine2.nomenclatureId = table.value();
    sLine2.quantity = Quantity::fromMilli(5000); // 5 шт
    sLine2.amount = Money::fromCents(
        100); // Собівартість розрахується при проведенні (5 шт * 800.00 = 4000.00 грн)
    sLine2.description = QStringLiteral("Списання собівартості реалізованої продукції");
    docSale.lines = {sLine1, sLine2};
    {
      Status seedPost = postNew(session, docSale);
      if (!seedPost)
        return seedPost;
    }
  }

  return Status::ok();
}

} // namespace

Status seedDemoData(LedgerSession & session, std::uint32_t seed)
{
  Status access = Access::requireSeedDemo(session.handle());
  if (!access) {
    return access;
  }
  const Result<int> existing = session.documents().count();
  if (!existing) {
    return existing.asStatus();
  }
  const int year = QDate::currentDate().year();
  const Result<CompanyProfile> company = session.company().load();
  const bool demoCompany = company && company.value().name == kDemoCompanyName;

  if (existing.value() > 0) {
    if (!demoCompany) {
      return Status::fail(
          Error::conflict(QStringLiteral("демо-дані можна засіяти лише в порожню базу")));
    }
    const Result<std::vector<Employee>> empList = session.employees().listActive();
    const bool hasEmployees = empList && !empList.value().empty();
    const bool hasPayroll = hasType(session, DocumentType::Payroll, year);
    const bool hasTaxInvoices = hasType(session, DocumentType::TaxInvoice, year);
    const bool hasMs = hasType(session, DocumentType::MsReceipt, year);
    const bool hasIa = hasType(session, DocumentType::IaCommission, year);
    const bool hasSp = hasType(session, DocumentType::ServicePurchase, year);
    const bool hasFa = hasType(session, DocumentType::FaCommission, year);
    const bool hasTransfer = hasType(session, DocumentType::StockTransfer, year);
    const bool hasCustOrder = hasType(session, DocumentType::CustomerOrder, year);
    const bool hasSupOrder = hasType(session, DocumentType::SupplierOrder, year);
    const bool hasProduction = hasType(session, DocumentType::ProductionReport, year);
    const bool hasExpenseReport = hasType(session, DocumentType::ExpenseReport, year);

    if (hasMs && hasIa && hasSp && hasFa && hasPayroll && hasTaxInvoices && hasEmployees &&
        hasTransfer && hasCustOrder && hasSupOrder && hasProduction && hasExpenseReport) {
      return Status::fail(
          Error::conflict(QStringLiteral("демо-дані вже містять повний набір документів")));
    }
    const Result<Counterparty> buyer =
        session.counterparties().findByEdrpou(QStringLiteral("11111111"));
    if (!buyer) {
      return Status::fail(
          Error::conflict(QStringLiteral("демо-дані можна засіяти лише в порожню базу")));
    }
    Status status = saveDemoCompany(session);
    if (!status) {
      return status;
    }
    const Result<DemoRefs> refs = ensureDirectories(session);
    if (!refs) {
      return refs.asStatus();
    }
    const Result<std::vector<Account>> accounts = session.accounts().listPostable();
    if (!accounts) {
      return accounts.asStatus();
    }
    if (!hasSp) {
      Status extra = seedServicePurchases(session, year, accounts.value(), refs.value());
      if (!extra) {
        return extra;
      }
    }
    if (!hasFa) {
      Status extra = seedFixedAssets(session, year, accounts.value(), refs.value());
      if (!extra) {
        return extra;
      }
    }
    if (!hasIa) {
      Status extra = seedIntangibles(session, year, accounts.value(), refs.value());
      if (!extra) {
        return extra;
      }
    }
    if (!hasMs) {
      Status extra = seedLowValue(session, year, accounts.value(), refs.value());
      if (!extra) {
        return extra;
      }
    }
    if (!hasEmployees || !hasPayroll) {
      Status extra = seedEmployeesAndPayroll(session, year, accounts.value());
      if (!extra) {
        return extra;
      }
    }
    if (!hasTaxInvoices) {
      Status extra = seedTaxInvoices(session, year, accounts.value());
      if (!extra) {
        return extra;
      }
    }
    if (!hasTransfer || !hasCustOrder || !hasSupOrder) {
      Status extra = seedTradeOperations(session, year, accounts.value(), refs.value());
      if (!extra) {
        return extra;
      }
    }
    if (!hasProduction) {
      Status extra = seedProduction(session, year, accounts.value(), refs.value());
      if (!extra) {
        return extra;
      }
    }
    if (!hasExpenseReport) {
      Status extra = seedExpenseReports(session, year, accounts.value(), refs.value());
      if (!extra) {
        return extra;
      }
    }
    (void)seedDemoUsers(session);

    if (!hasType(session, DocumentType::ServiceSale, year)) {
      return seedExtended(session, year, accounts.value(), refs.value());
    }
    return Status::ok();
  }

  std::mt19937 rng(seed);
  Status status = saveDemoCompany(session);
  if (!status) {
    return status;
  }
  const Result<DemoRefs> refs = ensureDirectories(session);
  if (!refs) {
    return refs.asStatus();
  }
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  if (!accounts) {
    return accounts.asStatus();
  }
  status = seedCore(session, rng, year, accounts.value(), refs.value());
  if (!status) {
    return status;
  }
  status = seedExtended(session, year, accounts.value(), refs.value());
  if (!status) {
    return status;
  }
  (void)seedDemoUsers(session);
  status = seedTradeOperations(session, year, accounts.value(), refs.value());
  if (!status) {
    return status;
  }
  status = seedProduction(session, year, accounts.value(), refs.value());
  if (!status) {
    return status;
  }
  return seedExpenseReports(session, year, accounts.value(), refs.value());
}

} // namespace fin
