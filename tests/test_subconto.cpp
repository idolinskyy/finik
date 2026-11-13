#include "support/Session.h"

#include "fin/db/UserRepository.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/User.h"
#include "fin/domain/Contract.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/MonthClose.h"
#include "fin/domain/Subconto.h"

using namespace fin;
using namespace fin::test;

TEST_CASE("SubcontoRepository: kinds and account configuration", "[subconto]")
{
  LedgerSession session = openSession();

  SECTION("Default kinds are seeded")
  {
    auto kindsRes = session.subconto().listKinds();
    REQUIRE(kindsRes);
    const auto & kinds = kindsRes.value();
    REQUIRE(kinds.size() >= 6);

    auto cpKind = session.subconto().findKind(subconto_kinds::Counterparty);
    REQUIRE(cpKind);
    CHECK(cpKind.value().nameUa == QStringLiteral("Контрагенти"));
    CHECK(cpKind.value().sourceTable == QStringLiteral("counterparties"));
  }

  SECTION("Default account subconto are configured")
  {
    const AccountId acc361 = account(session, chart::kCustomers);
    auto list = session.subconto().listForAccount(acc361);
    REQUIRE(list);
    REQUIRE(list.value().size() >= 1);
    CHECK(list.value()[0].lineNo == 1);
    CHECK(list.value()[0].subcontoKindId == subconto_kinds::Counterparty);
    CHECK(list.value()[0].isRequired == true);

    const AccountId acc281 = account(session, chart::kGoods);
    auto list281 = session.subconto().listForAccount(acc281);
    REQUIRE(list281);
    REQUIRE(list281.value().size() == 1);
    CHECK(list281.value()[0].subcontoKindId == subconto_kinds::Nomenclature);
  }

  SECTION("Custom account subconto update and validation")
  {
    const AccountId acc92 = account(session, chart::kAdminExpense);
    std::vector<AccountSubconto> settings = {{acc92, 1, subconto_kinds::Employee, true},
                                             {acc92, 2, subconto_kinds::Counterparty, false}};
    Status st = session.subconto().setForAccount(acc92, settings);
    REQUIRE(st);

    auto updated = session.subconto().listForAccount(acc92);
    REQUIRE(updated);
    REQUIRE(updated.value().size() == 2);
    CHECK(updated.value()[0].subcontoKindId == subconto_kinds::Employee);
    CHECK(updated.value()[1].subcontoKindId == subconto_kinds::Counterparty);
    CHECK(updated.value()[1].isRequired == false);

    // Rejects invalid line numbers or duplicate lines
    std::vector<AccountSubconto> invalidSettings = {
        {acc92, 1, subconto_kinds::Employee, true},
        {acc92, 1, subconto_kinds::Counterparty, false}};
    CHECK(!session.subconto().setForAccount(acc92, invalidSettings));

    // До 3 видів на рахунок
    std::vector<AccountSubconto> three = {{acc92, 1, subconto_kinds::Employee, true},
                                          {acc92, 2, subconto_kinds::Counterparty, false},
                                          {acc92, 3, subconto_kinds::Contract, false}};
    REQUIRE(session.subconto().setForAccount(acc92, three));
    auto threeRes = session.subconto().listForAccount(acc92);
    REQUIRE(threeRes);
    REQUIRE(threeRes.value().size() == 3);

    // Більше 3 — відхилення
    std::vector<AccountSubconto> four = three;
    four.push_back({acc92, 4, subconto_kinds::Nomenclature, false});
    CHECK(!session.subconto().setForAccount(acc92, four));
  }

  SECTION("Required contract subconto is validated on post")
  {
    const AccountId acc631 = account(session, chart::kSuppliers);
    std::vector<AccountSubconto> withContract = {{acc631, 1, subconto_kinds::Counterparty, true},
                                                 {acc631, 2, subconto_kinds::Contract, true}};
    REQUIRE(session.subconto().setForAccount(acc631, withContract));

    const CounterpartyId supplier = customer(session, QStringLiteral("ТОВ Договір"));
    const NomenclatureId goods = item(session, QStringLiteral("Товар-контракт"));
    Document doc = makeDoc(DocumentType::Purchase, QDate(2026, 5, 1), QStringLiteral("ПН-ctr"));
    doc.counterpartyId = supplier;
    // contractId порожній при обов'язковому Contract на 631
    doc.lines.push_back(stockLine(account(session, chart::kGoods), acc631, goods, 1000, 10000));
    Result<DocumentId> id = session.documents().insert(doc);
    REQUIRE(id);
    CHECK(!session.posting().post(id.value()));
  }
}

