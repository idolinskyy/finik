#include "support/Session.h"

#include "fin/auth/Access.h"
#include "fin/db/UserRepository.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/LowValueItem.h"
#include "fin/print/PrintForms.h"
#include "fin/reports/Reports.h"

using namespace fin;
using namespace fin::test;

namespace {

LowValueItem insertItem(LedgerSession & session, const QString & inventoryNo)
{
  LowValueItem row;
  row.inventoryNo = inventoryNo;
  row.name = QStringLiteral("Предмет ") + inventoryNo;
  row.unit = QStringLiteral("шт");
  const Result<LowValueId> id = session.lowValue().insert(row);
  REQUIRE(id);
  const Result<LowValueItem> loaded = session.lowValue().findById(id.value());
  REQUIRE(loaded);
  return loaded.value();
}

DocumentId postReceipt(LedgerSession & session, const LowValueItem & item, Quantity qty,
                       Money amount, QDate date, CounterpartyId party)
{
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  Document document =
      makeDoc(DocumentType::MsReceipt, date, QStringLiteral("МШ-%1").arg(item.inventoryNo));
  document.counterpartyId = party;
  MsLine row;
  row.itemId = item.id;
  row.quantity = qty;
  row.amount = amount;
  const Result<std::vector<DocumentLine>> lines = composeMsReceipt(accounts.value(), {row});
  REQUIRE(lines);
  document.lines = lines.value();
  return saveAndPost(session, document);
}

} // namespace

TEST_CASE("chart includes low-value account 22")
{
  LedgerSession session = openSession();
  REQUIRE(account(session, chart::kLowValue).isValid());
}

TEST_CASE("issue cost uses average and dumps remainder on the last units")
{
  LowValueItem item;
  item.inventoryNo = QStringLiteral("МШ-AVG");
  item.quantity = Quantity::fromMilli(10000);
  item.amount = Money::fromCents(1000);
  const Result<Money> two = lowValueIssueCost(item, Quantity::fromMilli(2000));
  REQUIRE(two);
  REQUIRE(two.value().cents() == 200);
  const Result<Money> rest = lowValueIssueCost(item, Quantity::fromMilli(10000));
  REQUIRE(rest);
  REQUIRE(rest.value().cents() == 1000);
  REQUIRE_FALSE(lowValueIssueCost(item, Quantity::fromMilli(11000)));
}

TEST_CASE("ms receipt posts 22/631 and updates the card")
{
  LedgerSession session = openSession();
  const CounterpartyId mill = customer(session, QStringLiteral("Постачальник МШП"));
  LowValueItem item = insertItem(session, QStringLiteral("МШ-22"));
  const DocumentId id = postReceipt(session, item, Quantity::fromMilli(10000),
                                    Money::fromCents(150000), QDate(2026, 5, 6), mill);
  const Result<LowValueItem> loaded = session.lowValue().findById(item.id);
  REQUIRE(loaded);
  REQUIRE(loaded.value().quantity.milli() == 10000);
  REQUIRE(loaded.value().amount.cents() == 150000);
  REQUIRE(session.verify());

  const Result<AccountCard> card = session.accountCard().build(
      account(session, chart::kLowValue), QDate(2026, 5, 1), QDate(2026, 5, 31));
  REQUIRE(card);
  const Result<Money> net = card.value().closingDebit.minus(card.value().closingCredit);
  REQUIRE(net);
  REQUIRE(net.value().cents() == 150000);

  PrintForms forms(session.handle());
  const Result<Document> posted = session.documents().findById(id);
  REQUIRE(posted);
  const Result<QString> html = forms.documentHtml(posted.value());
  REQUIRE(html);
  REQUIRE(html.value().contains(QStringLiteral("МШ-22")));
  REQUIRE(html.value().contains(QStringLiteral("22")));
}

