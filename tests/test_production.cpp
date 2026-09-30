#include <catch2/catch_test_macros.hpp>

#include "fin/core/Money.h"
#include "fin/core/Quantity.h"
#include "fin/db/BomRepository.h"
#include "fin/domain/BillOfMaterials.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/MonthClose.h"
#include "fin/session/LedgerSession.h"

using namespace fin;

TEST_CASE("Bill of Materials (BOM) repository operations", "[production][bom]")
{
  auto sessionRes = LedgerSession::openMemory();
  REQUIRE(sessionRes);
  auto & session = sessionRes.value();

  // 1. Створюємо номенклатуру готової продукції (Стіл письмовий) та матеріалів (ДСП, Ніжки)
  Nomenclature tableProd;
  tableProd.name = QStringLiteral("Стіл письмовий «Офіс-1»");
  tableProd.sku = QStringLiteral("PROD-TABLE");
  tableProd.unit = QStringLiteral("шт");
  auto tableIdRes = session.nomenclature().insert(tableProd);
  REQUIRE(tableIdRes);
  const NomenclatureId tableId = tableIdRes.value();

  Nomenclature dspMat;
  dspMat.name = QStringLiteral("Плита ДСП ламінована 16мм");
  dspMat.sku = QStringLiteral("MAT-DSP");
  dspMat.unit = QStringLiteral("м2");
  auto dspIdRes = session.nomenclature().insert(dspMat);
  REQUIRE(dspIdRes);
  const NomenclatureId dspId = dspIdRes.value();

  Nomenclature legMat;
  legMat.name = QStringLiteral("Ніжка металева 710мм");
  legMat.sku = QStringLiteral("MAT-LEG");
  legMat.unit = QStringLiteral("шт");
  auto legIdRes = session.nomenclature().insert(legMat);
  REQUIRE(legIdRes);
  const NomenclatureId legId = legIdRes.value();

  // 2. Створюємо специфікацію виробу на 1 стіл: 2.5 м2 ДСП + 4 ніжки
  BillOfMaterials bom;
  bom.productId = tableId;
  bom.name = QStringLiteral("Основна специфікація 2026");
  bom.outputQuantity = Quantity::fromMilli(1000); // 1 шт
  bom.isDefault = true;
  bom.isActive = true;
  bom.comment = QStringLiteral("Стандартна карта розкрою");

  BomLine line1;
  line1.lineNo = 1;
  line1.componentId = dspId;
  line1.quantity = Quantity::fromMilli(2500); // 2.5 м2
  line1.comment = QStringLiteral("Стільниця та царги");

  BomLine line2;
  line2.lineNo = 2;
  line2.componentId = legId;
  line2.quantity = Quantity::fromMilli(4000); // 4 шт
  line2.comment = QStringLiteral("Опори");

  bom.lines = {line1, line2};

  auto insertedBomRes = session.bom().insert(bom);
  REQUIRE(insertedBomRes);
  const BomId bomId = insertedBomRes.value();

  // 3. Завантаження та перевірка полів
  auto loadedBom = session.bom().findById(bomId);
  REQUIRE(loadedBom);
  CHECK(loadedBom.value().name == QStringLiteral("Основна специфікація 2026"));
  CHECK(loadedBom.value().productId == tableId);
  CHECK(loadedBom.value().outputQuantity == Quantity::fromMilli(1000));
  CHECK(loadedBom.value().lines.size() == 2);
  CHECK(loadedBom.value().lines[0].componentId == dspId);
  CHECK(loadedBom.value().lines[0].quantity == Quantity::fromMilli(2500));
  CHECK(loadedBom.value().lines[1].componentId == legId);
  CHECK(loadedBom.value().lines[1].quantity == Quantity::fromMilli(4000));

  // 4. Пошук специфікації за замовчуванням
  auto defaultBom = session.bom().findDefault(tableId);
  REQUIRE(defaultBom);
  CHECK(defaultBom.value().id == bomId);

  // 5. Оновлення специфікації
  BillOfMaterials updated = loadedBom.value();
  updated.name = QStringLiteral("Оновлена специфікація 2026 v2");
  updated.lines[0].quantity = Quantity::fromMilli(2400); // зменшили витрату до 2.4 м2
  auto updateStatus = session.bom().update(updated);
  REQUIRE(updateStatus);

  auto reloaded = session.bom().findById(bomId);
  REQUIRE(reloaded);
  CHECK(reloaded.value().name == QStringLiteral("Оновлена специфікація 2026 v2"));
  CHECK(reloaded.value().lines[0].quantity == Quantity::fromMilli(2400));

  // 6. Список усіх специфікацій
  auto allBoms = session.bom().listAll();
  REQUIRE(allBoms);
  CHECK(allBoms.value().size() == 1);

  // 7. Видалення специфікації
  auto delStatus = session.bom().remove(bomId);
  REQUIRE(delStatus);
  auto afterDel = session.bom().findById(bomId);
  CHECK_FALSE(afterDel);
}

