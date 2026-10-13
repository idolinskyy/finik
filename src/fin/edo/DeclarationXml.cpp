#include "fin/edo/DeclarationXml.h"

#include "fin/domain/Employee.h"

#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace fin {
namespace {

constexpr const char * kUnifiedVer = "11";

QString digits(const QString & text)
{
  QString out;
  for (const QChar c : text.trimmed()) {
    if (c.isDigit()) {
      out += c;
    }
  }
  return out;
}

bool isFopTin(const QString & tin)
{
  return tin.size() == 10;
}

QString formPrefix(const CompanyProfile & company)
{
  return isFopTin(digits(company.edrpou)) ? QStringLiteral("F") : QStringLiteral("J");
}

QString moneyXml(Money m)
{
  return QString::number(static_cast<double>(m.cents()) / 100.0, 'f', 2);
}

void writeMoneyTag(QXmlStreamWriter & writer, const QString & tag, Money m, bool always = false)
{
  if (always || !m.isZero()) {
    writer.writeTextElement(tag, moneyXml(m));
  }
}

void writeRowNumMoney(QXmlStreamWriter & writer, const QString & tag, int rowNum, Money m)
{
  writer.writeStartElement(tag);
  writer.writeAttribute(QStringLiteral("ROWNUM"), QString::number(rowNum));
  writer.writeCharacters(moneyXml(m));
  writer.writeEndElement();
}

void writeRowNumText(QXmlStreamWriter & writer, const QString & tag, int rowNum,
                     const QString & text)
{
  writer.writeStartElement(tag);
  writer.writeAttribute(QStringLiteral("ROWNUM"), QString::number(rowNum));
  writer.writeCharacters(text);
  writer.writeEndElement();
}

void writeRowNumNil(QXmlStreamWriter & writer, const QString & tag, int rowNum)
{
  writer.writeStartElement(tag);
  writer.writeAttribute(QStringLiteral("ROWNUM"), QString::number(rowNum));
  writer.writeAttribute(QStringLiteral("xsi:nil"), QStringLiteral("true"));
  writer.writeEndElement();
}

struct DeclarHeadOpts
{
  QString docType;
  QString docSub;
  QString docVer;
  QDate from;
  QDate to;
  int periodType = 1;
  int periodMonth = 0;
  bool linkMainCalculation = false;
};

Status requireTaxOffice(const CompanyProfile & company)
{
  if (company.taxOfficeRegion <= 0 || company.taxOfficeDistrict <= 0 ||
      company.taxOfficeCode <= 0) {
    return Status::fail(Error::validation(QStringLiteral(
        "для експорту XML ДПС у картці підприємства вкажіть коди ДПІ "
        "(регіон C_REG, район C_RAJ, код ДПІ C_STI_ORIG)")));
  }
  return Status::ok();
}

void writeDeclarHead(QXmlStreamWriter & writer, const CompanyProfile & company,
                     const DeclarHeadOpts & opts)
{
  const int periodMonth = opts.periodMonth > 0 ? opts.periodMonth : opts.to.month();
  writer.writeStartElement(QStringLiteral("DECLARHEAD"));
  writer.writeTextElement(QStringLiteral("TIN"), digits(company.edrpou));
  writer.writeTextElement(QStringLiteral("C_DOC"), opts.docType);
  writer.writeTextElement(QStringLiteral("C_DOC_SUB"), opts.docSub);
  writer.writeTextElement(QStringLiteral("C_DOC_VER"), opts.docVer);
  writer.writeTextElement(QStringLiteral("C_DOC_TYPE"), QStringLiteral("0"));
  writer.writeTextElement(QStringLiteral("C_DOC_CNT"), QStringLiteral("1"));
  writer.writeTextElement(QStringLiteral("C_REG"), QString::number(company.taxOfficeRegion));
  writer.writeTextElement(QStringLiteral("C_RAJ"), QString::number(company.taxOfficeDistrict));
  writer.writeTextElement(QStringLiteral("PERIOD_MONTH"), QString::number(periodMonth));
  writer.writeTextElement(QStringLiteral("PERIOD_TYPE"), QString::number(opts.periodType));
  writer.writeTextElement(QStringLiteral("PERIOD_YEAR"), QString::number(opts.to.year()));
  writer.writeTextElement(QStringLiteral("C_STI_ORIG"), QString::number(company.taxOfficeCode));
  writer.writeTextElement(QStringLiteral("C_DOC_STAN"), QStringLiteral("1"));
  if (opts.linkMainCalculation) {
    writer.writeStartElement(QStringLiteral("LINKED_DOCS"));
    writer.writeStartElement(QStringLiteral("DOC"));
    writer.writeTextElement(QStringLiteral("C_DOC"), formPrefix(company) + QStringLiteral("05"));
    writer.writeTextElement(QStringLiteral("C_DOC_SUB"), QStringLiteral("001"));
    writer.writeTextElement(QStringLiteral("C_DOC_VER"), QString::fromUtf8(kUnifiedVer));
    writer.writeTextElement(QStringLiteral("C_DOC_TYPE"), QStringLiteral("0"));
    writer.writeTextElement(QStringLiteral("C_DOC_CNT"), QStringLiteral("1"));
    writer.writeTextElement(QStringLiteral("C_DOC_STAN"), QStringLiteral("1"));
    writer.writeEndElement();
    writer.writeEndElement();
  } else {
    writer.writeStartElement(QStringLiteral("LINKED_DOCS"));
    writer.writeAttribute(QStringLiteral("xsi:nil"), QStringLiteral("true"));
    writer.writeEndElement();
  }
  writer.writeTextElement(QStringLiteral("D_FILL"),
                          QDate::currentDate().toString(QStringLiteral("ddMMyyyy")));
  writer.writeTextElement(QStringLiteral("SOFTWARE"), QStringLiteral("Finik"));
  writer.writeEndElement();
}

void writeCommonBody(QXmlStreamWriter & writer, const CompanyProfile & company, const QDate & to)
{
  writer.writeTextElement(QStringLiteral("HZ"), QStringLiteral("1"));
  writer.writeTextElement(QStringLiteral("HZY"), QString::number(to.year()));
  writer.writeTextElement(QStringLiteral("HZM"), QString::number(to.month()));
  writer.writeTextElement(QStringLiteral("HNAME"), company.name);
  writer.writeTextElement(QStringLiteral("HTIN"), digits(company.edrpou));
  if (!company.address.isEmpty()) {
    writer.writeTextElement(QStringLiteral("HLOC"), company.address);
  }
  if (!company.accountant.isEmpty()) {
    writer.writeTextElement(QStringLiteral("HBOS"), company.accountant);
  }
}

void startDeclar(QXmlStreamWriter & writer, const QString & xsd)
{
  writer.writeStartDocument(QStringLiteral("1.0"), true);
  writer.writeStartElement(QStringLiteral("DECLAR"));
  writer.writeNamespace(QStringLiteral("http://www.w3.org/2001/XMLSchema-instance"),
                        QStringLiteral("xsi"));
  writer.writeAttribute(QStringLiteral("http://www.w3.org/2001/XMLSchema-instance"),
                        QStringLiteral("noNamespaceSchemaLocation"), xsd);
}

void splitPersonName(const QString & full, QString * last, QString * first, QString * middle)
{
  const QStringList parts = full.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
  *last = parts.value(0);
  *first = parts.value(1);
  *middle = parts.size() > 2 ? parts.mid(2).join(QLatin1Char(' ')) : QString();
}

Result<Money> sscBaseCapped(Money gross, int year)
{
  const Result<PayrollNorms> norms = payrollNormsForYear(year);
  if (!norms) {
    return Result<Money>::fail(norms.error());
  }
  const std::int64_t maxBase = norms.value().minWageCents * norms.value().sscMaxBaseMonths;
  if (gross.cents() > maxBase) {
    return Result<Money>::ok(Money::fromCents(maxBase));
  }
  return Result<Money>::ok(gross);
}

int calendarDaysInPeriod(const EmployeeTaxRow & emp, QDate from, QDate to)
{
  QDate start = from;
  QDate end = to;
  if (emp.hiredOn.isValid() && emp.hiredOn > start) {
    start = emp.hiredOn;
  }
  if (emp.firedOn.isValid() && emp.firedOn < end) {
    end = emp.firedOn;
  }
  if (end < start) {
    return 0;
  }
  return static_cast<int>(start.daysTo(end) + 1);
}

} // namespace

