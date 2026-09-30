#include "support/Session.h"

#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/MonthClose.h"

using namespace fin;
using namespace fin::test;

namespace {

void postMonthResult(LedgerSession & session, int year, int month)
{
  Result<Document> document = buildPeriodResultDocument(session.handle(), year, month);
  REQUIRE(document);
  DocumentId id = document.value().id;
  if (!id) {
    Result<DocumentId> inserted = session.documents().insert(document.value());
    REQUIRE(inserted);
    id = inserted.value();
  } else {
    REQUIRE(session.documents().update(document.value()));
  }
  REQUIRE(session.posting().post(id));
}

} // namespace

TEST_CASE("closed month blocks post unpost and drafts")
{
  LedgerSession session = openSession();
  Document january = makeDoc(DocumentType::Manual, QDate(2026, 1, 15), QStringLiteral("БО-jan"));
  january.lines.push_back(
      line(account(session, chart::kCash), account(session, chart::kRevenue), 1000));
  const DocumentId januaryId = saveAndPost(session, january);

  postMonthResult(session, 2026, 1);
  REQUIRE(session.period().closeThroughMonth(2026, 1));
  const Result<QDate> closed = session.period().closedThrough();
  REQUIRE(closed);
  REQUIRE(closed.value() == QDate(2026, 1, 31));

  REQUIRE_FALSE(session.posting().unpost(januaryId));

  Document stillJanuary =
      makeDoc(DocumentType::Manual, QDate(2026, 1, 20), QStringLiteral("БО-jan2"));
  stillJanuary.lines.push_back(
      line(account(session, chart::kCash), account(session, chart::kRevenue), 500));
  REQUIRE_FALSE(session.documents().insert(stillJanuary));

  Document february = makeDoc(DocumentType::Manual, QDate(2026, 2, 1), QStringLiteral("БО-feb"));
  february.lines.push_back(
      line(account(session, chart::kCash), account(session, chart::kRevenue), 700));
  const DocumentId februaryId = saveAndPost(session, february);
  REQUIRE(session.posting().unpost(februaryId));
}

TEST_CASE("months close sequentially and reopen unlocks the last month")
{
  LedgerSession session = openSession();
  Document january = makeDoc(DocumentType::Manual, QDate(2026, 1, 2), QStringLiteral("БО-seq"));
  january.lines.push_back(
      line(account(session, chart::kCash), account(session, chart::kRevenue), 100));
  saveAndPost(session, january);

  postMonthResult(session, 2026, 1);
  REQUIRE(session.period().closeThroughMonth(2026, 1));
  REQUIRE_FALSE(session.period().closeThroughMonth(2026, 3));
  REQUIRE(session.period().closeThroughMonth(2026, 2));

  Document march = makeDoc(DocumentType::Manual, QDate(2026, 3, 1), QStringLiteral("БО-mar"));
  march.lines.push_back(
      line(account(session, chart::kCash), account(session, chart::kRevenue), 100));
  REQUIRE(session.documents().insert(march));

  REQUIRE(session.period().reopenLastMonth());
  const Result<QDate> closed = session.period().closedThrough();
  REQUIRE(closed);
  REQUIRE(closed.value() == QDate(2026, 1, 31));
  REQUIRE(session.period().reopenLastMonth());
  const Result<QDate> opened = session.period().closedThrough();
  REQUIRE(opened);
  REQUIRE_FALSE(opened.value().isValid());
}

TEST_CASE("unposted draft blocks period close")
{
  LedgerSession session = openSession();
  Document draft = makeDoc(DocumentType::Manual, QDate(2026, 4, 10), QStringLiteral("БО-draft"));
  draft.lines.push_back(
      line(account(session, chart::kCash), account(session, chart::kRevenue), 100));
  REQUIRE(session.documents().insert(draft));
  REQUIRE_FALSE(session.period().closeThroughMonth(2026, 4));
}

TEST_CASE("customer invoice draft does not block period close")
{
  LedgerSession session = openSession();
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  const NomenclatureId tea = item(session, QStringLiteral("Чай-період"));
  const CounterpartyId buyer = customer(session, QStringLiteral("Покупець-період"));
  GoodsLine row;
  row.nomenclatureId = tea;
  row.quantity = Quantity::fromMilli(1000);
  row.amount = Money::fromCents(1000);
  Document invoice =
      makeDoc(DocumentType::CustomerInvoice, QDate(2026, 4, 10), QStringLiteral("РХ-draft"));
  invoice.counterpartyId = buyer;
  const Result<std::vector<DocumentLine>> lines = composeCustomerInvoice(accounts.value(), {row});
  REQUIRE(lines);
  invoice.lines = lines.value();
  REQUIRE(session.documents().insert(invoice));
  REQUIRE(session.period().closeThroughMonth(2026, 4));
}

