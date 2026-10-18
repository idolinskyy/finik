#include <catch2/catch_test_macros.hpp>

#include "fin/core/Money.h"
#include "fin/core/Quantity.h"
#include "fin/db/ContractRepository.h"
#include "fin/db/WarehouseRepository.h"
#include "fin/domain/Contract.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/Nomenclature.h"
#include "fin/domain/Warehouse.h"
#include "fin/reports/Reports.h"
#include "fin/session/LedgerSession.h"

using namespace fin;

TEST_CASE("Warehouse repository and directory operations", "[trade][warehouse]")
{
  auto sessionRes = LedgerSession::openMemory();
  REQUIRE(sessionRes);
  auto & session = sessionRes.value();

  // Перевірка початкового складу (id = 1, 'MAIN')
  auto mainWh = session.warehouses().findById(WarehouseId{1});
  REQUIRE(mainWh);
  CHECK(mainWh.value().code == QStringLiteral("MAIN"));
  CHECK(mainWh.value().name == QStringLiteral("Основний склад"));

  // Створення другого складу
  Warehouse wh2;
  wh2.code = QStringLiteral("RETAIL");
  wh2.name = QStringLiteral("Роздрібний магазин");
  wh2.address = QStringLiteral("м. Львів, вул. Торгова, 5");
  auto inserted = session.warehouses().insert(wh2);
  REQUIRE(inserted);
  const WarehouseId wh2Id = inserted.value();

  auto loadedWh2 = session.warehouses().findById(wh2Id);
  REQUIRE(loadedWh2);
  CHECK(loadedWh2.value().code == QStringLiteral("RETAIL"));
  CHECK(loadedWh2.value().name == QStringLiteral("Роздрібний магазин"));

  // Пошук за кодом
  auto byCode = session.warehouses().findByCode(QStringLiteral("RETAIL"));
  REQUIRE(byCode);
  CHECK(byCode.value().id == wh2Id);

  // Список усіх складів
  auto allWh = session.warehouses().listAll();
  REQUIRE(allWh);
  CHECK(allWh.value().size() >= 2);

  // Заборона видалення основного складу
  auto delMain = session.warehouses().remove(WarehouseId{1});
  CHECK_FALSE(delMain);
}

TEST_CASE("Contract repository and counterparty contracts", "[trade][contract]")
{
  auto sessionRes = LedgerSession::openMemory();
  REQUIRE(sessionRes);
  auto & session = sessionRes.value();

  // Створюємо контрагента
  Counterparty cp;
  cp.name = QStringLiteral("ТОВ «Агропостач»");
  cp.edrpou = QStringLiteral("12345678");
  cp.isSupplier = true;
  cp.isCustomer = true;
  auto cpIdRes = session.counterparties().insert(cp);
  REQUIRE(cpIdRes);
  const CounterpartyId cpId = cpIdRes.value();

  // Створюємо договір з покупцем
  Contract c1;
  c1.counterpartyId = cpId;
  c1.number = QStringLiteral("101/2026");
  c1.name = QStringLiteral("Договір поставки зернових");
  c1.date = QDate(2026, 1, 15);
  c1.contractType = ContractType::WithCustomer;
  auto c1IdRes = session.contracts().insert(c1);
  REQUIRE(c1IdRes);
  const ContractId c1Id = c1IdRes.value();

  // Створюємо договір з постачальником
  Contract c2;
  c2.counterpartyId = cpId;
  c2.number = QStringLiteral("202/2026");
  c2.name = QStringLiteral("Договір закупівлі добрив");
  c2.date = QDate(2026, 2, 1);
  c2.contractType = ContractType::WithSupplier;
  auto c2IdRes = session.contracts().insert(c2);
  REQUIRE(c2IdRes);

  // Вибірка за контрагентом
  auto cpContracts = session.contracts().listByCounterparty(cpId);
  REQUIRE(cpContracts);
  CHECK(cpContracts.value().size() == 2);

  auto loadedC1 = session.contracts().findById(c1Id);
  REQUIRE(loadedC1);
  CHECK(loadedC1.value().number == QStringLiteral("101/2026"));
  CHECK(loadedC1.value().contractType == ContractType::WithCustomer);
}

