#include "support/Session.h"

#include "fin/auth/Access.h"
#include "fin/db/UserRepository.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/FixedAsset.h"
#include "fin/print/PrintForms.h"
#include "fin/reports/Reports.h"

using namespace fin;
using namespace fin::test;

namespace {

FixedAsset insertAsset(LedgerSession & session, const QString & inventoryNo,
                       std::int64_t originalCents, int months, std::int64_t openingCents = 0)
{
  FixedAsset row;
  row.inventoryNo = inventoryNo;
  row.name = QStringLiteral("Верстат ") + inventoryNo;
  row.assetAccountId = account(session, chart::kMachinery);
  row.expenseAccountId = account(session, chart::kAdminExpense);
  row.original = Money::fromCents(originalCents);
  row.openingAccumulated = Money::fromCents(openingCents);
  row.usefulLifeMonths = months;
  const Result<FixedAssetId> id = session.fixedAssets().insert(row);
  REQUIRE(id);
  const Result<FixedAsset> loaded = session.fixedAssets().findById(id.value());
  REQUIRE(loaded);
  return loaded.value();
}

DocumentId postCommission(LedgerSession & session, const FixedAsset & asset,
                          AccountId correspondence, QDate date, CounterpartyId party = {})
{
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  Document document =
      makeDoc(DocumentType::FaCommission, date, QStringLiteral("ОЗ-%1").arg(asset.inventoryNo));
  document.counterpartyId = party;
  const Result<std::vector<DocumentLine>> lines =
      composeFaCommission(accounts.value(), correspondence, {asset});
  REQUIRE(lines);
  document.lines = lines.value();
  return saveAndPost(session, document);
}

} // namespace

TEST_CASE("chart includes fixed asset accounts 104 105 109 131 152")
{
  LedgerSession session = openSession();
  REQUIRE(account(session, chart::kMachinery).isValid());
  REQUIRE(account(session, chart::kVehicles).isValid());
  REQUIRE(account(session, chart::kOtherFixedAssets).isValid());
  REQUIRE(account(session, chart::kFaDepreciation).isValid());
  REQUIRE(account(session, chart::kCapexFa).isValid());
  const Result<Account> folder = session.accounts().findByCode(QStringLiteral("10"));
  REQUIRE(folder);
  REQUIRE(folder.value().isFolder);
  const Result<Account> depFolder = session.accounts().findByCode(QStringLiteral("13"));
  REQUIRE(depFolder);
  REQUIRE(depFolder.value().isFolder);
}

TEST_CASE("straight-line charge starts next month and dumps remainder at the end")
{
  FixedAsset asset;
  asset.status = FixedAssetStatus::InUse;
  asset.commissionedOn = QDate(2026, 1, 15);
  asset.original = Money::fromCents(10000);
  asset.usefulLifeMonths = 60;
  const Result<Money> before = depreciationCharge(asset, 2026, 1);
  REQUIRE(before);
  REQUIRE(before.value().isZero());
  const Result<Money> first = depreciationCharge(asset, 2026, 2);
  REQUIRE(first);
  REQUIRE(first.value().cents() == 166);
  asset.accumulated = Money::fromCents(166 * 59);
  const Result<Money> last = depreciationCharge(asset, 2031, 1);
  REQUIRE(last);
  REQUIRE(last.value().cents() == 10000 - 166 * 59);

  FixedAsset tiny;
  tiny.status = FixedAssetStatus::InUse;
  tiny.commissionedOn = QDate(2026, 3, 1);
  tiny.original = Money::fromCents(50);
  tiny.usefulLifeMonths = 60;
  const Result<Money> mid = depreciationCharge(tiny, 2026, 4);
  REQUIRE(mid);
  REQUIRE(mid.value().isZero());
  const Result<Money> dump = depreciationCharge(tiny, 2031, 3);
  REQUIRE(dump);
  REQUIRE(dump.value().cents() == 50);
}