TEST_CASE("cannot delete a draft by moving it into a closed month")
{
  LedgerSession session = openSession();
  Document posted = makeDoc(DocumentType::Manual, QDate(2026, 5, 1), QStringLiteral("БО-may"));
  posted.lines.push_back(
      line(account(session, chart::kCash), account(session, chart::kRevenue), 100));
  saveAndPost(session, posted);
  postMonthResult(session, 2026, 5);
  REQUIRE(session.period().closeThroughMonth(2026, 5));

  Document draft = makeDoc(DocumentType::Manual, QDate(2026, 6, 1), QStringLiteral("БО-jun"));
  draft.lines.push_back(
      line(account(session, chart::kCash), account(session, chart::kRevenue), 100));
  Result<DocumentId> id = session.documents().insert(draft);
  REQUIRE(id);
  draft.id = id.value();
  draft.date = QDate(2026, 5, 15);
  REQUIRE_FALSE(session.documents().update(draft));
}

TEST_CASE("financial result closes 701 through 79 onto 44")
{
  LedgerSession session = openSession();
  Document cash = makeDoc(DocumentType::Manual, QDate(2026, 9, 5), QStringLiteral("БО-rev"));
  cash.lines.push_back(
      line(account(session, chart::kCash), account(session, chart::kRevenue), 50000));
  saveAndPost(session, cash);

  REQUIRE_FALSE(session.period().closeThroughMonth(2026, 9));
  postMonthResult(session, 2026, 9);
  REQUIRE(session.period().closeThroughMonth(2026, 9));

  const Result<Account> result =
      session.accounts().findByCode(QString::fromLatin1(chart::kFinancialResult));
  const Result<Account> retained =
      session.accounts().findByCode(QString::fromLatin1(chart::kRetainedEarnings));
  const Result<Account> revenue =
      session.accounts().findByCode(QString::fromLatin1(chart::kRevenue));
  REQUIRE(result);
  REQUIRE(retained);
  REQUIRE(revenue);
  const QDate from(2026, 9, 1);
  const QDate to(2026, 9, 30);
  const Result<AccountCard> resultCard = session.accountCard().build(result.value().id, from, to);
  const Result<AccountCard> retainedCard =
      session.accountCard().build(retained.value().id, from, to);
  const Result<AccountCard> revenueCard = session.accountCard().build(revenue.value().id, from, to);
  REQUIRE(resultCard);
  REQUIRE(retainedCard);
  REQUIRE(revenueCard);
  REQUIRE(revenueCard.value().closingDebit.cents() == revenueCard.value().closingCredit.cents());
  REQUIRE(resultCard.value().closingDebit.cents() == resultCard.value().closingCredit.cents());
  REQUIRE(retainedCard.value().closingCredit.cents() == 50000);
}

TEST_CASE("financial result closes service revenue 703 onto 44")
{
  LedgerSession session = openSession();
  Document service = makeDoc(DocumentType::Manual, QDate(2026, 8, 4), QStringLiteral("БО-703"));
  service.counterpartyId = customer(session, QStringLiteral("ТОВ Замовник"));
  service.lines.push_back(
      line(account(session, chart::kCustomers), account(session, chart::kServiceRevenue), 30000));
  saveAndPost(session, service);
  REQUIRE_FALSE(session.period().closeThroughMonth(2026, 8));
  postMonthResult(session, 2026, 8);
  REQUIRE(session.period().closeThroughMonth(2026, 8));
  const Result<Account> services =
      session.accounts().findByCode(QString::fromLatin1(chart::kServiceRevenue));
  const Result<Account> retained =
      session.accounts().findByCode(QString::fromLatin1(chart::kRetainedEarnings));
  REQUIRE(services);
  REQUIRE(retained);
  const Result<AccountCard> serviceCard =
      session.accountCard().build(services.value().id, QDate(2026, 8, 1), QDate(2026, 8, 31));
  const Result<AccountCard> retainedCard =
      session.accountCard().build(retained.value().id, QDate(2026, 8, 1), QDate(2026, 8, 31));
  REQUIRE(serviceCard);
  REQUIRE(retainedCard);
  REQUIRE(serviceCard.value().closingDebit.cents() == serviceCard.value().closingCredit.cents());
  REQUIRE(retainedCard.value().closingCredit.cents() == 30000);
}