TEST_CASE("Multi-warehouse movements and StockTransfer average costing", "[trade][stock_transfer]")
{
  auto sessionRes = LedgerSession::openMemory();
  REQUIRE(sessionRes);
  auto & session = sessionRes.value();

  auto chart = session.accounts().listPostable();
  REQUIRE(chart);

  // Створюємо номенклатуру
  Nomenclature item;
  item.name = QStringLiteral("Ноутбук Dell");
  item.sku = QStringLiteral("DELL-15");
  item.unit = QStringLiteral("шт");
  auto nomIdRes = session.nomenclature().insert(item);
  REQUIRE(nomIdRes);
  const NomenclatureId nomId = nomIdRes.value();

  // Створюємо контрагента
  Counterparty cp;
  cp.name = QStringLiteral("ТОВ «Дістрібюція»");
  cp.edrpou = QStringLiteral("87654321");
  cp.isSupplier = true;
  auto cpIdRes = session.counterparties().insert(cp);
  REQUIRE(cpIdRes);
  const CounterpartyId cpId = cpIdRes.value();

  // Створюємо другий склад
  Warehouse wh2;
  wh2.code = QStringLiteral("WEST");
  wh2.name = QStringLiteral("Західний філіал");
  auto wh2IdRes = session.warehouses().insert(wh2);
  REQUIRE(wh2IdRes);
  const WarehouseId wh2Id = wh2IdRes.value();

  const WarehouseId mainWhId = WarehouseId{1};

  // 1. Оприбуткування 10 шт по 15 000.00 грн на Основний склад (Purchase)
  Document purchaseDoc;
  purchaseDoc.type = DocumentType::Purchase;
  purchaseDoc.number = QStringLiteral("НП-001");
  purchaseDoc.date = QDate(2026, 3, 1);
  purchaseDoc.counterpartyId = cpId;
  purchaseDoc.warehouseId = mainWhId;

  GoodsLine pRow;
  pRow.nomenclatureId = nomId;
  pRow.quantity = Quantity::fromMilli(10000); // 10 шт
  pRow.unitPrice = Money::fromCents(1500000); // 15000 грн
  pRow.amount = Money::fromCents(15000000);   // 150000 грн
  auto pLines = composePurchase(chart.value(), {pRow});
  REQUIRE(pLines);
  purchaseDoc.lines = pLines.value();

  auto pDocIdRes = session.documents().insert(purchaseDoc);
  REQUIRE(pDocIdRes);
  auto postPurchase = session.posting().post(pDocIdRes.value());
  REQUIRE(postPurchase);

  // Перевіряємо залишки на складах:
  // На Основному складі має бути 10 шт на суму 150 000.00 грн
  auto stockMain = session.stock().build(QDate(2026, 3, 1), QDate(2026, 3, 31), mainWhId);
  REQUIRE(stockMain);
  REQUIRE(stockMain.value().rows.size() == 1);
  CHECK(stockMain.value().rows[0].closingQty == Quantity::fromMilli(10000));
  CHECK(stockMain.value().rows[0].closingAmount == Money::fromCents(15000000));

  // На Західному складі має бути 0
  auto stockWest = session.stock().build(QDate(2026, 3, 1), QDate(2026, 3, 31), wh2Id);
  REQUIRE(stockWest);
  CHECK(stockWest.value().rows.empty());

  // 2. Переміщення 4 шт з Основного складу на Західний склад (StockTransfer)
  Document transferDoc;
  transferDoc.type = DocumentType::StockTransfer;
  transferDoc.number = QStringLiteral("ПМ-001");
  transferDoc.date = QDate(2026, 3, 5);
  transferDoc.warehouseId = mainWhId;
  transferDoc.targetWarehouseId = wh2Id;

  GoodsLine tRow;
  tRow.nomenclatureId = nomId;
  tRow.quantity = Quantity::fromMilli(4000); // 4 шт
  auto tLines = composeStockTransfer(chart.value(), {tRow});
  REQUIRE(tLines);
  transferDoc.lines = tLines.value();

  auto tDocIdRes = session.documents().insert(transferDoc);
  REQUIRE(tDocIdRes);
  auto postTransfer = session.posting().post(tDocIdRes.value());
  REQUIRE(postTransfer);

  // Перевіряємо залишки після переміщення:
  // На Основному складі: 10 - 4 = 6 шт (вартість 6 * 15000 = 90 000.00 грн)
  auto stockMainAfter = session.stock().build(QDate(2026, 3, 1), QDate(2026, 3, 31), mainWhId);
  REQUIRE(stockMainAfter);
  REQUIRE(stockMainAfter.value().rows.size() == 1);
  CHECK(stockMainAfter.value().rows[0].closingQty == Quantity::fromMilli(6000));
  CHECK(stockMainAfter.value().rows[0].closingAmount == Money::fromCents(9000000));

  // На Західному складі: 4 шт (вартість 4 * 15000 = 60 000.00 грн)
  auto stockWestAfter = session.stock().build(QDate(2026, 3, 1), QDate(2026, 3, 31), wh2Id);
  REQUIRE(stockWestAfter);
  REQUIRE(stockWestAfter.value().rows.size() == 1);
  CHECK(stockWestAfter.value().rows[0].closingQty == Quantity::fromMilli(4000));
  CHECK(stockWestAfter.value().rows[0].closingAmount == Money::fromCents(6000000));

  // Загальний складський звіт (усі склади): 10 шт, 150 000.00 грн
  auto stockTotal = session.stock().build(QDate(2026, 3, 1), QDate(2026, 3, 31));
  REQUIRE(stockTotal);
  REQUIRE(stockTotal.value().rows.size() == 1);
  CHECK(stockTotal.value().rows[0].closingQty == Quantity::fromMilli(10000));
  CHECK(stockTotal.value().rows[0].closingAmount == Money::fromCents(15000000));

  // 3. Спроба списати з Західного складу 5 шт (коли є лише 4) -> помилка
  Document saleDoc;
  saleDoc.type = DocumentType::Sale;
  saleDoc.number = QStringLiteral("РТ-001");
  saleDoc.date = QDate(2026, 3, 10);
  saleDoc.counterpartyId = cpId;
  saleDoc.warehouseId = wh2Id;

  GoodsLine sRow;
  sRow.nomenclatureId = nomId;
  sRow.quantity = Quantity::fromMilli(5000);  // 5 шт
  sRow.unitPrice = Money::fromCents(2000000); // 20 000 грн
  sRow.amount = Money::fromCents(10000000);
  auto sLines = composeSale(chart.value(), {sRow});
  REQUIRE(sLines);
  saleDoc.lines = sLines.value();

  auto sDocIdRes = session.documents().insert(saleDoc);
  REQUIRE(sDocIdRes);
  saleDoc.id = sDocIdRes.value();
  auto postSaleFail = session.posting().post(sDocIdRes.value());
  CHECK_FALSE(postSaleFail); // Має бути помилка недостачі товару на Західному складі

  // 4. Продаж коректної кількості (3 шт) з Західного складу
  saleDoc.lines[0].quantity = Quantity::fromMilli(3000);
  saleDoc.lines[0].amount = Money::fromCents(6000000);
  saleDoc.lines[1].quantity = Quantity::fromMilli(3000);
  saleDoc.lines[1].amount = Money::fromCents(4500000);
  REQUIRE(session.documents().update(saleDoc));
  auto postSaleOk = session.posting().post(sDocIdRes.value());
  REQUIRE(postSaleOk);

  // На Західному складі лишилося 4 - 3 = 1 шт (15 000.00 грн)
  auto stockWestFinal = session.stock().build(QDate(2026, 3, 1), QDate(2026, 3, 31), wh2Id);
  REQUIRE(stockWestFinal);
  REQUIRE(stockWestFinal.value().rows.size() == 1);
  CHECK(stockWestFinal.value().rows[0].closingQty == Quantity::fromMilli(1000));
  CHECK(stockWestFinal.value().rows[0].closingAmount == Money::fromCents(1500000));
}