TEST_CASE("commission from supplier posts 104/631 and marks the card in use")
{
  LedgerSession session = openSession();
  const CounterpartyId mill = customer(session, QStringLiteral("Постачальник ОЗ"));
  FixedAsset asset = insertAsset(session, QStringLiteral("ОЗ-104"), 12000000, 60);
  const DocumentId id =
      postCommission(session, asset, account(session, chart::kSuppliers), QDate(2026, 3, 1), mill);
  const Result<FixedAsset> loaded = session.fixedAssets().findById(asset.id);
  REQUIRE(loaded);
  REQUIRE(loaded.value().status == FixedAssetStatus::InUse);
  REQUIRE(loaded.value().commissionedOn == QDate(2026, 3, 1));
  REQUIRE(loaded.value().accumulated.isZero());
  REQUIRE(session.verify());

  const Result<AccountCard> card104 = session.accountCard().build(
      account(session, chart::kMachinery), QDate(2026, 3, 1), QDate(2026, 3, 31));
  REQUIRE(card104);
  const Result<Money> net104 = card104.value().closingDebit.minus(card104.value().closingCredit);
  REQUIRE(net104);
  REQUIRE(net104.value().cents() == 12000000);

  PrintForms forms(session.handle());
  const Result<Document> posted = session.documents().findById(id);
  REQUIRE(posted);
  const Result<QString> html = forms.documentHtml(posted.value());
  REQUIRE(html);
  REQUIRE(html.value().contains(QStringLiteral("ОЗ-104")));
  REQUIRE(html.value().contains(QStringLiteral("104")));
}

TEST_CASE("amortization posts 92/131 and cannot unpost commission afterwards")
{
  LedgerSession session = openSession();
  const CounterpartyId mill = customer(session, QStringLiteral("Постачальник ОЗ"));
  FixedAsset asset = insertAsset(session, QStringLiteral("ОЗ-АМ"), 12000000, 60);
  const DocumentId commissionId =
      postCommission(session, asset, account(session, chart::kSuppliers), QDate(2026, 3, 1), mill);

  REQUIRE_FALSE(buildFaDepreciationDocument(session.handle(), 2026, 3));

  const Result<Document> built = buildFaDepreciationDocument(session.handle(), 2026, 4);
  REQUIRE(built);
  REQUIRE(built.value().date == QDate(2026, 4, 30));
  REQUIRE(built.value().lines.size() == 1);
  REQUIRE(built.value().lines.front().amount.cents() == 200000);
  const Result<DocumentId> amortId = session.documents().insert(built.value());
  REQUIRE(amortId);
  REQUIRE(session.posting().post(amortId.value()));

  const Result<FixedAsset> after = session.fixedAssets().findById(asset.id);
  REQUIRE(after);
  REQUIRE(after.value().accumulated.cents() == 200000);
  REQUIRE(session.verify());

  REQUIRE_FALSE(session.posting().unpost(commissionId));

  const Result<BalanceSheet> sheet =
      FinancialStatementReport(session.handle()).balance(QDate(2026, 4, 30));
  REQUIRE(sheet);
  bool sawDep = false;
  for (const StatementLine & row : sheet.value().assets) {
    if (row.code == QString::fromLatin1(chart::kFaDepreciation)) {
      sawDep = true;
      REQUIRE(row.amount.cents() == -200000);
    }
  }
  REQUIRE(sawDep);
  for (const StatementLine & row : sheet.value().liabilities) {
    REQUIRE(row.code != QString::fromLatin1(chart::kFaDepreciation));
  }

  PrintForms forms(session.handle());
  const Result<Document> amortDoc = session.documents().findById(amortId.value());
  REQUIRE(amortDoc);
  const Result<QString> html = forms.documentHtml(amortDoc.value());
  REQUIRE(html);
  REQUIRE(html.value().contains(QStringLiteral("131")));
}

TEST_CASE("opening commission with 00 posts opening accumulated to 131")
{
  LedgerSession session = openSession();
  FixedAsset asset = insertAsset(session, QStringLiteral("ОЗ-00"), 5000000, 60, 1000000);
  postCommission(session, asset, account(session, chart::kAuxiliary), QDate(2026, 1, 1));
  const Result<FixedAsset> loaded = session.fixedAssets().findById(asset.id);
  REQUIRE(loaded);
  REQUIRE(loaded.value().accumulated.cents() == 1000000);
  REQUIRE(session.verify());

  const Result<AccountCard> card131 = session.accountCard().build(
      account(session, chart::kFaDepreciation), QDate(2026, 1, 1), QDate(2026, 1, 31));
  REQUIRE(card131);
  const Result<Money> net131 = card131.value().closingCredit.minus(card131.value().closingDebit);
  REQUIRE(net131);
  REQUIRE(net131.value().cents() == 1000000);
}

TEST_CASE("in-use card cannot be deleted and cannot be commissioned twice")
{
  LedgerSession session = openSession();
  const CounterpartyId mill = customer(session, QStringLiteral("Постачальник ОЗ"));
  FixedAsset asset = insertAsset(session, QStringLiteral("ОЗ-DUP"), 800000, 24);
  postCommission(session, asset, account(session, chart::kSuppliers), QDate(2026, 2, 1), mill);
  REQUIRE_FALSE(session.fixedAssets().remove(asset.id));

  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  Document second =
      makeDoc(DocumentType::FaCommission, QDate(2026, 2, 2), QStringLiteral("ОЗ-again"));
  second.counterpartyId = mill;
  const Result<std::vector<DocumentLine>> lines =
      composeFaCommission(accounts.value(), account(session, chart::kSuppliers), {asset});
  REQUIRE(lines);
  second.lines = lines.value();
  const Result<DocumentId> id = session.documents().insert(second);
  REQUIRE(id);
  REQUIRE_FALSE(session.posting().post(id.value()));
}