TEST_CASE("Subconto: only accountant or admin binds analytics to accounts", "[subconto]")
{
  LedgerSession session = openSession();
  const AccountId acc92 = account(session, chart::kAdminExpense);
  const std::vector<AccountSubconto> settings = {{acc92, 1, subconto_kinds::Employee, true}};

  UserRepository users(session.handle());
  const auto addUser = [&](const QString & login, UserRole role) {
    User row;
    row.login = login;
    row.displayName = login;
    row.role = role;
    const Result<UserId> id = users.insert(row);
    REQUIRE(id);
    return id.value();
  };
  const UserId viewerId = addUser(QStringLiteral("oglyad-sub"), UserRole::Viewer);
  const UserId cashierId = addUser(QStringLiteral("kasa-sub"), UserRole::Cashier);
  const UserId accountantId = addUser(QStringLiteral("buh-sub"), UserRole::Accountant);

  REQUIRE(users.setCurrentUser(viewerId));
  CHECK_FALSE(session.subconto().setForAccount(acc92, settings));

  REQUIRE(users.setCurrentUser(cashierId));
  CHECK_FALSE(session.subconto().setForAccount(acc92, settings));

  REQUIRE(users.setCurrentUser(accountantId));
  CHECK(session.subconto().setForAccount(acc92, settings));

  // Невідомий вид субконто відхиляється з поясненням, а не помилкою SQL
  const std::vector<AccountSubconto> unknown = {{acc92, 1, QStringLiteral("no_such_kind"), true}};
  CHECK_FALSE(session.subconto().setForAccount(acc92, unknown));
}