TEST_CASE("Customer and Supplier Orders with reservations and basedOn", "[trade][orders]")
{
  auto sessionRes = LedgerSession::openMemory();
  REQUIRE(sessionRes);
  auto & session = sessionRes.value();

  auto chart = session.accounts().listPostable();
  REQUIRE(chart);

  // Створюємо номенклатуру
  Nomenclature item;
  item.name = QStringLiteral("Монітор 27\"");
  item.sku = QStringLiteral("MON-27");
  item.unit = QStringLiteral("шт");
  auto nomIdRes = session.nomenclature().insert(item);
  REQUIRE(nomIdRes);
  const NomenclatureId nomId = nomIdRes.value();

  // Створюємо контрагента
  Counterparty cp;
  cp.name = QStringLiteral("ТОВ «Клієнт Трейд»");
  cp.edrpou = QStringLiteral("99887766");
  cp.isCustomer = true;
  auto cpIdRes = session.counterparties().insert(cp);
  REQUIRE(cpIdRes);
  const CounterpartyId cpId = cpIdRes.value();

  // Створюємо договір
  Contract c;
  c.counterpartyId = cpId;
  c.number = QStringLiteral("Д-44");
  c.date = QDate(2026, 3, 1);
  c.contractType = ContractType::WithCustomer;
  auto cIdRes = session.contracts().insert(c);
  REQUIRE(cIdRes);
  const ContractId contractId = cIdRes.value();

  // 1. Створюємо Замовлення покупця на 5 шт
  Document custOrder;
  custOrder.type = DocumentType::CustomerOrder;
  custOrder.number = QStringLiteral("ЗМ-001");
  custOrder.date = QDate(2026, 3, 2);
  custOrder.counterpartyId = cpId;
  custOrder.contractId = contractId;
  custOrder.warehouseId = WarehouseId{1};

  GoodsLine row;
  row.nomenclatureId = nomId;
  row.quantity = Quantity::fromMilli(5000); // 5 шт
  row.unitPrice = Money::fromCents(800000); // 8000 грн
  row.amount = Money::fromCents(4000000);   // 40000 грн
  auto orderLines = composeCustomerOrder(chart.value(), {row});
  REQUIRE(orderLines);
  custOrder.lines = orderLines.value();

  auto custOrderIdRes = session.documents().insert(custOrder);
  REQUIRE(custOrderIdRes);

  // Замовлення не потребує проведення в Головну книгу
  CHECK_FALSE(requiresLedgerPosting(DocumentType::CustomerOrder));

  // 2. Створюємо Замовлення постачальнику на 10 шт
  Document suppOrder;
  suppOrder.type = DocumentType::SupplierOrder;
  suppOrder.number = QStringLiteral("ЗП-001");
  suppOrder.date = QDate(2026, 3, 3);
  suppOrder.counterpartyId = cpId;
  suppOrder.warehouseId = WarehouseId{1};

  GoodsLine sRow;
  sRow.nomenclatureId = nomId;
  sRow.quantity = Quantity::fromMilli(10000); // 10 шт
  sRow.unitPrice = Money::fromCents(600000);  // 6000 грн
  sRow.amount = Money::fromCents(6000000);    // 60000 грн
  auto suppLines = composeSupplierOrder(chart.value(), {sRow});
  REQUIRE(suppLines);
  suppOrder.lines = suppLines.value();

  auto suppOrderIdRes = session.documents().insert(suppOrder);
  REQUIRE(suppOrderIdRes);

  // 3. Перевіряємо складський звіт на наявність резервів та замовлених товарів
  auto stock = session.stock().build(QDate(2026, 3, 1), QDate(2026, 3, 31), WarehouseId{1});
  REQUIRE(stock);
  REQUIRE(stock.value().rows.size() == 1);
  CHECK(stock.value().rows[0].reservedQty == Quantity::fromMilli(5000));
  CHECK(stock.value().rows[0].orderedQty == Quantity::fromMilli(10000));

  // 4. Введення на підставі: створення Реалізації (Sale) з Замовлення покупця
  auto loadedCustOrder = session.documents().findById(custOrderIdRes.value());
  REQUIRE(loadedCustOrder);
  auto saleFromOrder = buildBasedOn(loadedCustOrder.value(), DocumentType::Sale, chart.value());
  REQUIRE(saleFromOrder);
  CHECK(saleFromOrder.value().type == DocumentType::Sale);
  CHECK(saleFromOrder.value().counterpartyId == cpId);
  CHECK(saleFromOrder.value().contractId == contractId);
  CHECK(saleFromOrder.value().lines.size() == 2); // Виручка + Собівартість
}

