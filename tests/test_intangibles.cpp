#include "support/Session.h"

#include "fin/auth/Access.h"
#include "fin/db/UserRepository.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/FixedAsset.h"
#include "fin/domain/IntangibleAsset.h"
#include "fin/print/PrintForms.h"
#include "fin/reports/Reports.h"

using namespace fin;
using namespace fin::test;

namespace {

IntangibleAsset insertAsset(LedgerSession & session, const QString & inventoryNo,
                            std::int64_t originalCents, int months, std::int64_t openingCents = 0)
{
  IntangibleAsset row;
  row.inventoryNo = inventoryNo;
  row.name = QStringLiteral("Програма ") + inventoryNo;
  row.assetAccountId = account(session, chart::kSoftware);
  row.expenseAccountId = account(session, chart::kAdminExpense);
  row.original = Money::fromCents(originalCents);
  row.openingAccumulated = Money::fromCents(openingCents);
  row.usefulLifeMonths = months;
  const Result<IntangibleId> id = session.intangibles().insert(row);
  REQUIRE(id);
  const Result<IntangibleAsset> loaded = session.intangibles().findById(id.value());
  REQUIRE(loaded);
  return loaded.value();
}

DocumentId postCommission(LedgerSession & session, const IntangibleAsset & asset,
                          AccountId correspondence, QDate date, CounterpartyId party = {})
{
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  Document document =
      makeDoc(DocumentType::IaCommission, date, QStringLiteral("НМ-%1").arg(asset.inventoryNo));
  document.counterpartyId = party;
  const Result<std::vector<DocumentLine>> lines =
      composeIaCommission(accounts.value(), correspondence, {asset});
  REQUIRE(lines);
  document.lines = lines.value();
  return saveAndPost(session, document);
}

} // namespace

TEST_CASE("chart includes intangible accounts 125 127 133 154")
{
  LedgerSession session = openSession();
  REQUIRE(account(session, chart::kSoftware).isValid());
  REQUIRE(account(session, chart::kOtherIntangibles).isValid());
  REQUIRE(account(session, chart::kIaAmortization).isValid());
  REQUIRE(account(session, chart::kCapexIa).isValid());
  const Result<Account> folder = session.accounts().findByCode(QStringLiteral("12"));
  REQUIRE(folder);
  REQUIRE(folder.value().isFolder);
  const Result<Account> depFolder = session.accounts().findByCode(QStringLiteral("13"));
  REQUIRE(depFolder);
  REQUIRE(depFolder.value().isFolder);
}

TEST_CASE("IA straight-line charge starts next month and dumps remainder at the end")
{
  IntangibleAsset asset;
  asset.status = IntangibleStatus::InUse;
  asset.commissionedOn = QDate(2026, 1, 15);
  asset.original = Money::fromCents(10000);
  asset.usefulLifeMonths = 60;
  const Result<Money> before = iaAmortizationCharge(asset, 2026, 1);
  REQUIRE(before);
  REQUIRE(before.value().isZero());
  const Result<Money> first = iaAmortizationCharge(asset, 2026, 2);
  REQUIRE(first);
  REQUIRE(first.value().cents() == 166);
  asset.accumulated = Money::fromCents(166 * 59);
  const Result<Money> last = iaAmortizationCharge(asset, 2031, 1);
  REQUIRE(last);
  REQUIRE(last.value().cents() == 10000 - 166 * 59);

  IntangibleAsset tiny;
  tiny.status = IntangibleStatus::InUse;
  tiny.commissionedOn = QDate(2026, 3, 1);
  tiny.original = Money::fromCents(50);
  tiny.usefulLifeMonths = 60;
  const Result<Money> mid = iaAmortizationCharge(tiny, 2026, 4);
  REQUIRE(mid);
  REQUIRE(mid.value().isZero());
  const Result<Money> dump = iaAmortizationCharge(tiny, 2031, 3);
  REQUIRE(dump);
  REQUIRE(dump.value().cents() == 50);
}