TEST_CASE("viewer cannot insert a fixed asset card")
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
  FixedAsset row;
  row.inventoryNo = QStringLiteral("ОЗ-VIEW");
  row.name = QStringLiteral("Заборона");
  row.assetAccountId = account(session, chart::kMachinery);
  row.expenseAccountId = account(session, chart::kAdminExpense);
  row.original = Money::fromCents(1000);
  row.usefulLifeMonths = 12;
  REQUIRE_FALSE(session.fixedAssets().insert(row));
  REQUIRE_FALSE(Access::canWriteDocument(UserRole::Cashier, DocumentType::FaCommission));
  REQUIRE_FALSE(Access::canWriteDocument(UserRole::Viewer, DocumentType::FaDepreciation));
  REQUIRE(Access::canWriteDocument(UserRole::Accountant, DocumentType::FaCommission));
}

TEST_CASE("write-off includes disposal-month charge and clears the balance")
{
  LedgerSession session = openSession();
  const CounterpartyId mill = customer(session, QStringLiteral("Постачальник ОЗ"));
  FixedAsset asset = insertAsset(session, QStringLiteral("ОЗ-СП"), 12000000, 60);
  const DocumentId commissionId =
      postCommission(session, asset, account(session, chart::kSuppliers), QDate(2026, 3, 1), mill);

  const Result<Document> built = buildFaDepreciationDocument(session.handle(), 2026, 4);
  REQUIRE(built);
  const Result<DocumentId> amortId = session.documents().insert(built.value());
  REQUIRE(amortId);
  REQUIRE(session.posting().post(amortId.value()));

  Result<FixedAsset> afterAmort = session.fixedAssets().findById(asset.id);
  REQUIRE(afterAmort);
  REQUIRE(afterAmort.value().accumulated.cents() == 200000);

  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  Document writeOff =
      makeDoc(DocumentType::FaWriteOff, QDate(2026, 5, 20), QStringLiteral("СЗ-СП"));
  const Result<std::vector<DocumentLine>> lines =
      composeFaWriteOff(session.handle(), accounts.value(), {afterAmort.value()}, writeOff.date);
  REQUIRE(lines);
  bool sawCharge = false;
  bool sawAccum = false;
  bool sawResidual = false;
  Money credited = Money::zero();
  for (const DocumentLine & line : lines.value()) {
    REQUIRE(line.fixedAssetId == asset.id);
    if (line.creditAccountId == account(session, chart::kFaDepreciation)) {
      sawCharge = true;
      REQUIRE(line.amount.cents() == 200000);
    }
    if (line.debitAccountId == account(session, chart::kFaDepreciation) &&
        line.creditAccountId == account(session, chart::kMachinery)) {
      sawAccum = true;
      REQUIRE(line.amount.cents() == 400000);
    }
    if (line.debitAccountId == account(session, chart::kAdminExpense) &&
        line.creditAccountId == account(session, chart::kMachinery)) {
      sawResidual = true;
      REQUIRE(line.amount.cents() == 11600000);
    }
    if (line.creditAccountId == account(session, chart::kMachinery)) {
      const Result<Money> next = credited.plus(line.amount);
      REQUIRE(next);
      credited = next.value();
    }
  }
  REQUIRE(sawCharge);
  REQUIRE(sawAccum);
  REQUIRE(sawResidual);
  REQUIRE(credited.cents() == 12000000);
  writeOff.lines = lines.value();
  const DocumentId writeId = saveAndPost(session, writeOff);

  const Result<FixedAsset> disposed = session.fixedAssets().findById(asset.id);
  REQUIRE(disposed);
  REQUIRE(disposed.value().status == FixedAssetStatus::Disposed);
  REQUIRE(disposed.value().disposedOn == QDate(2026, 5, 20));
  REQUIRE(disposed.value().accumulated.cents() == 400000);
  REQUIRE(session.verify());

  REQUIRE_FALSE(session.posting().unpost(commissionId));
  REQUIRE_FALSE(session.posting().unpost(amortId.value()));
  REQUIRE_FALSE(buildFaDepreciationDocument(session.handle(), 2026, 5));

  PrintForms forms(session.handle());
  const Result<Document> loaded = session.documents().findById(writeId);
  REQUIRE(loaded);
  const Result<QString> html = forms.documentHtml(loaded.value());
  REQUIRE(html);
  REQUIRE(html.value().contains(QStringLiteral("ОЗ-СП")));
  REQUIRE(html.value().contains(QStringLiteral("131")));

  REQUIRE(session.posting().unpost(writeId));
  const Result<FixedAsset> restored = session.fixedAssets().findById(asset.id);
  REQUIRE(restored);
  REQUIRE(restored.value().status == FixedAssetStatus::InUse);
  REQUIRE_FALSE(restored.value().disposedOn.isValid());
  REQUIRE(restored.value().accumulated.cents() == 200000);
  REQUIRE(session.verify());
}