TEST_CASE("Material issue (Вимога-накладна) average costing and movements", "[production][issue]")
{
  auto sessionRes = LedgerSession::openMemory();
  REQUIRE(sessionRes);
  auto & session = sessionRes.value();

  const auto accounts = session.accounts().listPostable().value();
  const AccountId acc201 = accountIdByCode(accounts, chart::kRawMaterials); // 201
  const AccountId acc231 = accountIdByCode(accounts, chart::kProduction);   // 231
  const AccountId acc631 = accountIdByCode(accounts, chart::kSuppliers);    // 631
  REQUIRE(acc201);
  REQUIRE(acc231);
  REQUIRE(acc631);

  // Постачальник та матеріал
  Counterparty supplier;
  supplier.name = QStringLiteral("ТОВ «Ліспром»");
  supplier.edrpou = QStringLiteral("44332211");
  supplier.isSupplier = true;
  const CounterpartyId supplierId = session.counterparties().insert(supplier).value();

  Nomenclature dsp;
  dsp.name = QStringLiteral("ДСП 16мм");
  dsp.unit = QStringLiteral("м2");
  const NomenclatureId dspId = session.nomenclature().insert(dsp).value();

  // Партія 1: Надходження 10 м2 по 200.00 грн (2000.00 грн)
  Document docP1;
  docP1.type = DocumentType::Purchase;
  docP1.date = QDate(2026, 3, 1);
  docP1.counterpartyId = supplierId;
  docP1.number = QStringLiteral("НАД-001");
  GoodsLine pLine1;
  pLine1.nomenclatureId = dspId;
  pLine1.quantity = Quantity::fromMilli(10000); // 10 м2
  pLine1.unitPrice = Money::fromCents(20000);   // 200.00 грн
  pLine1.amount = Money::fromCents(200000);     // 2000.00 грн
  docP1.lines = composePurchase(accounts, {pLine1}).value();
  const DocumentId p1Id = session.documents().insert(docP1).value();
  REQUIRE(session.posting().post(p1Id));

  // Партія 2: Надходження 10 м2 по 300.00 грн (3000.00 грн)
  Document docP2;
  docP2.type = DocumentType::Purchase;
  docP2.date = QDate(2026, 3, 2);
  docP2.counterpartyId = supplierId;
  docP2.number = QStringLiteral("НАД-002");
  GoodsLine pLine2;
  pLine2.nomenclatureId = dspId;
  pLine2.quantity = Quantity::fromMilli(10000); // 10 м2
  pLine2.unitPrice = Money::fromCents(30000);   // 300.00 грн
  pLine2.amount = Money::fromCents(300000);     // 3000.00 грн
  docP2.lines = composePurchase(accounts, {pLine2}).value();
  const DocumentId p2Id = session.documents().insert(docP2).value();
  REQUIRE(session.posting().post(p2Id));

  // Списання 15 м2 у виробництво за Вимогою-накладною (Дт 231 Кт 201)
  // За середньозваженою собівартістю: (2000.00 + 3000.00) / 20 м2 * 15 м2 = 3750.00 грн (375000 коп)
  Document docIssue;
  docIssue.type = DocumentType::MaterialIssue;
  docIssue.date = QDate(2026, 3, 5);
  docIssue.number = QStringLiteral("ВН-001");
  GoodsLine issueLine;
  issueLine.nomenclatureId = dspId;
  issueLine.quantity = Quantity::fromMilli(15000); // 15 м2
  issueLine.amount = Money::fromCents(100); // Тимчасова сума до проведення
  docIssue.lines = composeMaterialIssue(accounts, {issueLine}).value();
  const DocumentId issueId = session.documents().insert(docIssue).value();
  REQUIRE(session.posting().post(issueId));

  // Перевірка суми проведення та записів у Головній книзі
  auto postedIssue = session.documents().findById(issueId).value();
  REQUIRE(postedIssue.lines.size() == 1);
  CHECK(postedIssue.lines[0].amount == Money::fromCents(375000)); // 3750.00 грн

  auto ledgerEntries = session.ledger().listForDocument(issueId).value();
  REQUIRE(ledgerEntries.size() == 1);
  CHECK(ledgerEntries[0].debitAccountId == acc231);
  CHECK(ledgerEntries[0].creditAccountId == acc201);
  CHECK(ledgerEntries[0].amount == Money::fromCents(375000));

  // Перевірка залишку матеріалу на складі: залишилося 5 м2 по 250.00 = 1250.00 грн
  auto balances = session.inventory().balances(WarehouseId{1}).value();
  REQUIRE(balances.size() == 1);
  CHECK(balances[0].nomenclatureId == dspId);
  CHECK(balances[0].quantity == Quantity::fromMilli(5000));
  CHECK(balances[0].amount == Money::fromCents(125000));

  // Зняття проведення
  REQUIRE(session.posting().unpost(issueId));
  auto unpostedBalances = session.inventory().balances(WarehouseId{1}).value();
  CHECK(unpostedBalances[0].quantity == Quantity::fromMilli(20000));
  CHECK(unpostedBalances[0].amount == Money::fromCents(500000));
}

