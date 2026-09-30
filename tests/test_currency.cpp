#include "support/Session.h"

#include "fin/currency/NbuRateClient.h"
#include "fin/db/CurrencyRepository.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/Currency.h"
#include "fin/domain/MonthClose.h"
#include "fin/integrity/IntegrityChecker.h"
#include "fin/posting/InventoryCosting.h"
#include "fin/reports/Reports.h"

#include <catch2/catch_test_macros.hpp>

using namespace fin;
using namespace fin::test;

TEST_CASE("ExchangeRate calculates conversions accurately without float")
{
  // 1 USD = 41.5234 грн (nominal = 1, rateMicros = 41'523'400)
  const ExchangeRate usdRate(41'523'400, 1);
  REQUIRE(usdRate.isValid());
  REQUIRE(usdRate.nominal() == 1);
  REQUIRE(usdRate.rateMicros() == 41'523'400);

  // 100.00 USD -> 4 152.34 UAH (415234 копійок)
  const Result<Money> uah1 = usdRate.toUah(Money::fromCents(10000));
  REQUIRE(uah1);
  CHECK(uah1.value().cents() == 415234);

  // 4 152.34 UAH -> 100.00 USD (10000 центів)
  const Result<Money> usd1 = usdRate.toForeign(Money::fromCents(415234));
  REQUIRE(usd1);
  CHECK(usd1.value().cents() == 10000);

  // 100 JPY = 27.4500 грн (nominal = 100, rateMicros = 27'450'000)
  const ExchangeRate jpyRate(27'450'000, 100);
  REQUIRE(jpyRate.isValid());
  // 500.00 JPY (50000 cents) -> 500 * 0.2745 = 137.25 грн (13725 коп)
  const Result<Money> uahJpy = jpyRate.toUah(Money::fromCents(50000));
  REQUIRE(uahJpy);
  CHECK(uahJpy.value().cents() == 13725);
}

TEST_CASE("Currency repository handles CRUD and date-based lookup")
{
  LedgerSession session = openSession();

  const Result<std::vector<Currency>> list = session.currencies().listAll();
  REQUIRE(list);
  CHECK(list.value().size() >= 3u); // USD, EUR, PLN

  CurrencyRate r1;
  r1.currencyCode = QStringLiteral("USD");
  r1.rateDate = QDate(2026, 1, 1);
  r1.nominal = 1;
  r1.rateMicros = 40'000'000; // 40.00 грн
  REQUIRE(session.currencies().setRate(r1));

  CurrencyRate r2;
  r2.currencyCode = QStringLiteral("USD");
  r2.rateDate = QDate(2026, 1, 15);
  r2.nominal = 1;
  r2.rateMicros = 41'000'000; // 41.00 грн
  REQUIRE(session.currencies().setRate(r2));

  // Пошук курсу на 2026-01-10 -> повинен повернути курс від 2026-01-01 (40.00)
  const Result<CurrencyRate> on10 =
      session.currencies().getRate(QStringLiteral("USD"), QDate(2026, 1, 10));
  REQUIRE(on10);
  CHECK(on10.value().rateMicros == 40'000'000);

  // Пошук курсу на 2026-01-20 -> повинен повернути курс від 2026-01-15 (41.00)
  const Result<CurrencyRate> on20 =
      session.currencies().getRate(QStringLiteral("USD"), QDate(2026, 1, 20));
  REQUIRE(on20);
  CHECK(on20.value().rateMicros == 41'000'000);

  // Немає курсу ≤ дати — помилка (майбутній курс більше не підставляється)
  CurrencyRate eurFuture;
  eurFuture.currencyCode = QStringLiteral("EUR");
  eurFuture.rateDate = QDate(2026, 6, 1);
  eurFuture.nominal = 1;
  eurFuture.rateMicros = 45'000'000;
  REQUIRE(session.currencies().setRate(eurFuture));
  const Result<CurrencyRate> tooEarly =
      session.currencies().getRate(QStringLiteral("EUR"), QDate(2026, 1, 1));
  REQUIRE_FALSE(tooEarly);
}