Result<QString> buildVatDeclarationXml(const VatStatement & report, const CompanyProfile & company)
{
  if (digits(company.edrpou).isEmpty()) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("у картці підприємства немає ЄДРПОУ")));
  }
  const Status taxOffice = requireTaxOffice(company);
  if (!taxOffice) {
    return Result<QString>::fail(taxOffice.error());
  }
  // Базу оподаткування рахуємо зі ставки профілю; нуль підставляти не можна —
  // це дало б 20% там, де ставка фактично не задана.
  if (company.vatRateBp <= 0 || company.vatRateBp > 10000) {
    return Result<QString>::fail(Error::validation(
        QStringLiteral("у картці підприємства не задано базову ставку ПДВ (0 < ставка ≤ 100%)")));
  }
  const int rateBp = company.vatRateBp;

  QString xml;
  QXmlStreamWriter writer(&xml);
  writer.setAutoFormatting(true);
  writer.setAutoFormattingIndent(2);
  startDeclar(writer, QStringLiteral("J0200125.xsd"));

  DeclarHeadOpts opts;
  opts.docType = QStringLiteral("J02");
  opts.docSub = QStringLiteral("001");
  opts.docVer = QStringLiteral("25");
  opts.from = report.from;
  opts.to = report.to;
  writeDeclarHead(writer, company, opts);

  writer.writeStartElement(QStringLiteral("DECLARBODY"));
  writeCommonBody(writer, company, report.to);

  auto vatBaseXml = [rateBp](std::int64_t vatCents) {
    const std::int64_t baseCents = (vatCents * 10000 + rateBp / 2) / rateBp;
    return QString::number(static_cast<double>(baseCents) / 100.0, 'f', 2);
  };
  auto vatAmountXml = [](std::int64_t vatCents) {
    return QString::number(static_cast<double>(vatCents) / 100.0, 'f', 2);
  };

  // Каркас декларації з ПДВ (рядки R011/R101/R18): повне тіло за XSD ДПС — у плані.
  if (report.liability.closingCredit.isPositive()) {
    const std::int64_t vat = report.liability.closingCredit.cents();
    writer.writeTextElement(QStringLiteral("R011G3"), vatBaseXml(vat));
    writer.writeTextElement(QStringLiteral("R011G4"), vatAmountXml(vat));
  }

  if (report.credit.closingDebit.isPositive()) {
    const std::int64_t vat = report.credit.closingDebit.cents();
    writer.writeTextElement(QStringLiteral("R101G3"), vatBaseXml(vat));
    writer.writeTextElement(QStringLiteral("R101G4"), vatAmountXml(vat));
  }

  if (report.budget.closingCredit.isPositive()) {
    writer.writeTextElement(QStringLiteral("R18G3"),
                            vatAmountXml(report.budget.closingCredit.cents()));
  }

  writer.writeEndElement();
  writer.writeEndElement();
  writer.writeEndDocument();
  return Result<QString>::ok(xml);
}