TEST_CASE("Production report (Випуск продукції) and complete manufacturing cycle",
          "[production][report]")
{
  auto sessionRes = LedgerSession::openMemory();
  REQUIRE(sessionRes);
  auto & session = sessionRes.value();

  const auto accounts = session.accounts().listPostable().value();
  const AccountId acc201 = accountIdByCode(accounts, chart::kRawMaterials);   // 201
  const AccountId acc231 = accountIdByCode(accounts, chart::kProduction);     // 231
  const AccountId acc26 = accountIdByCode(accounts, chart::kFinishedGoods);   // 26
  const AccountId acc361 = accountIdByCode(accounts, chart::kCustomers);      // 361
  const AccountId acc701 = accountIdByCode(accounts, chart::kRevenue);        // 701
  const AccountId acc901 = accountIdByCode(accounts, chart::kProductionCogs); // 901
  REQUIRE(acc201);
  REQUIRE(acc231);
  REQUIRE(acc26);
  REQUIRE(acc361);
  REQUIRE(acc701);
  REQUIRE(acc901);

  // Створюємо контрагента-покупця
  Counterparty buyer;
  buyer.name = QStringLiteral("ТОВ «Офіс-Плюс»");
  buyer.edrpou = QStringLiteral("55443322");
  buyer.isCustomer = true;
  const CounterpartyId buyerId = session.counterparties().insert(buyer).value();

  // Створюємо готову продукцію
  Nomenclature table;
  table.name = QStringLiteral("Стіл дубовий");
  table.unit = QStringLiteral("шт");
  const NomenclatureId tableId = session.nomenclature().insert(table).value();

  // Створюємо матеріал (дошка)
  Nomenclature board;
  board.name = QStringLiteral("Дошка дубова");
  board.unit = QStringLiteral("м3");
  const NomenclatureId boardId = session.nomenclature().insert(board).value();

  // 1. Надходження сировини: 2 м3 на суму 10 000.00 грн
  Counterparty sup;
  sup.name = QStringLiteral("Постачальник деревини");
  sup.isSupplier = true;
  const CounterpartyId supId = session.counterparties().insert(sup).value();

  Document docP;
  docP.type = DocumentType::Purchase;
  docP.date = QDate(2026, 4, 1);
  docP.counterpartyId = supId;
  docP.number = QStringLiteral("НАД-ДЕР");
  GoodsLine pRow;
  pRow.nomenclatureId = boardId;
  pRow.quantity = Quantity::fromMilli(2000); // 2 м3
  pRow.unitPrice = Money::fromCents(500000); // 5000.00 грн / м3
  pRow.amount = Money::fromCents(1000000);   // 10000.00 грн
  docP.lines = composePurchase(accounts, {pRow}).value();
  const DocumentId pDocId = session.documents().insert(docP).value();
  REQUIRE(session.posting().post(pDocId));

  // 2. Списання сировини у виробництво (Вимога-накладна): 1.5 м3 (собівартість 7500.00 грн) -> Дт 231 Кт 201
  Document docIssue;
  docIssue.type = DocumentType::MaterialIssue;
  docIssue.date = QDate(2026, 4, 2);
  docIssue.number = QStringLiteral("ВН-ДЕР-1");
  GoodsLine issueRow;
  issueRow.nomenclatureId = boardId;
  issueRow.quantity = Quantity::fromMilli(1500); // 1.5 м3
  issueRow.amount = Money::fromCents(100);
  docIssue.lines = composeMaterialIssue(accounts, {issueRow}).value();
  const DocumentId issueDocId = session.documents().insert(docIssue).value();
  REQUIRE(session.posting().post(issueDocId));

  // 3. Звіт виробництва за зміну (Випуск 5 столів) -> Дт 26 Кт 231 на суму 7500.00 грн (1500.00 грн/шт)
  Document docProd;
  docProd.type = DocumentType::ProductionReport;
  docProd.date = QDate(2026, 4, 3);
  docProd.number = QStringLiteral("ЗВ-001");
  GoodsLine prodRow;
  prodRow.nomenclatureId = tableId;
  prodRow.quantity = Quantity::fromMilli(5000); // 5 шт
  prodRow.unitPrice = Money::fromCents(150000); // 1500.00 грн
  prodRow.amount = Money::fromCents(750000);    // 7500.00 грн
  docProd.lines = composeProductionReport(accounts, {prodRow}).value();
  const DocumentId prodDocId = session.documents().insert(docProd).value();
  REQUIRE(session.posting().post(prodDocId));

  // Перевірка оприбуткування готової продукції на рахунок 26 та склад
  auto prodBalances = session.inventory().balances(WarehouseId{1}).value();
  bool foundTable = false;
  for (const auto & b : prodBalances) {
    if (b.nomenclatureId == tableId) {
      foundTable = true;
      CHECK(b.quantity == Quantity::fromMilli(5000));
      CHECK(b.amount == Money::fromCents(750000));
    }
  }
  CHECK(foundTable);

  // 4. Реалізація 3 столів покупцю по 3000.00 грн/шт (разом 9000.00 грн):
  // Дохід: Дт 361 Кт 701 (9000.00 грн)
  // Собівартість продукції: Дт 901 Кт 26 (3 шт * 1500.00 = 4500.00 грн)
  Document docSale;
  docSale.type = DocumentType::Sale;
  docSale.date = QDate(2026, 4, 10);
  docSale.counterpartyId = buyerId;
  docSale.number = QStringLiteral("РН-ПР-1");

  DocumentLine revLine;
  revLine.debitAccountId = acc361;
  revLine.creditAccountId = acc701;
  revLine.amount = Money::fromCents(900000); // 9000.00 грн
  revLine.unitPrice = Money::fromCents(300000);

  DocumentLine cogsLine;
  cogsLine.debitAccountId = acc901; // Собівартість продукції 901
  cogsLine.creditAccountId = acc26; // Готова продукція 26
  cogsLine.nomenclatureId = tableId;
  cogsLine.quantity = Quantity::fromMilli(3000); // 3 шт
  cogsLine.amount = Money::fromCents(100); // Перерахується за середньою при проведенні

  docSale.lines = {revLine, cogsLine};
  const DocumentId saleDocId = session.documents().insert(docSale).value();
  REQUIRE(session.posting().post(saleDocId));

  // Перевірка розрахунку собівартості реалізації продукції (Дт 901 Кт 26)
  auto postedSale = session.documents().findById(saleDocId).value();
  REQUIRE(postedSale.lines.size() == 2);
  CHECK(postedSale.lines[1].debitAccountId == acc901);
  CHECK(postedSale.lines[1].creditAccountId == acc26);
  CHECK(postedSale.lines[1].amount == Money::fromCents(450000)); // 4500.00 грн
}