TEST_CASE("Posting foreign currency purchase and sale documents")
{
  LedgerSession session = openSession();

  const AccountId goods = account(session, chart::kGoods);
  const AccountId suppliers = account(session, chart::kForeignSuppliers);
  const CounterpartyId cp = customer(session, QStringLiteral("Global Export Ltd"));
  const NomenclatureId item1 = item(session, QStringLiteral("Сервер Dell"));

  // Документ імпорту товарів: 1 000.00 USD по курсу 40.00 грн = 40 000.00 грн
  Document docP = makeDoc(DocumentType::Purchase, QDate(2026, 2, 1), QStringLiteral("IMP-1"));
  docP.counterpartyId = cp;
  docP.currencyCode = QStringLiteral("USD");
  docP.currencyRateMicros = 40'000'000;
  docP.currencyNominal = 1;

  DocumentLine lineP = stockLine(goods, suppliers, item1, 1000, 100000); // 1 шт, 1 000.00 USD
  docP.lines.push_back(lineP);

  const DocumentId pId = saveAndPost(session, docP);

  // Перевірка записів у Головній книзі: сума в грн = 40 000.00 грн, сума у валюті = 1 000.00 USD
  const Result<std::vector<LedgerEntry>> entriesP = session.ledger().listForDocument(pId);
  REQUIRE(entriesP);
  REQUIRE(entriesP.value().size() == 1u);
  CHECK(entriesP.value()[0].amount.cents() == 4000000); // 40 000 грн
  CHECK(entriesP.value()[0].currencyCode == QStringLiteral("USD"));
  CHECK(entriesP.value()[0].currencyAmount.cents() == 100000); // 1 000 USD
  CHECK(entriesP.value()[0].currencyRateMicros == 40'000'000);

  // Перевірка складського обліку: собівартість повинна бути 40 000.00 грн
  InventoryCosting costing(session.handle());
  const Result<Money> cost = costing.peekCost(WarehouseId{1}, item1, Quantity::fromMilli(1000));
  REQUIRE(cost);
  CHECK(cost.value().cents() == 4000000);
}

TEST_CASE("Posting foreign currency sale with average cost and integrity")
{
  LedgerSession session = openSession();

  const AccountId goods = account(session, chart::kGoods);
  const AccountId suppliers = account(session, chart::kForeignSuppliers);
  const AccountId customers = account(session, chart::kForeignCustomers);
  const AccountId revenue = account(session, chart::kRevenue);
  const AccountId cogs = account(session, chart::kCogs);
  const CounterpartyId cp = customer(session, QStringLiteral("FX Buyer"));
  const NomenclatureId item1 = item(session, QStringLiteral("Ноутбук"));

  Document purchase =
      makeDoc(DocumentType::Purchase, QDate(2026, 2, 1), QStringLiteral("IMP-SALE-1"));
  purchase.counterpartyId = cp;
  purchase.currencyCode = QStringLiteral("USD");
  purchase.currencyRateMicros = 40'000'000;
  purchase.currencyNominal = 1;
  purchase.lines.push_back(stockLine(goods, suppliers, item1, 1000, 100000));
  saveAndPost(session, purchase);

  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 2, 10), QStringLiteral("EXP-1"));
  sale.counterpartyId = cp;
  sale.currencyCode = QStringLiteral("USD");
  sale.currencyRateMicros = 41'000'000;
  sale.currencyNominal = 1;
  sale.lines.push_back(line(customers, revenue, 150000)); // 1 500 USD
  DocumentLine costLine = stockLine(cogs, goods, item1, 1000, 1);
  costLine.amount = Money::fromCents(1);
  sale.lines.push_back(costLine);

  const DocumentId saleId = saveAndPost(session, sale);
  IntegrityChecker checker(session.handle());
  REQUIRE(checker.verifyEngine());

  const Result<std::vector<LedgerEntry>> entries = session.ledger().listForDocument(saleId);
  REQUIRE(entries);
  REQUIRE(entries.value().size() >= 2u);

  bool foundCogs = false;
  for (const LedgerEntry & entry : entries.value()) {
    if (entry.debitAccountId == cogs && entry.creditAccountId == goods) {
      foundCogs = true;
      CHECK(entry.amount.cents() == 4000000); // собівартість з залишку в грн
      CHECK(entry.currencyCode == QStringLiteral("USD"));
      CHECK(entry.currencyAmount.isPositive());
    }
  }
  CHECK(foundCogs);
}