Result<QString> buildBalance1MXml(const BalanceSheet & balance, const IncomeStatement & income,
                                  const CompanyProfile & company)
{
  if (digits(company.edrpou).isEmpty()) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("у картці підприємства немає ЄДРПОУ")));
  }
  const Status taxOffice = requireTaxOffice(company);
  if (!taxOffice) {
    return Result<QString>::fail(taxOffice.error());
  }

  QString xml;
  QXmlStreamWriter writer(&xml);
  writer.setAutoFormatting(true);
  writer.setAutoFormattingIndent(2);
  startDeclar(writer, QStringLiteral("J0901107.xsd"));

  DeclarHeadOpts opts;
  opts.docType = QStringLiteral("J09");
  opts.docSub = QStringLiteral("011");
  opts.docVer = QStringLiteral("07");
  opts.from = income.from;
  opts.to = income.to;
  writeDeclarHead(writer, company, opts);

  writer.writeStartElement(QStringLiteral("DECLARBODY"));
  writeCommonBody(writer, company, income.to);

  writer.writeTextElement(QStringLiteral("R2000G3"),
                          QString::number(static_cast<double>(income.goodsRevenue.cents() +
                                                              income.serviceRevenue.cents()) /
                                              100000.0,
                                          'f', 1));
  writer.writeTextElement(
      QStringLiteral("R2050G3"),
      QString::number(static_cast<double>(income.cogs.cents()) / 100000.0, 'f', 1));
  writer.writeTextElement(
      QStringLiteral("R2120G3"),
      QString::number(static_cast<double>(income.otherIncome.cents()) / 100000.0, 'f', 1));
  writer.writeTextElement(
      QStringLiteral("R2180G3"),
      QString::number(static_cast<double>(income.admin.cents()) / 100000.0, 'f', 1));
  writer.writeTextElement(
      QStringLiteral("R2290G3"),
      QString::number(static_cast<double>(income.profit.cents()) / 100000.0, 'f', 1));
  writer.writeTextElement(
      QStringLiteral("R1300G4"),
      QString::number(static_cast<double>(balance.assetsTotal.cents()) / 100000.0, 'f', 1));
  writer.writeTextElement(QStringLiteral("R1900G4"),
                          QString::number(static_cast<double>(balance.equityTotal.cents() +
                                                              balance.liabilitiesTotal.cents()) /
                                              100000.0,
                                          'f', 1));

  writer.writeEndElement();
  writer.writeEndElement();
  writer.writeEndDocument();
  return Result<QString>::ok(xml);
}