TEST_CASE("Contract subconto dimension in ledger postings", "[trade][subconto][contract]")
{
  auto sessionRes = LedgerSession::openMemory();
  REQUIRE(sessionRes);
  auto & session = sessionRes.value();

  auto chart = session.accounts().listPostable();
  REQUIRE(chart);

  Counterparty cp;
  cp.name = QStringLiteral("ТОВ «Прогрес»");
  cp.edrpou = QStringLiteral("33445566");
  cp.isCustomer = true;
  auto cpId = session.counterparties().insert(cp).value();

  Contract contract;
  contract.counterpartyId = cpId;
  contract.number = QStringLiteral("ДОГ-77");
  contract.date = QDate(2026, 4, 1);
  contract.contractType = ContractType::WithCustomer;
  auto cId = session.contracts().insert(contract).value();

  Nomenclature item;
  item.name = QStringLiteral("Стіл офісний");
  item.unit = QStringLiteral("шт");
  auto nomId = session.nomenclature().insert(item).value();

  // Оприбуткування для наявності товару
  Document purchase;
  purchase.type = DocumentType::Purchase;
  purchase.number = QStringLiteral("НП-10");
  purchase.date = QDate(2026, 4, 1);
  purchase.counterpartyId = cpId;
  GoodsLine pRow;
  pRow.nomenclatureId = nomId;
  pRow.quantity = Quantity::fromMilli(5000);
  pRow.unitPrice = Money::fromCents(200000);
  pRow.amount = Money::fromCents(1000000);
  purchase.lines = composePurchase(chart.value(), {pRow}).value();
  auto pId = session.documents().insert(purchase).value();
  REQUIRE(session.posting().post(pId));

  // Продаж з договором
  Document sale;
  sale.type = DocumentType::Sale;
  sale.number = QStringLiteral("РТ-10");
  sale.date = QDate(2026, 4, 2);
  sale.counterpartyId = cpId;
  sale.contractId = cId;
  GoodsLine sRow;
  sRow.nomenclatureId = nomId;
  sRow.quantity = Quantity::fromMilli(2000);
  sRow.unitPrice = Money::fromCents(300000);
  sRow.amount = Money::fromCents(600000);
  sale.lines = composeSale(chart.value(), {sRow}).value();
  auto sId = session.documents().insert(sale).value();
  REQUIRE(session.posting().post(sId));

  // Перевірка проводок Головної книги:
  // Дт 361 (Субконто 1 = Контрагент, Субконто 2 = Договір) Кт 701
  auto entries = session.ledger().listForDocument(sId);
  REQUIRE(entries);
  REQUIRE(entries.value().size() >= 2);

  const auto & revenueEntry = entries.value()[0];
  CHECK(revenueEntry.debitSubconto.s1.kindId == subconto_kinds::Counterparty);
  CHECK(revenueEntry.debitSubconto.s1.entityId == cpId.value);
  CHECK(revenueEntry.debitSubconto.s2.kindId == subconto_kinds::Contract);
  CHECK(revenueEntry.debitSubconto.s2.entityId == cId.value);
}