TEST_CASE("Subconto: manual entry and opening balances need required analytics", "[subconto]")
{
  LedgerSession session = openSession();
  const AccountId acc361 = account(session, chart::kCustomers);
  const AccountId revenue = account(session, chart::kRevenue);
  const AccountId zero = account(session, chart::kAuxiliary);

  SECTION("manual entry without counterparty is refused")
  {
    Document doc = makeDoc(DocumentType::Manual, QDate(2026, 5, 4), QStringLiteral("БО-sub"));
    doc.lines.push_back(line(acc361, revenue, 12000));
    const Result<DocumentId> id = session.documents().insert(doc);
    REQUIRE(id);
    CHECK_FALSE(session.posting().post(id.value()));

    doc.id = id.value();
    doc.counterpartyId = customer(session, QStringLiteral("ТОВ Ручна"));
    REQUIRE(session.documents().update(doc));
    CHECK(session.posting().post(id.value()));
  }

  SECTION("opening balance without counterparty is refused")
  {
    Document doc =
        makeDoc(DocumentType::OpeningBalance, QDate(2026, 1, 1), QStringLiteral("ЗС-sub"));
    doc.lines.push_back(line(acc361, zero, 30000));
    const Result<DocumentId> id = session.documents().insert(doc);
    REQUIRE(id);
    CHECK_FALSE(session.posting().post(id.value()));

    doc.id = id.value();
    doc.counterpartyId = customer(session, QStringLiteral("ТОВ Залишок"));
    REQUIRE(session.documents().update(doc));
    CHECK(session.posting().post(id.value()));
  }

  SECTION("one opening balance document carries a counterparty per line")
  {
    const CounterpartyId buyer = customer(session, QStringLiteral("ТОВ Дебітор"));
    const CounterpartyId supplier = customer(session, QStringLiteral("ТОВ Кредитор"));
    const AccountId acc631 = account(session, chart::kSuppliers);

    Document doc =
        makeDoc(DocumentType::OpeningBalance, QDate(2026, 1, 1), QStringLiteral("ЗС-multi"));
    DocumentLine debt = line(acc361, zero, 30000);
    debt.counterpartyId = buyer;
    doc.lines.push_back(debt);
    DocumentLine payable = line(zero, acc631, 20000);
    payable.counterpartyId = supplier;
    doc.lines.push_back(payable);
    const DocumentId id = saveAndPost(session, doc);

    const Result<std::vector<LedgerEntry>> entries = session.ledger().listForDocument(id);
    REQUIRE(entries);
    REQUIRE(entries.value().size() == 2);
    CHECK(entries.value()[0].debitSubconto.s1.entityId == buyer.value);
    CHECK(entries.value()[1].creditSubconto.s1.entityId == supplier.value);

    const auto tb =
        session.subcontoTrialBalance().build(acc361, 1, QDate(2026, 1, 1), QDate(2026, 1, 31));
    REQUIRE(tb);
    REQUIRE(tb.value().rows.size() == 1);
    CHECK(tb.value().rows[0].subconto.entityId == buyer.value);
    CHECK(tb.value().rows[0].closingDebit == Money::fromCents(30000));
  }

  SECTION("opening balance line carries contract subconto")
  {
    const CounterpartyId buyer = customer(session, QStringLiteral("ТОВ Дебітор-договір"));
    Contract contract;
    contract.counterpartyId = buyer;
    contract.number = QStringLiteral("ЗС-1");
    contract.name = QStringLiteral("Договір залишку");
    contract.date = QDate(2026, 1, 1);
    const Result<ContractId> cid = session.contracts().insert(contract);
    REQUIRE(cid);

    Document doc =
        makeDoc(DocumentType::OpeningBalance, QDate(2026, 1, 1), QStringLiteral("ЗС-ct"));
    DocumentLine debt = line(acc361, zero, 40000);
    debt.counterpartyId = buyer;
    debt.contractId = cid.value();
    doc.lines.push_back(debt);
    const DocumentId id = saveAndPost(session, doc);

    const Result<std::vector<LedgerEntry>> entries = session.ledger().listForDocument(id);
    REQUIRE(entries);
    REQUIRE(entries.value().size() == 1);
    CHECK(entries.value()[0].debitSubconto.s1.entityId == buyer.value);
    CHECK(entries.value()[0].debitSubconto.s2.entityId == cid.value().value);
  }

  SECTION("month close keeps aggregate entries without analytics")
  {
    Document sale = makeDoc(DocumentType::Manual, QDate(2026, 7, 3), QStringLiteral("БО-close"));
    sale.counterpartyId = customer(session, QStringLiteral("ТОВ Закриття"));
    sale.lines.push_back(line(acc361, revenue, 45000));
    saveAndPost(session, sale);

    const Result<Document> result = buildPeriodResultDocument(session.handle(), 2026, 7);
    REQUIRE(result);
    saveAndPost(session, result.value());
  }
}