TEST_CASE("IA commission from supplier posts 125/631 and marks the card in use")
{
  LedgerSession session = openSession();
  const CounterpartyId mill = customer(session, QStringLiteral("Постачальник НМА"));
  IntangibleAsset asset = insertAsset(session, QStringLiteral("НМА-125"), 1800000, 36);
  const DocumentId id =
      postCommission(session, asset, account(session, chart::kSuppliers), QDate(2026, 3, 1), mill);
  const Result<IntangibleAsset> loaded = session.intangibles().findById(asset.id);
  REQUIRE(loaded);
  REQUIRE(loaded.value().status == IntangibleStatus::InUse);
  REQUIRE(loaded.value().commissionedOn == QDate(2026, 3, 1));
  REQUIRE(loaded.value().accumulated.isZero());
  REQUIRE(session.verify());

  const Result<AccountCard> card125 = session.accountCard().build(
      account(session, chart::kSoftware), QDate(2026, 3, 1), QDate(2026, 3, 31));
  REQUIRE(card125);
  const Result<Money> net125 = card125.value().closingDebit.minus(card125.value().closingCredit);
  REQUIRE(net125);
  REQUIRE(net125.value().cents() == 1800000);

  PrintForms forms(session.handle());
  const Result<Document> posted = session.documents().findById(id);
  REQUIRE(posted);
  const Result<QString> html = forms.documentHtml(posted.value());
  REQUIRE(html);
  REQUIRE(html.value().contains(QStringLiteral("НМА-125")));
  REQUIRE(html.value().contains(QStringLiteral("125")));
}

TEST_CASE("IA amortization posts 92/133 and cannot unpost commission afterwards")
{
  LedgerSession session = openSession();
  const CounterpartyId mill = customer(session, QStringLiteral("Постачальник НМА"));
  IntangibleAsset asset = insertAsset(session, QStringLiteral("НМА-АН"), 1800000, 36);
  const DocumentId commissionId =
      postCommission(session, asset, account(session, chart::kSuppliers), QDate(2026, 3, 1), mill);

  REQUIRE_FALSE(buildIaDepreciationDocument(session.handle(), 2026, 3));

  const Result<Document> built = buildIaDepreciationDocument(session.handle(), 2026, 4);
  REQUIRE(built);
  REQUIRE(built.value().date == QDate(2026, 4, 30));
  REQUIRE(built.value().lines.size() == 1);
  REQUIRE(built.value().lines.front().amount.cents() == 50000);
  const Result<DocumentId> amortId = session.documents().insert(built.value());
  REQUIRE(amortId);
  REQUIRE(session.posting().post(amortId.value()));

  const Result<IntangibleAsset> after = session.intangibles().findById(asset.id);
  REQUIRE(after);
  REQUIRE(after.value().accumulated.cents() == 50000);
  REQUIRE(session.verify());

  REQUIRE_FALSE(session.posting().unpost(commissionId));

  const Result<BalanceSheet> sheet =
      FinancialStatementReport(session.handle()).balance(QDate(2026, 4, 30));
  REQUIRE(sheet);
  bool sawDep = false;
  for (const StatementLine & row : sheet.value().assets) {
    if (row.code == QString::fromLatin1(chart::kIaAmortization)) {
      sawDep = true;
      REQUIRE(row.amount.cents() == -50000);
    }
  }
  REQUIRE(sawDep);
  for (const StatementLine & row : sheet.value().liabilities) {
    REQUIRE(row.code != QString::fromLatin1(chart::kIaAmortization));
  }

  PrintForms forms(session.handle());
  const Result<Document> amortDoc = session.documents().findById(amortId.value());
  REQUIRE(amortDoc);
  const Result<QString> html = forms.documentHtml(amortDoc.value());
  REQUIRE(html);
  REQUIRE(html.value().contains(QStringLiteral("133")));
}

