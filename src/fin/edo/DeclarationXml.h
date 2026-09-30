#pragma once

#include "fin/core/Result.h"
#include "fin/domain/Company.h"
#include "fin/edo/PayrollTaxDigest.h"
#include "fin/reports/Reports.h"

#include <QString>

namespace fin {

// Каркас J0200125 (R011/R101/R18); база від company.vatRateBp; потрібні коди ДПІ.
Result<QString> buildVatDeclarationXml(const VatStatement & report, const CompanyProfile & company);

Result<QString> buildBalance1MXml(const BalanceSheet & balance, const IncomeStatement & income,
                                  const CompanyProfile & company);

// Основний документ об'єднаної звітності (J/F0500111).
Result<QString> buildConsolidatedTaxXml(const ConsolidatedTaxReport & report,
                                        const CompanyProfile & company);

// Додаток 4ДФ (J/F0510411) — T1RXXXXG*; виплачено з BankPayment/CashOut.
Result<QString> buildAnnex4DfXml(const ConsolidatedTaxReport & report,
                                 const CompanyProfile & company);

// Додаток Д1 ЄСВ (J/F0510111) — місячний зріз.
Result<QString> buildAnnexEssD1Xml(const ConsolidatedTaxReport & monthly,
                                   const CompanyProfile & company);

// Додаток Д5 (J/F0510511) — трудові відносини (прийом / звільнення в кварталі).
Result<QString> buildAnnexD5Xml(const ConsolidatedTaxReport & report,
                                const CompanyProfile & company);

// Декларація ЄП 3 групи (F0103308 / J0103508).
// priorPeriodTax → R13; priorMilitary → R24 (для розрахунку R14 / R25).
Result<QString> buildSingleTaxDeclarationXml(const IncomeBook & book,
                                             const CompanyProfile & company,
                                             Money priorPeriodTax = Money::zero(),
                                             Money priorMilitary = Money::zero());

// Структурна перевірка DECLAR (C_DOC / обов'язкові теги) без повного XSD ДПС.
Status validateDeclarStructure(const QString & xml, const QString & expectedXsdBasename);

} // namespace fin