TEST_CASE("Foreign advance offset compares UAH ledger to FX document amount")
{
  LedgerSession session = openSession();

  const AccountId bank = account(session, chart::kForeignBank);
  const AccountId advance = account(session, chart::kForeignAdvancesIssued);
  const AccountId suppliers = account(session, chart::kForeignSuppliers);
  const AccountId goods = account(session, chart::kGoods);
  const CounterpartyId cp = customer(session, QStringLiteral("FX Supplier"));
  const NomenclatureId item1 = item(session, QStringLiteral("Монітор"));

  // Аванс постачальнику: 500 USD @ 40 = 20 000 грн
  Document prepay =
      makeDoc(DocumentType::BankPayment, QDate(2026, 3, 1), QStringLiteral("ADV-USD-1"));
  prepay.counterpartyId = cp;
  prepay.currencyCode = QStringLiteral("USD");
  prepay.currencyRateMicros = 40'000'000;
  prepay.currencyNominal = 1;
  prepay.lines.push_back(line(advance, bank, 50000)); // 500 USD
  saveAndPost(session, prepay);

  // Надходження на 800 USD @ 40 — залік має бути 500 USD, не 500 «як UAH»
  Document purchase =
      makeDoc(DocumentType::Purchase, QDate(2026, 3, 5), QStringLiteral("IMP-ADV-1"));
  purchase.counterpartyId = cp;
  purchase.currencyCode = QStringLiteral("USD");
  purchase.currencyRateMicros = 40'000'000;
  purchase.currencyNominal = 1;
  purchase.lines.push_back(stockLine(goods, suppliers, item1, 1000, 80000));

  const DocumentId pId = saveAndPost(session, purchase);
  const Result<Document> posted = session.documents().findById(pId);
  REQUIRE(posted);

  Money offset = Money::zero();
  for (const DocumentLine & row : posted.value().lines) {
    if (row.debitAccountId == suppliers && row.creditAccountId == advance) {
      offset = row.amount;
    }
  }
  CHECK(offset.cents() == 50000); // 500 USD, не 500 коп / не 20 000 USD
  REQUIRE(IntegrityChecker(session.handle()).verifyEngine());
}

TEST_CASE("Foreign sale with advance offset and COGS keeps inventoryUah in bounds")
{
  LedgerSession session = openSession();

  const AccountId goods = account(session, chart::kGoods);
  const AccountId suppliers = account(session, chart::kForeignSuppliers);
  const AccountId customers = account(session, chart::kForeignCustomers);
  const AccountId revenue = account(session, chart::kRevenue);
  const AccountId cogs = account(session, chart::kCogs);
  const AccountId bank = account(session, chart::kForeignBank);
  const AccountId advance = account(session, chart::kForeignAdvancesReceived);
  const CounterpartyId cp = customer(session, QStringLiteral("FX Adv Buyer"));
  const NomenclatureId item1 = item(session, QStringLiteral("Планшет"));

  Document purchase =
      makeDoc(DocumentType::Purchase, QDate(2026, 4, 1), QStringLiteral("IMP-ADV-S"));
  purchase.counterpartyId = cp;
  purchase.currencyCode = QStringLiteral("USD");
  purchase.currencyRateMicros = 40'000'000;
  purchase.currencyNominal = 1;
  purchase.lines.push_back(stockLine(goods, suppliers, item1, 1000, 100000));
  saveAndPost(session, purchase);

  Document prepay =
      makeDoc(DocumentType::BankPayment, QDate(2026, 4, 2), QStringLiteral("ADV-IN-1"));
  prepay.counterpartyId = cp;
  prepay.currencyCode = QStringLiteral("USD");
  prepay.currencyRateMicros = 40'000'000;
  prepay.currencyNominal = 1;
  prepay.lines.push_back(line(bank, advance, 50000));
  saveAndPost(session, prepay);

  Document sale = makeDoc(DocumentType::Sale, QDate(2026, 4, 10), QStringLiteral("EXP-ADV-1"));
  sale.counterpartyId = cp;
  sale.currencyCode = QStringLiteral("USD");
  sale.currencyRateMicros = 40'000'000;
  sale.currencyNominal = 1;
  sale.lines.push_back(line(customers, revenue, 80000));
  DocumentLine costLine = stockLine(cogs, goods, item1, 1000, 1);
  costLine.amount = Money::fromCents(1);
  sale.lines.push_back(costLine);

  const DocumentId saleId = saveAndPost(session, sale);
  REQUIRE(IntegrityChecker(session.handle()).verifyEngine());
  const Result<Document> posted = session.documents().findById(saleId);
  REQUIRE(posted);
  REQUIRE(posted.value().lines.size() >= 3u);
}