TEST_CASE("Manufacturing overhead (Рахунок 91) and MonthClose allocation", "[production][overhead]")
{
  auto sessionRes = LedgerSession::openMemory();
  REQUIRE(sessionRes);
  auto & session = sessionRes.value();

  const auto accounts = session.accounts().listPostable().value();
  const AccountId acc91 = accountIdByCode(accounts, chart::kManufacturingOverhead); // 91
  const AccountId acc231 = accountIdByCode(accounts, chart::kProduction);           // 231
  const AccountId acc631 = accountIdByCode(accounts, chart::kSuppliers);            // 631
  const AccountId acc79 = accountIdByCode(accounts, chart::kFinancialResult);       // 79
  const AccountId acc44 = accountIdByCode(accounts, chart::kRetainedEarnings);      // 44
  REQUIRE(acc91);
  REQUIRE(acc231);
  REQUIRE(acc631);
  REQUIRE(acc79);
  REQUIRE(acc44);

  Counterparty utility;
  utility.name = QStringLiteral("Обленерго (Цех)");
  utility.isSupplier = true;
  const CounterpartyId utilityId = session.counterparties().insert(utility).value();

  // 1. Надходження послуг електроенергії цеху: Дт 91 Кт 631 на суму 2000.00 грн
  Document docService;
  docService.type = DocumentType::ServicePurchase;
  docService.date = QDate(2026, 5, 15);
  docService.counterpartyId = utilityId;
  docService.number = QStringLiteral("АКТ-ЕЛ-1");
  DocumentLine sLine;
  sLine.debitAccountId = acc91;
  sLine.creditAccountId = acc631;
  sLine.amount = Money::fromCents(200000); // 2000.00 грн
  docService.lines = {sLine};
  const DocumentId sDocId = session.documents().insert(docService).value();
  REQUIRE(session.posting().post(sDocId));

  // 2. Закриття місяця (composePeriodResult): перевіряємо регламентний розподіл 91 -> 231
  auto linesRes = composePeriodResult(session.handle(), QDate(2026, 5, 1), QDate(2026, 5, 31));
  REQUIRE(linesRes);
  const auto & closeLines = linesRes.value();

  bool found91Allocation = false;
  for (const auto & line : closeLines) {
    if (line.debitAccountId == acc231 && line.creditAccountId == acc91) {
      found91Allocation = true;
      CHECK(line.amount == Money::fromCents(200000));
      CHECK(line.description.contains(QStringLiteral("91 -> 231")));
    }
  }
  CHECK(found91Allocation);

  // 3. Формуємо документ закриття періоду та проводимо його
  auto closeDocRes = buildPeriodResultDocument(session.handle(), 2026, 5);
  REQUIRE(closeDocRes);
  const DocumentId closeDocId = session.documents().insert(closeDocRes.value()).value();
  REQUIRE(session.posting().post(closeDocId));

  // 4. Перевіряємо, що рахунок 91 повністю закрито
  auto closedCheck = requireIncomeExpenseClosed(session.handle(), QDate(2026, 5, 31));
  REQUIRE(closedCheck);
}

