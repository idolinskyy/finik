#include "fin/bank/PaymentExport.h"

#include "fin/domain/BankRequisites.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/session/LedgerSession.h"

#include <QStringList>

namespace fin {
namespace {

QString csvMoney(Money amount)
{
  const std::int64_t cents = amount.cents();
  const bool negative = cents < 0;
  const std::int64_t abs = negative ? -cents : cents;
  return QStringLiteral("%1%2.%3")
      .arg(negative ? QStringLiteral("-") : QString())
      .arg(abs / 100)
      .arg(abs % 100, 2, 10, QLatin1Char('0'));
}

QString csvEscape(QString value)
{
  value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
  if (value.contains(QLatin1Char(';')) || value.contains(QLatin1Char('"')) ||
      value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r'))) {
    return QLatin1Char('"') + value + QLatin1Char('"');
  }
  return value;
}

QString paymentPurpose(const Document & document)
{
  if (!document.comment.trimmed().isEmpty()) {
    return document.comment.trimmed();
  }
  QStringList parts;
  for (const DocumentLine & line : document.lines) {
    const QString text = line.description.trimmed();
    if (!text.isEmpty()) {
      parts.push_back(text);
    }
  }
  if (!parts.isEmpty()) {
    return parts.join(QStringLiteral("; "));
  }
  return QStringLiteral("Оплата за док. %1").arg(document.number);
}

} // namespace

Result<PaymentExportResult> exportOutgoingPaymentsCsv(LedgerSession & session, QDate from, QDate to)
{
  if (!from.isValid() || !to.isValid() || from > to) {
    return Result<PaymentExportResult>::fail(
        Error::validation(QStringLiteral("некоректний період експорту платежів")));
  }

  const Result<CompanyProfile> company = session.company().load();
  if (!company) {
    return Result<PaymentExportResult>::fail(company.error());
  }
  const QString payerIban = normalizeIban(company.value().iban);
  if (payerIban.isEmpty()) {
    return Result<PaymentExportResult>::fail(Error::validation(
        QStringLiteral("у картці підприємства немає IBAN — потрібен для клієнт-банку")));
  }
  const Status payerOk = validateIban(payerIban);
  if (!payerOk) {
    return Result<PaymentExportResult>::fail(payerOk.error());
  }
  const QString payerEdrpou = company.value().edrpou.trimmed();

  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  if (!accounts) {
    return Result<PaymentExportResult>::fail(accounts.error());
  }

  const Result<std::vector<DocumentHeader>> headers =
      session.documents().list(from, to, DocumentType::BankPayment);
  if (!headers) {
    return Result<PaymentExportResult>::fail(headers.error());
  }

  PaymentExportResult out;
  QStringList lines;
  lines << QStringLiteral(
      "ND;Data;SUMMA;CUR_TAG;N_P;DB_IBAN;CR_IBAN;KL_OKP;KL_OKP_K;KL_NM_K;MFO_K");

  for (const DocumentHeader & header : headers.value()) {
    const Result<Document> document = session.documents().findById(header.id);
    if (!document) {
      out.skipped.push_back(
          {header.number, header.date,
           QStringLiteral("не вдалося завантажити документ: %1").arg(document.message())});
      continue;
    }
    if (bankDocumentIncoming(document.value().lines, accounts.value())) {
      continue;
    }
    if (!document.value().counterpartyId) {
      out.skipped.push_back(
          {document.value().number, document.value().date,
           QStringLiteral("немає контрагента-одержувача")});
      continue;
    }
    const Result<Counterparty> party =
        session.counterparties().findById(document.value().counterpartyId);
    if (!party) {
      out.skipped.push_back({document.value().number, document.value().date,
                             QStringLiteral("контрагента не знайдено")});
      continue;
    }
    const QString payeeIban = normalizeIban(party.value().iban);
    if (payeeIban.isEmpty()) {
      out.skipped.push_back(
          {document.value().number, document.value().date,
           QStringLiteral("у контрагента «%1» немає IBAN").arg(party.value().name)});
      continue;
    }
    const Status payeeOk = validateIban(payeeIban);
    if (!payeeOk) {
      out.skipped.push_back({document.value().number, document.value().date,
                             QStringLiteral("IBAN одержувача: %1").arg(payeeOk.message())});
      continue;
    }
    const Money total = document.value().total();
    if (!total.isPositive()) {
      out.skipped.push_back(
          {document.value().number, document.value().date, QStringLiteral("сума не додатна")});
      continue;
    }

    const QString mfo = mfoFromIban(payeeIban);
    const QString purpose = paymentPurpose(document.value());
    const QString row =
        QStringList{csvEscape(document.value().number),
                    document.value().date.toString(QStringLiteral("dd.MM.yyyy")), csvMoney(total),
                    QStringLiteral("UAH"), csvEscape(purpose), payerIban, payeeIban,
                    csvEscape(payerEdrpou), csvEscape(party.value().edrpou.trimmed()),
                    csvEscape(party.value().name.trimmed()), mfo}
            .join(QLatin1Char(';'));
    lines << row;
    ++out.exported;
  }

  if (out.exported == 0 && out.skipped.empty()) {
    return Result<PaymentExportResult>::fail(
        Error::validation(QStringLiteral("за період немає вихідних банківських платежів")));
  }

  out.csv = lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
  return Result<PaymentExportResult>::ok(std::move(out));
}

} // namespace fin