Result<QString> buildConsolidatedTaxXml(const ConsolidatedTaxReport & report,
                                        const CompanyProfile & company)
{
  if (digits(company.edrpou).isEmpty()) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("у картці підприємства немає ЄДРПОУ")));
  }
  const Status taxOffice = requireTaxOffice(company);
  if (!taxOffice) {
    return Result<QString>::fail(taxOffice.error());
  }

  const QString prefix = formPrefix(company);
  const QString xsd = prefix + QStringLiteral("0500111.xsd");

  QString xml;
  QXmlStreamWriter writer(&xml);
  writer.setAutoFormatting(true);
  writer.setAutoFormattingIndent(2);
  startDeclar(writer, xsd);

  DeclarHeadOpts opts;
  opts.docType = prefix + QStringLiteral("05");
  opts.docSub = QStringLiteral("001");
  opts.docVer = QString::fromUtf8(kUnifiedVer);
  opts.from = report.from;
  opts.to = report.to;
  opts.periodType = 2;
  opts.periodMonth = report.quarter;
  writeDeclarHead(writer, company, opts);

  writer.writeStartElement(QStringLiteral("DECLARBODY"));
  writeCommonBody(writer, company, report.to);
  writer.writeTextElement(QStringLiteral("HKV"), QString::number(report.quarter));
  writer.writeTextElement(QStringLiteral("HNUM1"), QStringLiteral("1"));
  writer.writeTextElement(QStringLiteral("R01G1"), QString::number(report.employeeCount));
  writeMoneyTag(writer, QStringLiteral("R02G1"), report.totalGross);
  writeMoneyTag(writer, QStringLiteral("R03G1"), report.totalSsc);
  writeMoneyTag(writer, QStringLiteral("R04G1"), report.totalPit);
  writeMoneyTag(writer, QStringLiteral("R05G1"), report.totalMilitary);
  writer.writeEndElement();
  writer.writeEndElement();
  writer.writeEndDocument();

  Status check = validateDeclarStructure(xml, xsd);
  if (!check) {
    return Result<QString>::fail(check.error());
  }
  return Result<QString>::ok(xml);
}

Result<QString> buildAnnex4DfXml(const ConsolidatedTaxReport & report,
                                 const CompanyProfile & company)
{
  if (digits(company.edrpou).isEmpty()) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("у картці підприємства немає ЄДРПОУ")));
  }
  const Status taxOffice = requireTaxOffice(company);
  if (!taxOffice) {
    return Result<QString>::fail(taxOffice.error());
  }

  const QString prefix = formPrefix(company);
  const QString xsd = prefix + QStringLiteral("0510411.xsd");

  QString xml;
  QXmlStreamWriter writer(&xml);
  writer.setAutoFormatting(true);
  writer.setAutoFormattingIndent(2);
  startDeclar(writer, xsd);

  DeclarHeadOpts opts;
  opts.docType = prefix + QStringLiteral("05");
  opts.docSub = QStringLiteral("104");
  opts.docVer = QString::fromUtf8(kUnifiedVer);
  opts.from = report.from;
  opts.to = report.to;
  opts.periodType = 2;
  opts.periodMonth = report.quarter;
  opts.linkMainCalculation = true;
  writeDeclarHead(writer, company, opts);

  writer.writeStartElement(QStringLiteral("DECLARBODY"));
  writeCommonBody(writer, company, report.to);
  writer.writeTextElement(QStringLiteral("HNUM"), QStringLiteral("1"));

  int laborCount = 0;
  for (const EmployeeTaxRow & emp : report.employees) {
    if (emp.incomeSign == 101) {
      ++laborCount;
    }
  }
  writer.writeTextElement(QStringLiteral("R00G01I"), QString::number(laborCount));

  int rowNo = 0;
  for (const EmployeeTaxRow & emp : report.employees) {
    ++rowNo;
    const QString taxId = digits(emp.taxId);
    writeRowNumText(writer, QStringLiteral("T1RXXXXG02"), rowNo,
                    taxId.isEmpty() ? emp.taxId.trimmed() : taxId);

    writeRowNumMoney(writer, QStringLiteral("T1RXXXXG03A"), rowNo, emp.gross);
    writeRowNumMoney(writer, QStringLiteral("T1RXXXXG03"), rowNo, emp.paidGross);
    writeRowNumMoney(writer, QStringLiteral("T1RXXXXG04A"), rowNo, emp.pit);
    writeRowNumMoney(writer, QStringLiteral("T1RXXXXG04"), rowNo, emp.paidPit);
    writeRowNumMoney(writer, QStringLiteral("T1RXXXXG5A"), rowNo, emp.military);
    writeRowNumMoney(writer, QStringLiteral("T1RXXXXG5"), rowNo, emp.paidMilitary);
    writeRowNumText(writer, QStringLiteral("T1RXXXXG05"), rowNo, QString::number(emp.incomeSign));

    const bool hiredInPeriod =
        emp.hiredOn.isValid() && emp.hiredOn >= report.from && emp.hiredOn <= report.to;
    if (hiredInPeriod) {
      writeRowNumText(writer, QStringLiteral("T1RXXXXG06D"), rowNo,
                      emp.hiredOn.toString(QStringLiteral("dd.MM.yyyy")));
    } else {
      writeRowNumNil(writer, QStringLiteral("T1RXXXXG06D"), rowNo);
    }

    const bool firedInPeriod =
        emp.firedOn.isValid() && emp.firedOn >= report.from && emp.firedOn <= report.to;
    if (firedInPeriod) {
      writeRowNumText(writer, QStringLiteral("T1RXXXXG07D"), rowNo,
                      emp.firedOn.toString(QStringLiteral("dd.MM.yyyy")));
    } else {
      writeRowNumNil(writer, QStringLiteral("T1RXXXXG07D"), rowNo);
    }

    if (emp.hasPitBenefit) {
      writeRowNumText(writer, QStringLiteral("T1RXXXXG08"), rowNo, QStringLiteral("01"));
    } else {
      writeRowNumNil(writer, QStringLiteral("T1RXXXXG08"), rowNo);
    }
    writeRowNumText(writer, QStringLiteral("T1RXXXXG09"), rowNo, QStringLiteral("0"));
  }

  writeMoneyTag(writer, QStringLiteral("R01G03A"), report.totalGross, true);
  writeMoneyTag(writer, QStringLiteral("R01G03"), report.totalPaidGross, true);
  writeMoneyTag(writer, QStringLiteral("R01G04A"), report.totalPit, true);
  writeMoneyTag(writer, QStringLiteral("R01G04"), report.totalPaidPit, true);
  writeMoneyTag(writer, QStringLiteral("R01G5A"), report.totalMilitary, true);
  writeMoneyTag(writer, QStringLiteral("R01G5"), report.totalPaidMilitary, true);

  writer.writeEndElement();
  writer.writeEndElement();
  writer.writeEndDocument();

  Status check = validateDeclarStructure(xml, xsd);
  if (!check) {
    return Result<QString>::fail(check.error());
  }
  return Result<QString>::ok(xml);
}