TEST_CASE("Foreign document without rate is rejected by validator")
{
  LedgerSession session = openSession();
  Document doc = makeDoc(DocumentType::BankPayment, QDate(2026, 5, 1), QStringLiteral("FX-NORATE"));
  doc.counterpartyId = customer(session, QStringLiteral("FX NoRate"));
  doc.currencyCode = QStringLiteral("USD");
  doc.currencyRateMicros = 0;
  doc.lines.push_back(
      line(account(session, chart::kForeignBank), account(session, chart::kForeignTransit), 10000));
  const Result<DocumentId> id = session.documents().insert(doc);
  REQUIRE(id);
  REQUIRE_FALSE(session.posting().post(id.value()));
}

TEST_CASE("Month-end foreign currency revaluation in period result")
{
  LedgerSession session = openSession();

  const AccountId bank312 = account(session, chart::kForeignBank);
  const AccountId transit = account(session, chart::kForeignTransit);
  const CounterpartyId cp = customer(session, QStringLiteral("Foreign Partner"));

  // 1. Початкове зарахування валюти на рахунок 312: 1 000.00 USD за курсом 40.00 грн (40 000.00 грн)
  Document doc =
      makeDoc(DocumentType::BankPayment, QDate(2026, 3, 10), QStringLiteral("BANK-USD-1"));
  doc.counterpartyId = cp;
  doc.currencyCode = QStringLiteral("USD");
  doc.currencyRateMicros = 40'000'000;
  doc.currencyNominal = 1;

  DocumentLine l = line(bank312, transit, 100000); // 1000 USD
  doc.lines.push_back(l);

  saveAndPost(session, doc);

  // 2. Встановлюємо новий курс на кінець місяця (2026-03-31): 41.50 грн за 1 USD (+1.50 грн/USD)
  CurrencyRate r;
  r.currencyCode = QStringLiteral("USD");
  r.rateDate = QDate(2026, 3, 31);
  r.nominal = 1;
  r.rateMicros = 41'500'000; // 41.50 грн
  REQUIRE(session.currencies().setRate(r));

  // 3. Формуємо документ PeriodResult за березень 2026
  const Result<Document> periodDoc = buildPeriodResultDocument(session.handle(), 2026, 3);
  REQUIRE(periodDoc);

  // Перевіряємо, що згенеровано проводку курсової різниці Дт 312 Кт 714 на 1 500.00 грн (150000 коп)
  const AccountId gain714 = account(session, chart::kExchangeGain);
  bool foundGain = false;
  for (const DocumentLine & line : periodDoc.value().lines) {
    if (line.debitAccountId == bank312 && line.creditAccountId == gain714) {
      foundGain = true;
      CHECK(line.amount.cents() == 150000); // +1500 грн
    }
  }
  CHECK(foundGain);

  // Зберігаємо і проводимо закриття періоду
  Result<DocumentId> docId = session.documents().insert(periodDoc.value());
  REQUIRE(docId);
  Status postStatus = session.posting().post(docId.value());
  if (!postStatus) {
    UNSCOPED_INFO("POST ERROR: " << postStatus.error().message.toStdString());
  }
  REQUIRE(postStatus);

  // Перевіряємо цілісність бази (нульовий дисбаланс)
  IntegrityChecker checker(session.handle());
  REQUIRE(checker.verifyEngine());
}