TEST_CASE("Subconto: PostingEngine fills subconto and reports analyze by subconto", "[subconto]")
{
  LedgerSession session = openSession();

  const CounterpartyId supplier = customer(session, QStringLiteral("ТОВ Постачальник 1"));
  const NomenclatureId laptop = item(session, QStringLiteral("Ноутбук Dell"));

  const AccountId acc281 = account(session, chart::kGoods);
  const AccountId acc631 = account(session, chart::kSuppliers);

  // 1. Надходження товару: Дт 281 Кт 631 (10 шт по 1000 грн = 10 000 грн)
  Document docPurchase =
      makeDoc(DocumentType::Purchase, QDate(2026, 3, 1), QStringLiteral("ПН-001"));
  docPurchase.counterpartyId = supplier;
  docPurchase.lines.push_back(stockLine(acc281, acc631, laptop, 10000, 100000));

  const DocumentId docId = saveAndPost(session, docPurchase);

  // Перевіряємо проводку Головної книги на наявність субконто
  auto entries = session.ledger().listForDocument(docId);
  REQUIRE(entries);
  REQUIRE(entries.value().size() == 1);
  const auto & pEntry = entries.value()[0];

  // Дт 281 має субконто Номенклатура
  CHECK(pEntry.debitSubconto.s1.kindId == subconto_kinds::Nomenclature);
  CHECK(pEntry.debitSubconto.s1.entityId == laptop.value);

  // Кт 631 має субконто Контрагент
  CHECK(pEntry.creditSubconto.s1.kindId == subconto_kinds::Counterparty);
  CHECK(pEntry.creditSubconto.s1.entityId == supplier.value);

  // 2. Звіт: ОСВ по рахунку 281 у розрізі субконто Номенклатура
  auto tbRes =
      session.subcontoTrialBalance().build(acc281, 1, QDate(2026, 3, 1), QDate(2026, 3, 31));
  REQUIRE(tbRes);
  const auto & tb = tbRes.value();
  CHECK(tb.subcontoKindId == subconto_kinds::Nomenclature);
  REQUIRE(tb.rows.size() == 1);
  CHECK(tb.rows[0].subconto.entityId == laptop.value);
  CHECK(tb.rows[0].subconto.name == QStringLiteral("Ноутбук Dell"));
  CHECK(tb.rows[0].turnoverDebit == Money::fromCents(1000000));
  CHECK(tb.rows[0].closingDebit == Money::fromCents(1000000));
  CHECK(tb.totals.closingDebit == Money::fromCents(1000000));

  // 3. Звіт: Аналіз рахунку 281 (кореспонденції)
  auto analysisRes = session.accountAnalysis().build(acc281, QDate(2026, 3, 1), QDate(2026, 3, 31));
  REQUIRE(analysisRes);
  const auto & analysis = analysisRes.value();
  CHECK(analysis.turnoverDebit == Money::fromCents(1000000));
  REQUIRE(analysis.correspondences.size() == 1);
  CHECK(analysis.correspondences[0].corrAccountCode == QString::fromLatin1(chart::kSuppliers));
  CHECK(analysis.correspondences[0].debitTurnover == Money::fromCents(1000000));

  // 4. Картка рахунку з фільтрацією за субконто
  auto cardRes = session.accountCard().buildWithSubconto(
      acc631, QDate(2026, 3, 1), QDate(2026, 3, 31), subconto_kinds::Counterparty, supplier.value);
  REQUIRE(cardRes);
  const auto & card = cardRes.value();
  REQUIRE(card.rows.size() == 1);
  CHECK(card.rows[0].credit == Money::fromCents(1000000));
  CHECK(card.rows[0].creditSubconto.s1.name == QStringLiteral("ТОВ Постачальник 1"));
}