Result<QString> buildAnnexEssD1Xml(const ConsolidatedTaxReport & monthly,
                                   const CompanyProfile & company)
{
  if (digits(company.edrpou).isEmpty()) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("у картці підприємства немає ЄДРПОУ")));
  }
  const Status taxOffice = requireTaxOffice(company);
  if (!taxOffice) {
    return Result<QString>::fail(taxOffice.error());
  }
  if (monthly.month < 1 || monthly.month > 12) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("Д1 потребує місячного звіту (month 1..12)")));
  }

  const QString prefix = formPrefix(company);
  const QString xsd = prefix + QStringLiteral("0510111.xsd");
  const int monthInQuarter = ((monthly.month - 1) % 3) + 1;

  QString xml;
  QXmlStreamWriter writer(&xml);
  writer.setAutoFormatting(true);
  writer.setAutoFormattingIndent(2);
  startDeclar(writer, xsd);

  DeclarHeadOpts opts;
  opts.docType = prefix + QStringLiteral("05");
  opts.docSub = QStringLiteral("101");
  opts.docVer = QString::fromUtf8(kUnifiedVer);
  opts.from = monthly.from;
  opts.to = monthly.to;
  opts.periodType = 2;
  opts.periodMonth = monthly.quarter;
  opts.linkMainCalculation = true;
  writeDeclarHead(writer, company, opts);

  writer.writeStartElement(QStringLiteral("DECLARBODY"));
  writer.writeTextElement(QStringLiteral("HZ"), QStringLiteral("1"));
  writer.writeTextElement(QStringLiteral("HZY"), QString::number(monthly.year));
  writer.writeTextElement(QStringLiteral("HZKV"), QString::number(monthly.quarter));
  writer.writeTextElement(QStringLiteral("HNM"), QString::number(monthInQuarter));
  writer.writeTextElement(QStringLiteral("HNUM1"), QStringLiteral("1"));
  writer.writeTextElement(QStringLiteral("HNAME"), company.name);
  writer.writeTextElement(QStringLiteral("HTIN"), digits(company.edrpou));

  int rowNo = 0;
  Money sum16 = Money::zero();
  Money sum17 = Money::zero();
  Money sum20 = Money::zero();
  for (const EmployeeTaxRow & emp : monthly.employees) {
    if (emp.gross.isZero() && emp.ssc.isZero()) {
      continue;
    }
    ++rowNo;
    const QString taxId = digits(emp.taxId);
    writeRowNumText(writer, QStringLiteral("T1RXXXXG5"), rowNo, QStringLiteral("1"));
    writeRowNumNil(writer, QStringLiteral("T1RXXXXG6"), rowNo);
    writeRowNumText(writer, QStringLiteral("T1RXXXXG7S"), rowNo,
                    taxId.isEmpty() ? emp.taxId.trimmed() : taxId);
    // Категорія ЗО: 1 — найманий працівник; 2 — особа з інвалідністю (пільговий ЄСВ).
    writeRowNumText(writer, QStringLiteral("T1RXXXXG8"), rowNo,
                    emp.disabledSsc ? QStringLiteral("2") : QStringLiteral("1"));
    writeRowNumText(writer, QStringLiteral("T1RXXXXG9"), rowNo, QStringLiteral("1"));
    writeRowNumText(writer, QStringLiteral("T1RXXXXG101"), rowNo, QString::number(monthly.month));
    writeRowNumText(writer, QStringLiteral("T1RXXXXG102"), rowNo, QString::number(monthly.year));

    QString last;
    QString first;
    QString middle;
    splitPersonName(emp.name, &last, &first, &middle);
    writeRowNumText(writer, QStringLiteral("T1RXXXXG111S"), rowNo, last);
    writeRowNumText(writer, QStringLiteral("T1RXXXXG112S"), rowNo, first);
    if (middle.isEmpty()) {
      writeRowNumNil(writer, QStringLiteral("T1RXXXXG113S"), rowNo);
    } else {
      writeRowNumText(writer, QStringLiteral("T1RXXXXG113S"), rowNo, middle);
    }

    writeRowNumNil(writer, QStringLiteral("T1RXXXXG12"), rowNo);
    writeRowNumNil(writer, QStringLiteral("T1RXXXXG13"), rowNo);
    writeRowNumText(writer, QStringLiteral("T1RXXXXG14"), rowNo,
                    QString::number(calendarDaysInPeriod(emp, monthly.from, monthly.to)));
    writeRowNumNil(writer, QStringLiteral("T1RXXXXG15"), rowNo);

    const Result<Money> baseRes = sscBaseCapped(emp.gross, monthly.year);
    if (!baseRes) {
      return Result<QString>::fail(baseRes.error());
    }
    const Money base = baseRes.value();
    writeRowNumMoney(writer, QStringLiteral("T1RXXXXG16"), rowNo, emp.gross);
    writeRowNumMoney(writer, QStringLiteral("T1RXXXXG17"), rowNo, base);
    writeRowNumNil(writer, QStringLiteral("T1RXXXXG18"), rowNo);
    writeRowNumMoney(writer, QStringLiteral("T1RXXXXG19"), rowNo, Money::zero());
    writeRowNumMoney(writer, QStringLiteral("T1RXXXXG20"), rowNo, emp.ssc);
    writeRowNumText(writer, QStringLiteral("T1RXXXXG21"), rowNo, QStringLiteral("1"));
    writeRowNumText(writer, QStringLiteral("T1RXXXXG22"), rowNo, QStringLiteral("0"));
    writeRowNumText(writer, QStringLiteral("T1RXXXXG23"), rowNo, QStringLiteral("0"));
    writeRowNumText(writer, QStringLiteral("T1RXXXXG24"), rowNo, QStringLiteral("0"));
    writeRowNumText(writer, QStringLiteral("T1RXXXXG25"), rowNo, QStringLiteral("0"));
    writeRowNumNil(writer, QStringLiteral("T1RXXXXG26"), rowNo);

    sum16 = Money::fromCents(sum16.cents() + emp.gross.cents());
    sum17 = Money::fromCents(sum17.cents() + base.cents());
    sum20 = Money::fromCents(sum20.cents() + emp.ssc.cents());
  }

  writeMoneyTag(writer, QStringLiteral("R01G16"), sum16, true);
  writeMoneyTag(writer, QStringLiteral("R01G17"), sum17, true);
  writeMoneyTag(writer, QStringLiteral("R01G20"), sum20, true);
  if (!company.accountant.isEmpty()) {
    writer.writeTextElement(QStringLiteral("HBOS"), company.accountant);
  }

  writer.writeEndElement();
  writer.writeEndElement();
  writer.writeEndDocument();

  Status check = validateDeclarStructure(xml, xsd);
  if (!check) {
    return Result<QString>::fail(check.error());
  }
  return Result<QString>::ok(xml);
}