TEST_CASE("Kit assembly and disassembly roll average cost on 281", "[trade][kit]")
{
  auto sessionRes = LedgerSession::openMemory();
  REQUIRE(sessionRes);
  auto & session = sessionRes.value();
  auto chart = session.accounts().listPostable();
  REQUIRE(chart);

  auto insertNom = [&](const QString & name) {
    Nomenclature n;
    n.name = name;
    n.unit = QStringLiteral("шт");
    return session.nomenclature().insert(n).value();
  };
  const NomenclatureId bolt = insertNom(QStringLiteral("Болт М8"));
  const NomenclatureId nut = insertNom(QStringLiteral("Гайка М8"));
  const NomenclatureId kit = insertNom(QStringLiteral("Набір М8"));

  Counterparty cp;
  cp.name = QStringLiteral("ТОВ Комплект");
  cp.isSupplier = true;
  const CounterpartyId cpId = session.counterparties().insert(cp).value();
  const WarehouseId wh{1};

  Document buy;
  buy.type = DocumentType::Purchase;
  buy.number = QStringLiteral("НП-KIT");
  buy.date = QDate(2026, 5, 1);
  buy.counterpartyId = cpId;
  buy.warehouseId = wh;
  GoodsLine b1;
  b1.nomenclatureId = bolt;
  b1.quantity = Quantity::fromMilli(10000);
  b1.unitPrice = Money::fromCents(10000);
  b1.amount = Money::fromCents(100000);
  GoodsLine b2;
  b2.nomenclatureId = nut;
  b2.quantity = Quantity::fromMilli(10000);
  b2.unitPrice = Money::fromCents(5000);
  b2.amount = Money::fromCents(50000);
  buy.lines = composePurchase(chart.value(), {b1, b2}).value();
  REQUIRE(session.posting().post(session.documents().insert(buy).value()));

  Document assembly;
  assembly.type = DocumentType::KitAssembly;
  assembly.number = QStringLiteral("КМ-1");
  assembly.date = QDate(2026, 5, 2);
  assembly.warehouseId = wh;
  GoodsLine kitRow;
  kitRow.nomenclatureId = kit;
  kitRow.quantity = Quantity::fromMilli(5000);
  GoodsLine c1;
  c1.nomenclatureId = bolt;
  c1.quantity = Quantity::fromMilli(5000);
  GoodsLine c2;
  c2.nomenclatureId = nut;
  c2.quantity = Quantity::fromMilli(5000);
  assembly.lines = composeKitAssembly(chart.value(), {kitRow, c1, c2}).value();
  REQUIRE(session.posting().post(session.documents().insert(assembly).value()));

  auto stock = session.stock().build(QDate(2026, 5, 1), QDate(2026, 5, 31), wh);
  REQUIRE(stock);
  auto findNom = [&](NomenclatureId id) -> StockRow {
    for (const StockRow & row : stock.value().rows) {
      if (row.nomenclatureId == id)
        return row;
    }
    FAIL("nomenclature missing from stock");
    return {};
  };
  CHECK(findNom(bolt).closingQty == Quantity::fromMilli(5000));
  CHECK(findNom(nut).closingQty == Quantity::fromMilli(5000));
  CHECK(findNom(kit).closingQty == Quantity::fromMilli(5000));
  CHECK(findNom(kit).closingAmount == Money::fromCents(75000));

  Document dis;
  dis.type = DocumentType::KitDisassembly;
  dis.number = QStringLiteral("РК-1");
  dis.date = QDate(2026, 5, 3);
  dis.warehouseId = wh;
  GoodsLine kitOut;
  kitOut.nomenclatureId = kit;
  kitOut.quantity = Quantity::fromMilli(2000);
  GoodsLine out1;
  out1.nomenclatureId = bolt;
  out1.quantity = Quantity::fromMilli(2000);
  out1.unitPrice = Money::fromCents(10000);
  GoodsLine out2;
  out2.nomenclatureId = nut;
  out2.quantity = Quantity::fromMilli(2000);
  out2.unitPrice = Money::fromCents(5000);
  dis.lines = composeKitDisassembly(chart.value(), {kitOut, out1, out2}).value();
  REQUIRE(session.posting().post(session.documents().insert(dis).value()));

  stock = session.stock().build(QDate(2026, 5, 1), QDate(2026, 5, 31), wh);
  REQUIRE(stock);
  CHECK(findNom(kit).closingQty == Quantity::fromMilli(3000));
  CHECK(findNom(bolt).closingQty == Quantity::fromMilli(7000));
  CHECK(findNom(nut).closingQty == Quantity::fromMilli(7000));
}