TEST_CASE("Month-end revaluation fails closed without NBU rate")
{
  LedgerSession session = openSession();
  const AccountId bank312 = account(session, chart::kForeignBank);
  const AccountId transit = account(session, chart::kForeignTransit);
  const CounterpartyId cp = customer(session, QStringLiteral("FX FailClosed"));

  Currency chf;
  chf.code = QStringLiteral("CHF");
  chf.nameUa = QStringLiteral("Швейцарський франк");
  chf.numericCode = 756;
  chf.symbol = QStringLiteral("Fr");
  REQUIRE(session.currencies().insert(chf));

  Document doc =
      makeDoc(DocumentType::BankPayment, QDate(2026, 6, 10), QStringLiteral("BANK-CHF-1"));
  doc.counterpartyId = cp;
  doc.currencyCode = QStringLiteral("CHF");
  doc.currencyRateMicros = 45'000'000;
  doc.currencyNominal = 1;
  doc.lines.push_back(line(bank312, transit, 10000)); // 100 CHF
  saveAndPost(session, doc);

  const Result<Document> periodDoc = buildPeriodResultDocument(session.handle(), 2026, 6);
  REQUIRE_FALSE(periodDoc);
  CHECK(periodDoc.error().message.contains(QStringLiteral("CHF")));
}

TEST_CASE("Month-end revaluation of 362 keeps counterparty on line")
{
  LedgerSession session = openSession();
  const AccountId customers = account(session, chart::kForeignCustomers);
  const AccountId revenue = account(session, chart::kServiceRevenue);
  const AccountId gain714 = account(session, chart::kExchangeGain);
  const CounterpartyId cp = customer(session, QStringLiteral("FX Debtor"));

  Document sale =
      makeDoc(DocumentType::ServiceSale, QDate(2026, 7, 5), QStringLiteral("EXP-362-1"));
  sale.counterpartyId = cp;
  sale.currencyCode = QStringLiteral("USD");
  sale.currencyRateMicros = 40'000'000;
  sale.currencyNominal = 1;
  sale.lines.push_back(line(customers, revenue, 100000)); // 1 000 USD = 40 000 грн
  saveAndPost(session, sale);

  CurrencyRate r;
  r.currencyCode = QStringLiteral("USD");
  r.rateDate = QDate(2026, 7, 31);
  r.nominal = 1;
  r.rateMicros = 42'000'000;
  REQUIRE(session.currencies().setRate(r));

  const Result<Document> periodDoc = buildPeriodResultDocument(session.handle(), 2026, 7);
  REQUIRE(periodDoc);
  bool found = false;
  for (const DocumentLine & row : periodDoc.value().lines) {
    if (row.debitAccountId == customers && row.creditAccountId == gain714) {
      found = true;
      CHECK(row.amount.cents() == 200000); // +2 000 грн
      CHECK(row.counterpartyId == cp);
    }
  }
  CHECK(found);
}

TEST_CASE("Month-end revaluation covers transit account 334")
{
  LedgerSession session = openSession();
  const AccountId transit = account(session, chart::kForeignTransit);
  const AccountId bank = account(session, chart::kForeignBank);
  const AccountId gain714 = account(session, chart::kExchangeGain);
  const CounterpartyId cp = customer(session, QStringLiteral("FX Transit"));

  Document doc =
      makeDoc(DocumentType::BankPayment, QDate(2026, 8, 10), QStringLiteral("TR-334-1"));
  doc.counterpartyId = cp;
  doc.currencyCode = QStringLiteral("USD");
  doc.currencyRateMicros = 40'000'000;
  doc.currencyNominal = 1;
  doc.lines.push_back(line(transit, bank, 50000)); // 500 USD на 334
  saveAndPost(session, doc);

  CurrencyRate r;
  r.currencyCode = QStringLiteral("USD");
  r.rateDate = QDate(2026, 8, 31);
  r.nominal = 1;
  r.rateMicros = 41'000'000;
  REQUIRE(session.currencies().setRate(r));

  const Result<Document> periodDoc = buildPeriodResultDocument(session.handle(), 2026, 8);
  REQUIRE(periodDoc);
  bool found334 = false;
  for (const DocumentLine & row : periodDoc.value().lines) {
    if (row.debitAccountId == transit && row.creditAccountId == gain714) {
      found334 = true;
      CHECK(row.amount.cents() == 50000); // +500 грн на 334
    }
  }
  CHECK(found334);
}