TEST_CASE("ms issue posts 92/22 at average and cannot delete a used card")
{
  LedgerSession session = openSession();
  const CounterpartyId mill = customer(session, QStringLiteral("Постачальник МШП"));
  LowValueItem item = insertItem(session, QStringLiteral("МШ-ISS"));
  postReceipt(session, item, Quantity::fromMilli(10000), Money::fromCents(150000),
              QDate(2026, 5, 6), mill);

  const Result<LowValueItem> stocked = session.lowValue().findById(item.id);
  REQUIRE(stocked);
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  MsLine issueRow;
  issueRow.itemId = item.id;
  issueRow.quantity = Quantity::fromMilli(2000);
  Document issue = makeDoc(DocumentType::MsIssue, QDate(2026, 5, 12), QStringLiteral("МЕ-ISS"));
  const Result<std::vector<DocumentLine>> lines =
      composeMsIssue(accounts.value(), {stocked.value()}, {issueRow});
  REQUIRE(lines);
  REQUIRE(lines.value().front().amount.cents() == 30000);
  issue.lines = lines.value();
  const DocumentId issueId = saveAndPost(session, issue);

  const Result<LowValueItem> after = session.lowValue().findById(item.id);
  REQUIRE(after);
  REQUIRE(after.value().quantity.milli() == 8000);
  REQUIRE(after.value().amount.cents() == 120000);
  REQUIRE(session.verify());
  REQUIRE_FALSE(session.lowValue().remove(item.id));

  PrintForms forms(session.handle());
  const Result<Document> posted = session.documents().findById(issueId);
  REQUIRE(posted);
  const Result<QString> html = forms.documentHtml(posted.value());
  REQUIRE(html);
  REQUIRE(html.value().contains(QStringLiteral("92")));
}

TEST_CASE("unposting receipt after issue is blocked by remaining stock")
{
  LedgerSession session = openSession();
  const CounterpartyId mill = customer(session, QStringLiteral("Постачальник МШП"));
  LowValueItem item = insertItem(session, QStringLiteral("МШ-UNP"));
  const DocumentId receiptId = postReceipt(session, item, Quantity::fromMilli(10000),
                                           Money::fromCents(50000), QDate(2026, 6, 1), mill);
  const Result<LowValueItem> stocked = session.lowValue().findById(item.id);
  REQUIRE(stocked);
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  MsLine issueRow;
  issueRow.itemId = item.id;
  issueRow.quantity = Quantity::fromMilli(4000);
  Document issue = makeDoc(DocumentType::MsIssue, QDate(2026, 6, 2), QStringLiteral("МЕ-UNP"));
  const Result<std::vector<DocumentLine>> lines =
      composeMsIssue(accounts.value(), {stocked.value()}, {issueRow});
  REQUIRE(lines);
  issue.lines = lines.value();
  saveAndPost(session, issue);
  REQUIRE_FALSE(session.posting().unpost(receiptId));
}

TEST_CASE("empty card can be deleted; viewer cannot insert МШП")
{
  LedgerSession session = openSession();
  LowValueItem item = insertItem(session, QStringLiteral("МШ-DEL"));
  REQUIRE(session.lowValue().remove(item.id));

  UserRepository users(session.handle());
  User viewer;
  viewer.login = QStringLiteral("oglyad-ms");
  viewer.displayName = QStringLiteral("Перегляд");
  viewer.role = UserRole::Viewer;
  const Result<UserId> id = users.insert(viewer);
  REQUIRE(id);
  REQUIRE(users.setCurrentUser(id.value()));
  LowValueItem forbidden;
  forbidden.inventoryNo = QStringLiteral("МШ-VIEW");
  forbidden.name = QStringLiteral("Заборона");
  REQUIRE_FALSE(session.lowValue().insert(forbidden));
  REQUIRE_FALSE(Access::canWriteDocument(UserRole::Cashier, DocumentType::MsReceipt));
  REQUIRE_FALSE(Access::canWriteDocument(UserRole::Viewer, DocumentType::MsIssue));
  REQUIRE(Access::canWriteDocument(UserRole::Accountant, DocumentType::MsReceipt));
}
