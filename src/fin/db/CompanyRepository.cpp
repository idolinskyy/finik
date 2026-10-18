#include "fin/db/CompanyRepository.h"

#include "fin/auth/Access.h"
#include "fin/auth/SecretStore.h"
#include "fin/core/Sql.h"
#include "fin/db/AuditLog.h"
#include "fin/domain/BankRequisites.h"

namespace fin {

CompanyRepository::CompanyRepository(QSqlDatabase db) : db_(std::move(db)) {}

Result<CompanyProfile> CompanyRepository::load() const
{
  sql::Query query(db_);
  Status status = query.exec(
      QStringLiteral("SELECT name, edrpou, iban, mfo, bank_name, address, accountant, cashier, "
                     "tax_system, single_tax_rate_bp, vat_payer, vat_rate_bp, monobank_token, "
                     "allow_negative_stock, prro_enabled, checkbox_license_key, "
                     "checkbox_cashier_login, checkbox_cashier_pin, prro_payment_cash, "
                     "tax_office_region, tax_office_district, tax_office_code "
                     "FROM company_profile WHERE id = 1"));
  if (!status) {
    return Result<CompanyProfile>::fail(status.error());
  }
  if (!query.next()) {
    return Result<CompanyProfile>::fail(
        Error::database(QStringLiteral("немає картки підприємства")));
  }
  CompanyProfile profile;
  profile.name = query.value(0).toString();
  profile.edrpou = query.value(1).toString();
  profile.iban = query.value(2).toString();
  profile.mfo = query.value(3).toString();
  profile.bankName = query.value(4).toString();
  profile.address = query.value(5).toString();
  profile.accountant = query.value(6).toString();
  profile.cashier = query.value(7).toString();
  profile.taxSystem = taxSystemFromString(query.value(8).toString());
  profile.singleTaxRateBp = query.value(9).toInt();
  profile.vatPayer = query.value(10).toInt() != 0;
  profile.vatRateBp = query.value(11).toInt();
  const Result<QString> token = unsealSecret(query.value(12).toString());
  if (!token) {
    return Result<CompanyProfile>::fail(token.error());
  }
  profile.monobankToken = token.value();
  profile.allowNegativeStock = query.value(13).toInt() != 0;
  profile.prroEnabled = query.value(14).toInt() != 0;
  const Result<QString> license = unsealSecret(query.value(15).toString());
  if (!license) {
    return Result<CompanyProfile>::fail(license.error());
  }
  profile.checkboxLicenseKey = license.value();
  profile.checkboxCashierLogin = query.value(16).toString();
  const Result<QString> pin = unsealSecret(query.value(17).toString());
  if (!pin) {
    return Result<CompanyProfile>::fail(pin.error());
  }
  profile.checkboxCashierPin = pin.value();
  profile.prroPaymentCash = query.value(18).toInt() != 0;
  profile.taxOfficeRegion = query.value(19).toInt();
  profile.taxOfficeDistrict = query.value(20).toInt();
  profile.taxOfficeCode = query.value(21).toInt();
  if (profile.singleTaxRateBp <= 0) {
    profile.singleTaxRateBp = 500;
  }
  // 0 означає «ставку не задано» — не підмінюємо її на 20%, інакше експорт
  // декларації сформував би неправильну базу оподаткування.
  if (profile.vatRateBp < 0 || profile.vatRateBp > 10000) {
    profile.vatRateBp = 0;
  }
  if (profile.name.trimmed().isEmpty()) {
    profile.name = QStringLiteral("Підприємство");
  }
  return Result<CompanyProfile>::ok(profile);
}

Status CompanyRepository::save(const CompanyProfile & profile)
{
  Status access = Access::requireEditCompany(db_);
  if (!access) {
    return access;
  }
  const QString name = profile.name.trimmed();
  if (name.isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("назва підприємства обов'язкова")));
  }
  if (profile.edrpou.trimmed().size() > 10) {
    return Status::fail(
        Error::validation(QStringLiteral("ЄДРПОУ має містити не більше 10 символів")));
  }
  const QString iban = normalizeIban(profile.iban);
  QString mfo = profile.mfo.trimmed();
  if (mfo.isEmpty()) {
    mfo = mfoFromIban(iban);
  }
  const Status bank = validateBankRequisites(iban, mfo);
  if (!bank) {
    return bank;
  }
  if (profile.singleTaxRateBp < 0 || profile.singleTaxRateBp > 10000) {
    return Status::fail(
        Error::validation(QStringLiteral("ставка єдиного податку має бути від 0 до 100%")));
  }
  if (profile.vatRateBp < 0 || profile.vatRateBp > 10000) {
    return Status::fail(Error::validation(QStringLiteral("ставка ПДВ має бути від 0 до 100%")));
  }
  if (profile.vatPayer && profile.vatRateBp <= 0) {
    return Status::fail(Error::validation(
        QStringLiteral("платник ПДВ повинен мати базову ставку ПДВ більшу за 0")));
  }
  if (profile.taxOfficeRegion < 0 || profile.taxOfficeRegion > 99 ||
      profile.taxOfficeDistrict < 0 || profile.taxOfficeDistrict > 99 ||
      profile.taxOfficeCode < 0 || profile.taxOfficeCode > 9999) {
    return Status::fail(Error::validation(
        QStringLiteral("коди ДПІ: регіон і район 0–99, код ДПІ 0–9999 (0 = не задано)")));
  }
  const Result<QString> sealed = sealSecret(profile.monobankToken);
  if (!sealed) {
    return sealed.asStatus();
  }
  const Result<QString> sealedLicense = sealSecret(profile.checkboxLicenseKey);
  if (!sealedLicense) {
    return sealedLicense.asStatus();
  }
  const Result<QString> sealedPin = sealSecret(profile.checkboxCashierPin);
  if (!sealedPin) {
    return sealedPin.asStatus();
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("UPDATE company_profile SET name = :name, edrpou = :edrpou, "
                     "iban = :iban, mfo = :mfo, bank_name = :bank, address = :address, "
                     "accountant = :accountant, cashier = :cashier, tax_system = :tax, "
                     "single_tax_rate_bp = :rate, vat_payer = :vat, vat_rate_bp = :vatrate, "
                     "monobank_token = :monobank, allow_negative_stock = :negstock, "
                     "prro_enabled = :prro, checkbox_license_key = :lic, "
                     "checkbox_cashier_login = :clogin, checkbox_cashier_pin = :cpin, "
                     "prro_payment_cash = :pcash, tax_office_region = :treg, "
                     "tax_office_district = :traj, tax_office_code = :tsti "
                     "WHERE id = 1"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":name"), name);
  query.bind(QStringLiteral(":edrpou"), profile.edrpou.trimmed());
  query.bind(QStringLiteral(":iban"), iban);
  query.bind(QStringLiteral(":mfo"), mfo);
  query.bind(QStringLiteral(":bank"), profile.bankName.trimmed());
  query.bind(QStringLiteral(":address"), profile.address.trimmed());
  query.bind(QStringLiteral(":accountant"), profile.accountant.trimmed());
  query.bind(QStringLiteral(":cashier"), profile.cashier.trimmed());
  query.bind(QStringLiteral(":tax"), taxSystemToString(profile.taxSystem));
  query.bind(QStringLiteral(":rate"), profile.singleTaxRateBp);
  query.bind(QStringLiteral(":vat"), profile.vatPayer ? 1 : 0);
  query.bind(QStringLiteral(":vatrate"), profile.vatRateBp);
  query.bind(QStringLiteral(":monobank"), sealed.value());
  query.bind(QStringLiteral(":negstock"), profile.allowNegativeStock ? 1 : 0);
  query.bind(QStringLiteral(":prro"), profile.prroEnabled ? 1 : 0);
  query.bind(QStringLiteral(":lic"), sealedLicense.value());
  query.bind(QStringLiteral(":clogin"), profile.checkboxCashierLogin.trimmed());
  query.bind(QStringLiteral(":cpin"), sealedPin.value());
  query.bind(QStringLiteral(":pcash"), profile.prroPaymentCash ? 1 : 0);
  query.bind(QStringLiteral(":treg"), profile.taxOfficeRegion);
  query.bind(QStringLiteral(":traj"), profile.taxOfficeDistrict);
  query.bind(QStringLiteral(":tsti"), profile.taxOfficeCode);
  status = query.exec();
  if (!status) {
    return status;
  }
  return appendAudit(db_, QStringLiteral("update"), QStringLiteral("company"), 1, name);
}

} // namespace fin