TEST_CASE("Subconto trial balance expands all dimensions as a tree", "[subconto]")
{
  LedgerSession session = openSession();
  const AccountId acc361 = account(session, chart::kCustomers);
  const AccountId zero = account(session, chart::kAuxiliary);

  const CounterpartyId alpha = customer(session, QStringLiteral("ТОВ Альфа-ОСВ"));
  const CounterpartyId beta = customer(session, QStringLiteral("ТОВ Бета-ОСВ"));
  Contract c1;
  c1.counterpartyId = alpha;
  c1.number = QStringLiteral("A-1");
  c1.name = QStringLiteral("Договір Альфа 1");
  c1.date = QDate(2026, 1, 1);
  const Result<ContractId> idA1 = session.contracts().insert(c1);
  REQUIRE(idA1);
  Contract c2;
  c2.counterpartyId = alpha;
  c2.number = QStringLiteral("A-2");
  c2.name = QStringLiteral("Договір Альфа 2");
  c2.date = QDate(2026, 1, 2);
  const Result<ContractId> idA2 = session.contracts().insert(c2);
  REQUIRE(idA2);
  Contract c3;
  c3.counterpartyId = beta;
  c3.number = QStringLiteral("B-1");
  c3.name = QStringLiteral("Договір Бета");
  c3.date = QDate(2026, 1, 3);
  const Result<ContractId> idB1 = session.contracts().insert(c3);
  REQUIRE(idB1);

  Document doc = makeDoc(DocumentType::OpeningBalance, QDate(2026, 1, 1), QStringLiteral("ЗС-tree"));
  DocumentLine l1 = line(acc361, zero, 10000);
  l1.counterpartyId = alpha;
  l1.contractId = idA1.value();
  doc.lines.push_back(l1);
  DocumentLine l2 = line(acc361, zero, 5000);
  l2.counterpartyId = alpha;
  l2.contractId = idA2.value();
  doc.lines.push_back(l2);
  DocumentLine l3 = line(acc361, zero, 3000);
  l3.counterpartyId = beta;
  l3.contractId = idB1.value();
  doc.lines.push_back(l3);
  saveAndPost(session, doc);

  const auto tree =
      session.subcontoTrialBalance().build(acc361, 0, QDate(2026, 1, 1), QDate(2026, 1, 31));
  REQUIRE(tree);
  REQUIRE(tree.value().hierarchical);
  REQUIRE(tree.value().rows.size() == 5);

  int parents = 0;
  int children = 0;
  Money parentSum = Money::zero();
  for (const SubcontoTrialBalanceRow & row : tree.value().rows) {
    if (row.level == 0) {
      ++parents;
      const Result<Money> added = parentSum.plus(row.closingDebit);
      REQUIRE(added);
      parentSum = added.value();
    } else if (row.level == 1) {
      ++children;
    }
  }
  REQUIRE(parents == 2);
  REQUIRE(children == 3);
  REQUIRE(parentSum == Money::fromCents(18000));
  REQUIRE(tree.value().totals.closingDebit == Money::fromCents(18000));

  const auto flat =
      session.subcontoTrialBalance().build(acc361, 1, QDate(2026, 1, 1), QDate(2026, 1, 31));
  REQUIRE(flat);
  REQUIRE_FALSE(flat.value().hierarchical);
  REQUIRE(flat.value().rows.size() == 2);
  REQUIRE(flat.value().totals.closingDebit == Money::fromCents(18000));
}