TEST_CASE("Trial balance exposes parallel foreign closing for 312")
{
  LedgerSession session = openSession();
  const AccountId bank312 = account(session, chart::kForeignBank);
  const AccountId transit = account(session, chart::kForeignTransit);
  const CounterpartyId cp = customer(session, QStringLiteral("FX OSV"));

  Document doc =
      makeDoc(DocumentType::BankPayment, QDate(2026, 9, 5), QStringLiteral("OSV-USD-1"));
  doc.counterpartyId = cp;
  doc.currencyCode = QStringLiteral("USD");
  doc.currencyRateMicros = 40'000'000;
  doc.currencyNominal = 1;
  doc.lines.push_back(line(bank312, transit, 25000)); // 250 USD
  saveAndPost(session, doc);

  const Result<TrialBalance> osv =
      session.trialBalance().build(QDate(2026, 9, 1), QDate(2026, 9, 30));
  REQUIRE(osv);
  bool found = false;
  for (const TrialBalanceRow & row : osv.value().rows) {
    if (row.code == QLatin1String(chart::kForeignBank)) {
      found = true;
      CHECK(row.currencyCode == QStringLiteral("USD"));
      CHECK(row.closingForeignDebit.cents() == 25000);
      CHECK(row.closingDebit.cents() == 1000000); // 10 000 грн
    }
  }
  CHECK(found);
}

TEST_CASE("NbuRateClient parses JSON response from bank.gov.ua accurately")
{
  const QByteArray sampleJson = R"([
    {
      "r030": 840,
      "txt": "Долар США",
      "rate": 41.5234,
      "cc": "USD",
      "exchangedate": "17.09.2026"
    },
    {
      "r030": 978,
      "txt": "Євро",
      "rate": 45.1200,
      "cc": "EUR",
      "exchangedate": "17.09.2026"
    },
    {
      "r030": 985,
      "txt": "Злотий",
      "rate": 10.5500,
      "cc": "PLN",
      "exchangedate": "17.09.2026"
    }
  ])";

  const Result<std::vector<CurrencyRate>> rates =
      NbuRateClient::parseNbuJson(sampleJson, QDate(2026, 9, 17));
  REQUIRE(rates);
  REQUIRE(rates.value().size() == 3u);

  // USD: 41.5234 -> 41'523'400 micros
  const CurrencyRate & usd = rates.value()[0];
  CHECK(usd.currencyCode == QStringLiteral("USD"));
  CHECK(usd.rateDate == QDate(2026, 9, 17));
  CHECK(usd.nominal == 1);
  CHECK(usd.rateMicros == 41'523'400);

  // EUR: 45.1200 -> 45'120'000 micros
  const CurrencyRate & eur = rates.value()[1];
  CHECK(eur.currencyCode == QStringLiteral("EUR"));
  CHECK(eur.rateMicros == 45'120'000);

  // PLN: 10.5500 -> 10'550'000 micros
  const CurrencyRate & pln = rates.value()[2];
  CHECK(pln.currencyCode == QStringLiteral("PLN"));
  CHECK(pln.rateMicros == 10'550'000);
}

TEST_CASE("CurrencyRepository checks rate presence on specific date")
{
  LedgerSession session = openSession();

  const QDate dt(2026, 5, 20);
  CHECK_FALSE(session.currencies().hasRatesOnDate(dt));
  CHECK_FALSE(session.currencies().hasRateOnDate(QStringLiteral("USD"), dt));
  // UAH is always considered present
  CHECK(session.currencies().hasRateOnDate(QStringLiteral("UAH"), dt));

  CurrencyRate r;
  r.currencyCode = QStringLiteral("USD");
  r.rateDate = dt;
  r.nominal = 1;
  r.rateMicros = 41'000'000;
  REQUIRE(session.currencies().setRate(r));

  CHECK(session.currencies().hasRatesOnDate(dt));
  CHECK(session.currencies().hasRateOnDate(QStringLiteral("USD"), dt));
  CHECK_FALSE(session.currencies().hasRateOnDate(QStringLiteral("EUR"), dt));
}