TEST_CASE("IA and FA monthly amortization are independent documents")
{
  LedgerSession session = openSession();
  const CounterpartyId mill = customer(session, QStringLiteral("Постачальник"));
  IntangibleAsset ia = insertAsset(session, QStringLiteral("НМА-ОКР"), 1800000, 36);
  postCommission(session, ia, account(session, chart::kSuppliers), QDate(2026, 3, 1), mill);

  FixedAsset fa;
  fa.inventoryNo = QStringLiteral("ОЗ-ОКР");
  fa.name = QStringLiteral("Верстат окремо");
  fa.assetAccountId = account(session, chart::kMachinery);
  fa.expenseAccountId = account(session, chart::kAdminExpense);
  fa.original = Money::fromCents(12000000);
  fa.usefulLifeMonths = 60;
  const Result<FixedAssetId> faId = session.fixedAssets().insert(fa);
  REQUIRE(faId);
  const Result<FixedAsset> loadedFa = session.fixedAssets().findById(faId.value());
  REQUIRE(loadedFa);
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  Document faCommission =
      makeDoc(DocumentType::FaCommission, QDate(2026, 3, 1), QStringLiteral("ОЗ-ОКР-1"));
  faCommission.counterpartyId = mill;
  const Result<std::vector<DocumentLine>> faLines = composeFaCommission(
      accounts.value(), account(session, chart::kSuppliers), {loadedFa.value()});
  REQUIRE(faLines);
  faCommission.lines = faLines.value();
  saveAndPost(session, faCommission);

  const Result<Document> iaApril = buildIaDepreciationDocument(session.handle(), 2026, 4);
  REQUIRE(iaApril);
  const Result<DocumentId> iaAmortId = session.documents().insert(iaApril.value());
  REQUIRE(iaAmortId);
  REQUIRE(session.posting().post(iaAmortId.value()));

  const Result<Document> faApril = buildFaDepreciationDocument(session.handle(), 2026, 4);
  REQUIRE(faApril);
  const Result<DocumentId> faAmortId = session.documents().insert(faApril.value());
  REQUIRE(faAmortId);
  REQUIRE(session.posting().post(faAmortId.value()));
  REQUIRE(session.verify());
}

TEST_CASE("opening IA commission with 00 posts opening accumulated to 133")
{
  LedgerSession session = openSession();
  IntangibleAsset asset = insertAsset(session, QStringLiteral("НМА-00"), 5000000, 60, 1000000);
  postCommission(session, asset, account(session, chart::kAuxiliary), QDate(2026, 1, 1));
  const Result<IntangibleAsset> loaded = session.intangibles().findById(asset.id);
  REQUIRE(loaded);
  REQUIRE(loaded.value().accumulated.cents() == 1000000);
  REQUIRE(session.verify());

  const Result<AccountCard> card133 = session.accountCard().build(
      account(session, chart::kIaAmortization), QDate(2026, 1, 1), QDate(2026, 1, 31));
  REQUIRE(card133);
  const Result<Money> net133 = card133.value().closingCredit.minus(card133.value().closingDebit);
  REQUIRE(net133);
  REQUIRE(net133.value().cents() == 1000000);
}

TEST_CASE("in-use IA card cannot be deleted and cannot be commissioned twice")
{
  LedgerSession session = openSession();
  const CounterpartyId mill = customer(session, QStringLiteral("Постачальник НМА"));
  IntangibleAsset asset = insertAsset(session, QStringLiteral("НМА-DUP"), 800000, 24);
  postCommission(session, asset, account(session, chart::kSuppliers), QDate(2026, 2, 1), mill);
  REQUIRE_FALSE(session.intangibles().remove(asset.id));

  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  Document second =
      makeDoc(DocumentType::IaCommission, QDate(2026, 2, 2), QStringLiteral("НМ-again"));
  second.counterpartyId = mill;
  const Result<std::vector<DocumentLine>> lines =
      composeIaCommission(accounts.value(), account(session, chart::kSuppliers), {asset});
  REQUIRE(lines);
  second.lines = lines.value();
  const Result<DocumentId> id = session.documents().insert(second);
  REQUIRE(id);
  REQUIRE_FALSE(session.posting().post(id.value()));
}

TEST_CASE("viewer cannot insert an intangible asset card")
{
  LedgerSession session = openSession();
  UserRepository users(session.handle());
  User viewer;
  viewer.login = QStringLiteral("oglyad");
  viewer.displayName = QStringLiteral("Перегляд");
  viewer.role = UserRole::Viewer;
  const Result<UserId> id = users.insert(viewer);
  REQUIRE(id);
  REQUIRE(users.setCurrentUser(id.value()));
  IntangibleAsset row;
  row.inventoryNo = QStringLiteral("НМА-VIEW");
  row.name = QStringLiteral("Заборона");
  row.assetAccountId = account(session, chart::kSoftware);
  row.expenseAccountId = account(session, chart::kAdminExpense);
  row.original = Money::fromCents(1000);
  row.usefulLifeMonths = 12;
  REQUIRE_FALSE(session.intangibles().insert(row));
  REQUIRE_FALSE(Access::canWriteDocument(UserRole::Cashier, DocumentType::IaCommission));
  REQUIRE_FALSE(Access::canWriteDocument(UserRole::Viewer, DocumentType::IaDepreciation));
  REQUIRE(Access::canWriteDocument(UserRole::Accountant, DocumentType::IaCommission));
}