Result<QString> buildAnnexD5Xml(const ConsolidatedTaxReport & report,
                                const CompanyProfile & company)
{
  if (digits(company.edrpou).isEmpty()) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("у картці підприємства немає ЄДРПОУ")));
  }
  const Status taxOffice = requireTaxOffice(company);
  if (!taxOffice) {
    return Result<QString>::fail(taxOffice.error());
  }

  const QString prefix = formPrefix(company);
  const QString xsd = prefix + QStringLiteral("0510511.xsd");

  QString xml;
  QXmlStreamWriter writer(&xml);
  writer.setAutoFormatting(true);
  writer.setAutoFormattingIndent(2);
  startDeclar(writer, xsd);

  DeclarHeadOpts opts;
  opts.docType = prefix + QStringLiteral("05");
  opts.docSub = QStringLiteral("105");
  opts.docVer = QString::fromUtf8(kUnifiedVer);
  opts.from = report.from;
  opts.to = report.to;
  opts.periodType = 2;
  opts.periodMonth = report.quarter;
  opts.linkMainCalculation = true;
  writeDeclarHead(writer, company, opts);

  writer.writeStartElement(QStringLiteral("DECLARBODY"));
  writeCommonBody(writer, company, report.to);
  writer.writeTextElement(QStringLiteral("HNUM"), QStringLiteral("1"));
  writer.writeTextElement(QStringLiteral("HZKV"), QString::number(report.quarter));

  int rowNo = 0;
  for (const EmployeeTaxRow & emp : report.employees) {
    const bool hiredIn =
        emp.hiredOn.isValid() && emp.hiredOn >= report.from && emp.hiredOn <= report.to;
    const bool firedIn =
        emp.firedOn.isValid() && emp.firedOn >= report.from && emp.firedOn <= report.to;
    if (!hiredIn && !firedIn) {
      continue;
    }
    ++rowNo;
    const QString taxId = digits(emp.taxId);
    writeRowNumText(writer, QStringLiteral("T1RXXXXG02S"), rowNo,
                    taxId.isEmpty() ? emp.taxId.trimmed() : taxId);

    QString last;
    QString first;
    QString middle;
    splitPersonName(emp.name, &last, &first, &middle);
    writeRowNumText(writer, QStringLiteral("T1RXXXXG03S"), rowNo, last);
    writeRowNumText(writer, QStringLiteral("T1RXXXXG04S"), rowNo, first);
    if (middle.isEmpty()) {
      writeRowNumNil(writer, QStringLiteral("T1RXXXXG05S"), rowNo);
    } else {
      writeRowNumText(writer, QStringLiteral("T1RXXXXG05S"), rowNo, middle);
    }

    // Дата початку: прийом у періоді або дата прийому для звільнених у періоді.
    if (emp.hiredOn.isValid() && (hiredIn || firedIn)) {
      writeRowNumText(writer, QStringLiteral("T1RXXXXG06D"), rowNo,
                      emp.hiredOn.toString(QStringLiteral("dd.MM.yyyy")));
    } else {
      writeRowNumNil(writer, QStringLiteral("T1RXXXXG06D"), rowNo);
    }

    if (firedIn) {
      writeRowNumText(writer, QStringLiteral("T1RXXXXG07D"), rowNo,
                      emp.firedOn.toString(QStringLiteral("dd.MM.yyyy")));
      // Код підстави припинення (загальний): 1 — з ініціативи працівника / інше.
      writeRowNumText(writer, QStringLiteral("T1RXXXXG08"), rowNo, QStringLiteral("1"));
    } else {
      writeRowNumNil(writer, QStringLiteral("T1RXXXXG07D"), rowNo);
      writeRowNumNil(writer, QStringLiteral("T1RXXXXG08"), rowNo);
    }

    // Ознака особливих умов (військова служба тощо) — 0 = немає.
    writeRowNumText(writer, QStringLiteral("T1RXXXXG09"), rowNo, QStringLiteral("0"));
  }

  writer.writeTextElement(QStringLiteral("R00G01I"), QString::number(rowNo));
  if (!company.accountant.isEmpty()) {
    writer.writeTextElement(QStringLiteral("HBOS"), company.accountant);
  }

  writer.writeEndElement();
  writer.writeEndElement();
  writer.writeEndDocument();

  Status check = validateDeclarStructure(xml, xsd);
  if (!check) {
    return Result<QString>::fail(check.error());
  }
  return Result<QString>::ok(xml);
}