TEST_CASE("Custom subconto: expense articles seed, post and required check", "[subconto]")
{
  LedgerSession session = openSession();
  REQUIRE(session.accounts().seedChartIfEmpty());

  auto kinds = session.subconto().findKind(subconto_kinds::ExpenseArticle);
  REQUIRE(kinds);
  CHECK(isCustomSubcontoSource(kinds.value().sourceTable));

  const AccountId acc92 = account(session, chart::kAdminExpense);
  const AccountId acc311 = account(session, chart::kBank);
  auto bound = session.subconto().listForAccount(acc92);
  REQUIRE(bound);
  REQUIRE_FALSE(bound.value().empty());
  CHECK(bound.value()[0].subcontoKindId == subconto_kinds::ExpenseArticle);
  CHECK(bound.value()[0].isRequired == false);

  auto items = session.subconto().listItems(subconto_kinds::ExpenseArticle, true);
  REQUIRE(items);
  REQUIRE(items.value().size() >= 3);
  const std::int64_t articleId = items.value().front().id;

  Document optionalDoc = makeDoc(DocumentType::Manual, QDate(2026, 5, 10), QStringLiteral("БО-ст"));
  DocumentLine optionalLine = line(acc92, acc311, 15000);
  optionalLine.customSubconto1Id = articleId;
  optionalDoc.lines.push_back(optionalLine);
  const DocumentId postedId = saveAndPost(session, optionalDoc);

  const Result<std::vector<LedgerEntry>> entries = session.ledger().listForDocument(postedId);
  REQUIRE(entries);
  REQUIRE(entries.value().size() == 1);
  CHECK(entries.value()[0].debitSubconto.s1.kindId == subconto_kinds::ExpenseArticle);
  CHECK(entries.value()[0].debitSubconto.s1.entityId == articleId);

  const auto tb =
      session.subcontoTrialBalance().build(acc92, 1, QDate(2026, 5, 1), QDate(2026, 5, 31));
  REQUIRE(tb);
  REQUIRE(tb.value().rows.size() == 1);
  CHECK(tb.value().rows[0].subconto.entityId == articleId);
  CHECK(tb.value().rows[0].turnoverDebit == Money::fromCents(15000));

  std::vector<AccountSubconto> required = bound.value();
  required[0].isRequired = true;
  REQUIRE(session.subconto().setForAccount(acc92, required));

  Document missing = makeDoc(DocumentType::Manual, QDate(2026, 5, 11), QStringLiteral("БО-немає"));
  missing.lines.push_back(line(acc92, acc311, 2000));
  const Result<DocumentId> missingId = session.documents().insert(missing);
  REQUIRE(missingId);
  CHECK_FALSE(session.posting().post(missingId.value()));

  Document withArticle =
      makeDoc(DocumentType::Manual, QDate(2026, 5, 12), QStringLiteral("БО-є"));
  DocumentLine filled = line(acc92, acc311, 2000);
  filled.customSubconto1Id = articleId;
  withArticle.lines.push_back(filled);
  CHECK(saveAndPost(session, withArticle).isValid());
}

TEST_CASE("Custom subconto: items CRUD, new kind, viewer cannot edit", "[subconto]")
{
  LedgerSession session = openSession();

  SubcontoItem item;
  item.kindId = subconto_kinds::ExpenseArticle;
  item.code = QStringLiteral("IT");
  item.name = QStringLiteral("IT-послуги");
  const Result<std::int64_t> id = session.subconto().insertItem(item);
  REQUIRE(id);
  Result<SubcontoItem> found = session.subconto().findItem(id.value());
  REQUIRE(found);
  CHECK(found.value().name == QStringLiteral("IT-послуги"));

  found.value().name = QStringLiteral("IT та зв'язок");
  REQUIRE(session.subconto().updateItem(found.value()));
  REQUIRE(session.subconto().removeItem(id.value()));
  CHECK_FALSE(session.subconto().findItem(id.value()));

  REQUIRE(session.subconto().insertCustomKind(QStringLiteral("cost_center"),
                                              QStringLiteral("Центри витрат")));
  CHECK_FALSE(session.subconto().insertCustomKind(QStringLiteral("cost_center"),
                                                  QStringLiteral("дубль")));
  CHECK_FALSE(session.subconto().insertCustomKind(QStringLiteral("X"), QStringLiteral("коротко")));

  SubcontoItem center;
  center.kindId = QStringLiteral("cost_center");
  center.code = QStringLiteral("ADM");
  center.name = QStringLiteral("Адміністрація");
  REQUIRE(session.subconto().insertItem(center));

  UserRepository users(session.handle());
  User viewer;
  viewer.login = QStringLiteral("oglyad-article");
  viewer.displayName = viewer.login;
  viewer.role = UserRole::Viewer;
  const Result<UserId> viewerId = users.insert(viewer);
  REQUIRE(viewerId);
  REQUIRE(users.setCurrentUser(viewerId.value()));
  SubcontoItem blocked;
  blocked.kindId = subconto_kinds::ExpenseArticle;
  blocked.code = QStringLiteral("X1");
  blocked.name = QStringLiteral("Заборона");
  CHECK_FALSE(session.subconto().insertItem(blocked));
  CHECK_FALSE(session.subconto().insertCustomKind(QStringLiteral("dept"), QStringLiteral("Відділ")));
}