TEST_CASE("Based-on ProductionReport <-> MaterialIssue uses BOM", "[production][basedOn]")
{
  auto sessionRes = LedgerSession::openMemory();
  REQUIRE(sessionRes);
  auto & session = sessionRes.value();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);

  Nomenclature product;
  product.name = QStringLiteral("Виріб-тест");
  product.sku = QStringLiteral("P-1");
  product.unit = QStringLiteral("шт");
  const NomenclatureId productId = session.nomenclature().insert(product).value();

  Nomenclature mat;
  mat.name = QStringLiteral("Матеріал-тест");
  mat.sku = QStringLiteral("M-1");
  mat.unit = QStringLiteral("шт");
  const NomenclatureId matId = session.nomenclature().insert(mat).value();

  BillOfMaterials bom;
  bom.productId = productId;
  bom.name = QStringLiteral("Спец-тест");
  bom.outputQuantity = Quantity::fromMilli(1000);
  bom.isDefault = true;
  bom.isActive = true;
  BomLine line;
  line.componentId = matId;
  line.quantity = Quantity::fromMilli(2000);
  bom.lines = {line};
  const BomId bomId = session.bom().insert(bom).value();

  Document release;
  release.type = DocumentType::ProductionReport;
  release.date = QDate(2026, 9, 1);
  release.number = QStringLiteral("ВП-1");
  release.bomId = bomId;
  GoodsLine prodRow;
  prodRow.nomenclatureId = productId;
  prodRow.quantity = Quantity::fromMilli(1000);
  release.lines = composeProductionReport(accounts.value(), {prodRow}).value();
  release.id = session.documents().insert(release).value();

  Result<Document> issue =
      buildBasedOn(release, DocumentType::MaterialIssue, accounts.value(), session.handle());
  REQUIRE(issue);
  CHECK(issue.value().type == DocumentType::MaterialIssue);
  CHECK(issue.value().bomId == bomId);
  CHECK(issue.value().basedOnId == release.id);
  REQUIRE_FALSE(issue.value().lines.empty());
  bool foundMat = false;
  for (const DocumentLine & row : issue.value().lines) {
    if (row.nomenclatureId == matId) {
      foundMat = true;
      CHECK(row.quantity == Quantity::fromMilli(2000));
    }
  }
  CHECK(foundMat);

  Document materialIssue = issue.value();
  materialIssue.number = QStringLiteral("ВН-1");
  materialIssue.id = session.documents().insert(materialIssue).value();

  Result<Document> back =
      buildBasedOn(materialIssue, DocumentType::ProductionReport, accounts.value(),
                   session.handle());
  REQUIRE(back);
  CHECK(back.value().type == DocumentType::ProductionReport);
  CHECK(back.value().bomId == bomId);
  bool foundProduct = false;
  for (const DocumentLine & row : back.value().lines) {
    if (row.nomenclatureId == productId) {
      foundProduct = true;
    }
  }
  CHECK(foundProduct);

  Document bareRelease = release;
  bareRelease.bomId = {};
  REQUIRE_FALSE(
      buildBasedOn(bareRelease, DocumentType::MaterialIssue, accounts.value(), session.handle()));
}