Result<QString> buildSingleTaxDeclarationXml(const IncomeBook & book,
                                             const CompanyProfile & company, Money priorPeriodTax,
                                             Money priorMilitary)
{
  if (digits(company.edrpou).isEmpty()) {
    return Result<QString>::fail(
        Error::validation(QStringLiteral("у картці підприємства немає ЄДРПОУ")));
  }
  const Status taxOffice = requireTaxOffice(company);
  if (!taxOffice) {
    return Result<QString>::fail(taxOffice.error());
  }
  if (book.taxSystem != TaxSystem::Simplified) {
    return Result<QString>::fail(Error::validation(
        QStringLiteral("декларація ЄП формується лише для спрощеної системи оподаткування")));
  }

  const QString tin = digits(company.edrpou);
  const bool fop = isFopTin(tin);
  const QString prefix = fop ? QStringLiteral("F") : QStringLiteral("J");
  const QString docSub = fop ? QStringLiteral("033") : QStringLiteral("035");
  const QString xsd = prefix + QStringLiteral("01") + docSub + QStringLiteral("08.xsd");
  const int quarter = (book.to.month() - 1) / 3 + 1;

  QString xml;
  QXmlStreamWriter writer(&xml);
  writer.setAutoFormatting(true);
  writer.setAutoFormattingIndent(2);
  startDeclar(writer, xsd);

  DeclarHeadOpts opts;
  opts.docType = prefix + QStringLiteral("01");
  opts.docSub = docSub;
  opts.docVer = QStringLiteral("08");
  opts.from = book.from;
  opts.to = book.to;
  opts.periodType = 2;
  opts.periodMonth = quarter;
  writeDeclarHead(writer, company, opts);

  writer.writeStartElement(QStringLiteral("DECLARBODY"));
  writer.writeTextElement(QStringLiteral("HZ"), QStringLiteral("1"));
  writer.writeTextElement(QStringLiteral("HZY"), QString::number(book.to.year()));
  writer.writeTextElement(QStringLiteral("H1KV"), QString::number(quarter));
  writer.writeTextElement(QStringLiteral("HNAME"), company.name);
  writer.writeTextElement(QStringLiteral("HTIN"), tin);
  if (!company.address.isEmpty()) {
    writer.writeTextElement(QStringLiteral("HLOC"), company.address);
  }
  if (!company.accountant.isEmpty()) {
    writer.writeTextElement(QStringLiteral("HBOS"), company.accountant);
  }

  const bool rate3 = book.rateBp == 300;
  if (rate3) {
    writeMoneyTag(writer, QStringLiteral("R05G3"), book.income, true);
    writeMoneyTag(writer, QStringLiteral("R09G3"), book.tax, true);
  } else {
    writeMoneyTag(writer, QStringLiteral("R06G3"), book.income, true);
    writeMoneyTag(writer, QStringLiteral("R11G3"), book.tax, true);
  }

  writeMoneyTag(writer, QStringLiteral("R12G3"), book.tax, true);
  if (!priorPeriodTax.isZero()) {
    writeMoneyTag(writer, QStringLiteral("R13G3"), priorPeriodTax, true);
  }
  const std::int64_t payable = book.tax.cents() - priorPeriodTax.cents();
  writeMoneyTag(writer, QStringLiteral("R14G3"), Money::fromCents(payable > 0 ? payable : 0), true);

  // Військовий збір 1% з доходу (ФОП 3 група; для юрособи — за наявності в формі).
  if (fop) {
    const Money mil = Money::fromCents((book.income.cents() + 50) / 100); // 1%
    writeMoneyTag(writer, QStringLiteral("R23G3"), mil, true);
    if (!priorMilitary.isZero()) {
      writeMoneyTag(writer, QStringLiteral("R24G3"), priorMilitary, true);
    }
    const std::int64_t milPay = mil.cents() - priorMilitary.cents();
    writeMoneyTag(writer, QStringLiteral("R25G3"), Money::fromCents(milPay > 0 ? milPay : 0), true);
  }

  writer.writeEndElement();
  writer.writeEndElement();
  writer.writeEndDocument();

  Status check = validateDeclarStructure(xml, xsd);
  if (!check) {
    return Result<QString>::fail(check.error());
  }
  return Result<QString>::ok(xml);
}