TEST_CASE("write-off after posted month amort does not double-charge")
{
  LedgerSession session = openSession();
  const CounterpartyId mill = customer(session, QStringLiteral("Постачальник ОЗ"));
  FixedAsset asset = insertAsset(session, QStringLiteral("ОЗ-АМ"), 12000000, 60);
  postCommission(session, asset, account(session, chart::kSuppliers), QDate(2026, 3, 1), mill);
  const Result<Document> april = buildFaDepreciationDocument(session.handle(), 2026, 4);
  REQUIRE(april);
  const Result<DocumentId> aprilId = session.documents().insert(april.value());
  REQUIRE(aprilId);
  REQUIRE(session.posting().post(aprilId.value()));
  const Result<Document> may = buildFaDepreciationDocument(session.handle(), 2026, 5);
  REQUIRE(may);
  const Result<DocumentId> mayId = session.documents().insert(may.value());
  REQUIRE(mayId);
  REQUIRE(session.posting().post(mayId.value()));

  const Result<FixedAsset> loaded = session.fixedAssets().findById(asset.id);
  REQUIRE(loaded);
  REQUIRE(loaded.value().accumulated.cents() == 400000);
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  Document writeOff =
      makeDoc(DocumentType::FaWriteOff, QDate(2026, 5, 31), QStringLiteral("СЗ-АМ"));
  const Result<std::vector<DocumentLine>> lines =
      composeFaWriteOff(session.handle(), accounts.value(), {loaded.value()}, writeOff.date);
  REQUIRE(lines);
  REQUIRE(lines.value().size() == 2);
  REQUIRE(lines.value()[0].debitAccountId == account(session, chart::kFaDepreciation));
  REQUIRE(lines.value()[0].creditAccountId == account(session, chart::kMachinery));
  REQUIRE(lines.value()[0].amount.cents() == 400000);
  REQUIRE(lines.value()[1].debitAccountId == account(session, chart::kAdminExpense));
  REQUIRE(lines.value()[1].creditAccountId == account(session, chart::kMachinery));
  REQUIRE(lines.value()[1].amount.cents() == 11600000);
  writeOff.lines = lines.value();
  saveAndPost(session, writeOff);
  REQUIRE(session.verify());
  REQUIRE_FALSE(session.posting().unpost(mayId.value()));
}

TEST_CASE("write-off in the commissioning month does not charge amort")
{
  LedgerSession session = openSession();
  const CounterpartyId mill = customer(session, QStringLiteral("Постачальник ОЗ"));
  FixedAsset asset = insertAsset(session, QStringLiteral("ОЗ-МІС"), 500000, 24);
  postCommission(session, asset, account(session, chart::kSuppliers), QDate(2026, 6, 2), mill);
  const Result<FixedAsset> loaded = session.fixedAssets().findById(asset.id);
  REQUIRE(loaded);
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  Document writeOff =
      makeDoc(DocumentType::FaWriteOff, QDate(2026, 6, 20), QStringLiteral("СЗ-МІС"));
  const Result<std::vector<DocumentLine>> lines =
      composeFaWriteOff(session.handle(), accounts.value(), {loaded.value()}, writeOff.date);
  REQUIRE(lines);
  REQUIRE(lines.value().size() == 1);
  REQUIRE(lines.value().front().debitAccountId == account(session, chart::kAdminExpense));
  REQUIRE(lines.value().front().creditAccountId == account(session, chart::kMachinery));
  REQUIRE(lines.value().front().amount.cents() == 500000);
  writeOff.lines = lines.value();
  saveAndPost(session, writeOff);
  REQUIRE(session.verify());
}

TEST_CASE("cashier cannot post fa write-off")
{
  REQUIRE_FALSE(Access::canWriteDocument(UserRole::Cashier, DocumentType::FaWriteOff));
  REQUIRE(Access::canWriteDocument(UserRole::Accountant, DocumentType::FaWriteOff));
}