TEST_CASE("IA write-off includes disposal-month charge and clears the balance")
{
  LedgerSession session = openSession();
  const CounterpartyId mill = customer(session, QStringLiteral("Постачальник НМА"));
  IntangibleAsset asset = insertAsset(session, QStringLiteral("НМА-СП"), 1800000, 36);
  const DocumentId commissionId =
      postCommission(session, asset, account(session, chart::kSuppliers), QDate(2026, 3, 1), mill);

  const Result<Document> built = buildIaDepreciationDocument(session.handle(), 2026, 4);
  REQUIRE(built);
  const Result<DocumentId> amortId = session.documents().insert(built.value());
  REQUIRE(amortId);
  REQUIRE(session.posting().post(amortId.value()));

  Result<IntangibleAsset> afterAmort = session.intangibles().findById(asset.id);
  REQUIRE(afterAmort);
  REQUIRE(afterAmort.value().accumulated.cents() == 50000);

  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  Document writeOff =
      makeDoc(DocumentType::IaWriteOff, QDate(2026, 5, 20), QStringLiteral("СН-СП"));
  const Result<std::vector<DocumentLine>> lines =
      composeIaWriteOff(session.handle(), accounts.value(), {afterAmort.value()}, writeOff.date);
  REQUIRE(lines);
  bool sawCharge = false;
  bool sawAccum = false;
  bool sawResidual = false;
  Money credited = Money::zero();
  for (const DocumentLine & line : lines.value()) {
    REQUIRE(line.intangibleId == asset.id);
    if (line.creditAccountId == account(session, chart::kIaAmortization)) {
      sawCharge = true;
      REQUIRE(line.amount.cents() == 50000);
    }
    if (line.debitAccountId == account(session, chart::kIaAmortization) &&
        line.creditAccountId == account(session, chart::kSoftware)) {
      sawAccum = true;
      REQUIRE(line.amount.cents() == 100000);
    }
    if (line.debitAccountId == account(session, chart::kAdminExpense) &&
        line.creditAccountId == account(session, chart::kSoftware)) {
      sawResidual = true;
      REQUIRE(line.amount.cents() == 1700000);
    }
    if (line.creditAccountId == account(session, chart::kSoftware)) {
      const Result<Money> next = credited.plus(line.amount);
      REQUIRE(next);
      credited = next.value();
    }
  }
  REQUIRE(sawCharge);
  REQUIRE(sawAccum);
  REQUIRE(sawResidual);
  REQUIRE(credited.cents() == 1800000);
  writeOff.lines = lines.value();
  const DocumentId writeId = saveAndPost(session, writeOff);

  const Result<IntangibleAsset> disposed = session.intangibles().findById(asset.id);
  REQUIRE(disposed);
  REQUIRE(disposed.value().status == IntangibleStatus::Disposed);
  REQUIRE(disposed.value().disposedOn == QDate(2026, 5, 20));
  REQUIRE(disposed.value().accumulated.cents() == 100000);
  REQUIRE(session.verify());

  REQUIRE_FALSE(session.posting().unpost(commissionId));
  REQUIRE_FALSE(session.posting().unpost(amortId.value()));
  REQUIRE_FALSE(buildIaDepreciationDocument(session.handle(), 2026, 5));

  PrintForms forms(session.handle());
  const Result<Document> loaded = session.documents().findById(writeId);
  REQUIRE(loaded);
  const Result<QString> html = forms.documentHtml(loaded.value());
  REQUIRE(html);
  REQUIRE(html.value().contains(QStringLiteral("НМА-СП")));
  REQUIRE(html.value().contains(QStringLiteral("133")));

  REQUIRE(session.posting().unpost(writeId));
  const Result<IntangibleAsset> restored = session.intangibles().findById(asset.id);
  REQUIRE(restored);
  REQUIRE(restored.value().status == IntangibleStatus::InUse);
  REQUIRE_FALSE(restored.value().disposedOn.isValid());
  REQUIRE(restored.value().accumulated.cents() == 50000);
  REQUIRE(session.verify());
}