Status validateDeclarStructure(const QString & xml, const QString & expectedXsdBasename)
{
  if (xml.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("порожній XML")));
  }
  QXmlStreamReader reader(xml);
  bool sawDeclar = false;
  bool sawHead = false;
  bool sawBody = false;
  QString cDoc;
  QString cSub;
  QString cVer;
  QString schemaLoc;
  bool inDeclarHead = false;
  bool inLinkedDocs = false;
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement()) {
      const auto name = reader.name();
      if (name == QLatin1String("DECLAR")) {
        sawDeclar = true;
        schemaLoc = reader.attributes()
                        .value(QStringLiteral("http://www.w3.org/2001/XMLSchema-instance"),
                               QStringLiteral("noNamespaceSchemaLocation"))
                        .toString();
      } else if (name == QLatin1String("DECLARHEAD")) {
        sawHead = true;
        inDeclarHead = true;
      } else if (name == QLatin1String("LINKED_DOCS")) {
        inLinkedDocs = true;
      } else if (name == QLatin1String("DECLARBODY")) {
        sawBody = true;
      } else if (inDeclarHead && !inLinkedDocs && name == QLatin1String("C_DOC")) {
        cDoc = reader.readElementText();
      } else if (inDeclarHead && !inLinkedDocs && name == QLatin1String("C_DOC_SUB")) {
        cSub = reader.readElementText();
      } else if (inDeclarHead && !inLinkedDocs && name == QLatin1String("C_DOC_VER")) {
        cVer = reader.readElementText();
      }
    } else if (reader.isEndElement()) {
      if (reader.name() == QLatin1String("DECLARHEAD")) {
        inDeclarHead = false;
      } else if (reader.name() == QLatin1String("LINKED_DOCS")) {
        inLinkedDocs = false;
      }
    }
  }
  if (reader.hasError()) {
    return Status::fail(Error::validation(QStringLiteral("XML: %1").arg(reader.errorString())));
  }
  if (!sawDeclar || !sawHead || !sawBody) {
    return Status::fail(
        Error::validation(QStringLiteral("немає DECLAR / DECLARHEAD / DECLARBODY")));
  }
  if (!expectedXsdBasename.isEmpty() && schemaLoc != expectedXsdBasename) {
    return Status::fail(Error::validation(
        QStringLiteral("очікувано schema %1, отримано %2").arg(expectedXsdBasename, schemaLoc)));
  }
  if (!expectedXsdBasename.isEmpty() && expectedXsdBasename.endsWith(QStringLiteral(".xsd"))) {
    const QString formId = expectedXsdBasename.left(expectedXsdBasename.size() - 4);
    const QString built = cDoc + cSub + cVer;
    if (built != formId) {
      return Status::fail(
          Error::validation(QStringLiteral("C_DOC* (%1) не відповідає %2").arg(built, formId)));
    }
  }
  return Status::ok();
}

} // namespace fin
