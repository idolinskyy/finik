#include "support/Session.h"

#include "fin/core/Sql.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/print/PrintForms.h"

using namespace fin;
using namespace fin::test;

TEST_CASE("company profile defaults and round-trips")
{
  LedgerSession session = openSession();
  const Result<CompanyProfile> loaded = session.company().load();
  REQUIRE(loaded);
  REQUIRE(loaded.value().name == QStringLiteral("Підприємство"));
  REQUIRE(loaded.value().edrpou.isEmpty());

  CompanyProfile profile;
  profile.name = QStringLiteral("  ТОВ Друкарня  ");
  profile.edrpou = QStringLiteral("12345678");
  profile.address = QStringLiteral("Київ");
  profile.accountant = QStringLiteral("Іваненко");
  profile.cashier = QStringLiteral("Петренко");
  REQUIRE(session.company().save(profile));

  const Result<CompanyProfile> saved = session.company().load();
  REQUIRE(saved);
  REQUIRE(saved.value().name == QStringLiteral("ТОВ Друкарня"));
  REQUIRE(saved.value().edrpou == QStringLiteral("12345678"));
  REQUIRE(saved.value().heading().contains(QStringLiteral("ЄДРПОУ 12345678")));
  REQUIRE(saved.value().taxSystem == TaxSystem::Simplified);
  REQUIRE(saved.value().singleTaxRateBp == 500);
  REQUIRE_FALSE(saved.value().vatPayer);
  REQUIRE(saved.value().vatRateBp == 2000);

  CompanyProfile tax = saved.value();
  tax.taxSystem = TaxSystem::General;
  tax.singleTaxRateBp = 300;
  REQUIRE(session.company().save(tax));
  const Result<CompanyProfile> taxed = session.company().load();
  REQUIRE(taxed);
  REQUIRE(taxed.value().taxSystem == TaxSystem::General);
  REQUIRE(taxed.value().singleTaxRateBp == 300);
  tax.vatPayer = true;
  tax.vatRateBp = 2000;
  REQUIRE(session.company().save(tax));
  const Result<CompanyProfile> vat = session.company().load();
  REQUIRE(vat);
  REQUIRE(vat.value().vatPayer);
  REQUIRE(vat.value().vatRateBp == 2000);
  tax.vatRateBp = 10001;
  REQUIRE_FALSE(session.company().save(tax));

  // Платник ПДВ без ставки — відхиляємо: інакше документи й декларація
  // порахували б ПДВ за нульовою (або підставленою) ставкою
  tax.vatRateBp = 0;
  REQUIRE_FALSE(session.company().save(tax));

  // Для неплатника нульова ставка допустима і зберігається як 0
  tax.vatPayer = false;
  REQUIRE(session.company().save(tax));
  const Result<CompanyProfile> zeroRate = session.company().load();
  REQUIRE(zeroRate);
  REQUIRE(zeroRate.value().vatRateBp == 0);

  tax.vatPayer = true;
  tax.vatRateBp = 2000;
  REQUIRE(session.company().save(tax));
  tax.singleTaxRateBp = 10001;
  REQUIRE_FALSE(session.company().save(tax));

  CompanyProfile empty;
  empty.name = QStringLiteral("   ");
  REQUIRE_FALSE(session.company().save(empty));

  CompanyProfile longCode;
  longCode.name = QStringLiteral("ТОВ");
  longCode.edrpou = QStringLiteral("12345678901");
  REQUIRE_FALSE(session.company().save(longCode));
}

TEST_CASE("company IBAN is validated and fills MFO")
{
  LedgerSession session = openSession();
  CompanyProfile profile;
  profile.name = QStringLiteral("ТОВ Банк");
  profile.iban = QStringLiteral("UA953220010000000000000001234");
  REQUIRE(session.company().save(profile));
  const Result<CompanyProfile> saved = session.company().load();
  REQUIRE(saved);
  REQUIRE(saved.value().iban == QStringLiteral("UA953220010000000000000001234"));
  REQUIRE(saved.value().mfo == QStringLiteral("322001"));

  profile.iban = QStringLiteral("UA00");
  REQUIRE_FALSE(session.company().save(profile));
  profile.iban = QStringLiteral("UA953220010000000000000001234");
  profile.mfo = QStringLiteral("300335");
  REQUIRE_FALSE(session.company().save(profile));
}

TEST_CASE("print forms take company heading from the card")
{
  LedgerSession session = openSession();
  CompanyProfile profile;
  profile.name = QStringLiteral("ПП Каса-Тест");
  profile.edrpou = QStringLiteral("87654321");
  REQUIRE(session.company().save(profile));

  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  REQUIRE(accounts);
  Document cash = makeDoc(DocumentType::CashIn, QDate(2026, 9, 10), QStringLiteral("ПКО-org"));
  CashLine row;
  row.correspondenceId = account(session, chart::kRevenue);
  row.amount = Money::fromCents(100);
  const Result<std::vector<DocumentLine>> lines = composeCashIn(accounts.value(), {row});
  REQUIRE(lines);
  cash.lines = lines.value();
  saveAndPost(session, cash);

  PrintForms forms(session.handle());
  const Result<QString> html = forms.cashReceiptHtml(cash);
  REQUIRE(html);
  REQUIRE(html.value().contains(QStringLiteral("ПП Каса-Тест")));
  REQUIRE(html.value().contains(QStringLiteral("87654321")));
}

TEST_CASE("company monobank token is sealed at rest and allow_negative round-trips")
{
  LedgerSession session = openSession();
  CompanyProfile profile;
  profile.name = QStringLiteral("ТОВ Секрет");
  profile.monobankToken = QStringLiteral("uSecretToken-123");
  profile.allowNegativeStock = true;
  REQUIRE(session.company().save(profile));

  const Result<CompanyProfile> loaded = session.company().load();
  REQUIRE(loaded);
  REQUIRE(loaded.value().monobankToken == QStringLiteral("uSecretToken-123"));
  REQUIRE(loaded.value().allowNegativeStock);

  sql::Query query(session.handle());
  REQUIRE(query.exec(QStringLiteral(
      "SELECT monobank_token, allow_negative_stock FROM company_profile WHERE id = 1")));
  REQUIRE(query.next());
  const QString stored = query.value(0).toString();
  REQUIRE(stored.startsWith(QStringLiteral("v1:")));
  REQUIRE_FALSE(stored.contains(QStringLiteral("uSecretToken")));
  REQUIRE(query.value(1).toInt() == 1);
}