TEST_CASE("IA write-off after posted month amort does not double-charge")
{
  LedgerSession session = openSession();
  const CounterpartyId mill = customer(session, QStringLiteral("Постачальник НМА"));
  IntangibleAsset asset = insertAsset(session, QStringLiteral("НМА-АМ"), 1800000, 36);
  postCommission(session, asset, account(session, chart::kSuppliers), QDate(2026, 3, 1), mill);
  const Result<Document> april = buildIaDepreciationDocument(session.handle(), 2026, 4);
  REQUIRE(april);
  const Result<DocumentId> aprilId = session.documents().insert(april.value());
  REQUIRE(aprilId);
  REQUIRE(session.posting().post(aprilId.value()));
  const Result<Document> may = buildIaDepreciationDocument(session.handle(), 2026, 5);
  REQUIRE(may);
  const Result<DocumentId> mayId = session.documents().insert(may.value());
  REQUIRE(mayId);
  REQUIRE(session.posting().post(mayId.value()));

  const Result<IntangibleAsset> loaded = session.intangibles().findById(asset.id);
  REQUIRE(loaded);
  REQUIRE(loaded.value().accumulated.cents() == 100000);
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  Document writeOff =
      makeDoc(DocumentType::IaWriteOff, QDate(2026, 5, 31), QStringLiteral("СН-АМ"));
  const Result<std::vector<DocumentLine>> lines =
      composeIaWriteOff(session.handle(), accounts.value(), {loaded.value()}, writeOff.date);
  REQUIRE(lines);
  REQUIRE(lines.value().size() == 2);
  REQUIRE(lines.value()[0].debitAccountId == account(session, chart::kIaAmortization));
  REQUIRE(lines.value()[0].creditAccountId == account(session, chart::kSoftware));
  REQUIRE(lines.value()[0].amount.cents() == 100000);
  REQUIRE(lines.value()[1].debitAccountId == account(session, chart::kAdminExpense));
  REQUIRE(lines.value()[1].creditAccountId == account(session, chart::kSoftware));
  REQUIRE(lines.value()[1].amount.cents() == 1700000);
  writeOff.lines = lines.value();
  saveAndPost(session, writeOff);
  REQUIRE(session.verify());
  REQUIRE_FALSE(session.posting().unpost(mayId.value()));
}

TEST_CASE("IA write-off in the commissioning month does not charge amort")
{
  LedgerSession session = openSession();
  const CounterpartyId mill = customer(session, QStringLiteral("Постачальник НМА"));
  IntangibleAsset asset = insertAsset(session, QStringLiteral("НМА-МІС"), 500000, 24);
  postCommission(session, asset, account(session, chart::kSuppliers), QDate(2026, 6, 1), mill);
  const Result<IntangibleAsset> loaded = session.intangibles().findById(asset.id);
  REQUIRE(loaded);
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  Document writeOff =
      makeDoc(DocumentType::IaWriteOff, QDate(2026, 6, 20), QStringLiteral("СН-МІС"));
  const Result<std::vector<DocumentLine>> lines =
      composeIaWriteOff(session.handle(), accounts.value(), {loaded.value()}, writeOff.date);
  REQUIRE(lines);
  REQUIRE(lines.value().size() == 1);
  REQUIRE(lines.value().front().debitAccountId == account(session, chart::kAdminExpense));
  REQUIRE(lines.value().front().creditAccountId == account(session, chart::kSoftware));
  REQUIRE(lines.value().front().amount.cents() == 500000);
  writeOff.lines = lines.value();
  saveAndPost(session, writeOff);
  REQUIRE(session.verify());
}

TEST_CASE("cashier cannot post ia write-off")
{
  REQUIRE_FALSE(Access::canWriteDocument(UserRole::Cashier, DocumentType::IaWriteOff));
  REQUIRE(Access::canWriteDocument(UserRole::Accountant, DocumentType::IaWriteOff));
}
