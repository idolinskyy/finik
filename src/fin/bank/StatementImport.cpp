#include "fin/bank/StatementImport.h"

#include "fin/auth/Access.h"
#include "fin/db/AccountRepository.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/session/LedgerSession.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QXmlStreamReader>

namespace fin {
namespace {

QString fingerprint(const QDate & date, Money amount, bool incoming, const QString & purpose)
{
  return date.toString(Qt::ISODate) + QLatin1Char('|') + QString::number(amount.cents()) +
         QLatin1Char('|') + (incoming ? QLatin1Char('1') : QLatin1Char('0')) + QLatin1Char('|') +
         purpose.trimmed().toLower();
}

QDate parseDate(const QString & text)
{
  const QString trimmed = text.trimmed();
  QDate date = QDate::fromString(trimmed, Qt::ISODate);
  if (date.isValid()) {
    return date;
  }
  date = QDate::fromString(trimmed, QStringLiteral("dd.MM.yyyy"));
  if (date.isValid()) {
    return date;
  }
  return QDate::fromString(trimmed, QStringLiteral("dd/MM/yyyy"));
}

Result<Money> parseAmount(const QString & text)
{
  QString raw = text.trimmed();
  raw.replace(QLatin1Char(' '), QString());
  raw.replace(QChar::Nbsp, QString());
  if (raw.isEmpty()) {
    return Result<Money>::fail(Error::validation(QStringLiteral("порожня сума")));
  }
  return Money::fromDisplayString(raw);
}

bool looksIncoming(const QString & text, bool * known)
{
  const QString value = text.trimmed().toLower();
  if (value.isEmpty()) {
    if (known != nullptr) {
      *known = false;
    }
    return true;
  }
  if (known != nullptr) {
    *known = true;
  }
  return value == QStringLiteral("in") || value == QStringLiteral("1") ||
         value == QStringLiteral("надходження") || value == QStringLiteral("c") ||
         value == QStringLiteral("credit") || value == QStringLiteral("+");
}

QStringList splitCsvLine(const QString & line, QChar delimiter)
{
  QStringList cells;
  QString current;
  bool quoted = false;
  for (int i = 0; i < line.size(); ++i) {
    const QChar c = line.at(i);
    if (c == QLatin1Char('"')) {
      if (quoted && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
        current += QLatin1Char('"');
        ++i;
      } else {
        quoted = !quoted;
      }
      continue;
    }
    if (!quoted && c == delimiter) {
      cells.push_back(current.trimmed());
      current.clear();
      continue;
    }
    current += c;
  }
  cells.push_back(current.trimmed());
  return cells;
}

QString extractContractNumber(const QString & text)
{
  const QString source = text.trimmed();
  if (source.isEmpty()) {
    return {};
  }
  static const QRegularExpression re(
      QStringLiteral(
          R"((?:дог(?:овір|овором|овору)|contract)\s*(?:№|n[oо]\.?)?\s*([^\s,;]+))"),
      QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
  const QRegularExpressionMatch match = re.match(source);
  if (!match.hasMatch()) {
    return {};
  }
  QString number = match.captured(1).trimmed();
  while (number.endsWith(QLatin1Char('.')) || number.endsWith(QLatin1Char(')'))) {
    number.chop(1);
  }
  return number;
}

void applyContractNumber(StatementRow & item)
{
  if (item.contractNumber.trimmed().isEmpty()) {
    item.contractNumber = extractContractNumber(item.purpose);
  } else {
    item.contractNumber = item.contractNumber.trimmed();
  }
}

ContractId matchContract(LedgerSession & session, CounterpartyId counterpartyId,
                         const QString & number)
{
  if (!counterpartyId || number.trimmed().isEmpty()) {
    return {};
  }
  const Result<std::vector<Contract>> list = session.contracts().listByCounterparty(counterpartyId);
  if (!list) {
    return {};
  }
  ContractId found;
  int hits = 0;
  for (const Contract & contract : list.value()) {
    if (contract.number.compare(number.trimmed(), Qt::CaseInsensitive) == 0) {
      found = contract.id;
      ++hits;
    }
  }
  return hits == 1 ? found : ContractId{};
}

int columnIndex(const QStringList & header, const QStringList & aliases)
{
  for (int i = 0; i < header.size(); ++i) {
    const QString name = header.at(i).trimmed().toLower();
    for (const QString & alias : aliases) {
      if (name == alias) {
        return i;
      }
    }
  }
  return -1;
}

QString cell(const QStringList & row, int index)
{
  if (index < 0 || index >= row.size()) {
    return {};
  }
  return row.at(index);
}

Result<StatementParse> parseCsv(const QString & text)
{
  const QStringList rawLines =
      text.split(QRegularExpression(QStringLiteral("\\r?\\n")), Qt::SkipEmptyParts);
  if (rawLines.isEmpty()) {
    return Result<StatementParse>::fail(Error::validation(QStringLiteral("файл виписки порожній")));
  }
  QChar delimiter = QLatin1Char(';');
  if (!rawLines.front().contains(QLatin1Char(';')) && rawLines.front().contains(QLatin1Char(','))) {
    delimiter = QLatin1Char(',');
  }
  QStringList header = splitCsvLine(rawLines.front(), delimiter);
  for (QString & name : header) {
    name = name.trimmed().toLower();
  }
  const bool hasHeader =
      columnIndex(header, {QStringLiteral("date"), QStringLiteral("дата")}) >= 0 ||
      columnIndex(header, {QStringLiteral("amount"), QStringLiteral("сума")}) >= 0;
  int dateCol = 0;
  int amountCol = 1;
  int directionCol = 2;
  int purposeCol = 3;
  int edrpouCol = 4;
  int nameCol = 5;
  int contractCol = 6;
  int start = 0;
  if (hasHeader) {
    dateCol = columnIndex(
        header, {QStringLiteral("date"), QStringLiteral("дата"), QStringLiteral("transdate")});
    amountCol = columnIndex(
        header, {QStringLiteral("amount"), QStringLiteral("сума"), QStringLiteral("sum")});
    directionCol = columnIndex(header, {QStringLiteral("direction"), QStringLiteral("напрям"),
                                        QStringLiteral("incoming"), QStringLiteral("side")});
    purposeCol = columnIndex(header, {QStringLiteral("purpose"), QStringLiteral("призначення"),
                                      QStringLiteral("osnd"), QStringLiteral("comment")});
    edrpouCol = columnIndex(header, {QStringLiteral("edrpou"), QStringLiteral("єдрпоу"),
                                     QStringLiteral("okpo"), QStringLiteral("cntr_crf")});
    nameCol = columnIndex(
        header, {QStringLiteral("name"), QStringLiteral("контрагент"), QStringLiteral("party")});
    contractCol = columnIndex(header, {QStringLiteral("contract"), QStringLiteral("договір"),
                                       QStringLiteral("dogovor"), QStringLiteral("dogovir")});
    start = 1;
    if (dateCol < 0 || amountCol < 0) {
      return Result<StatementParse>::fail(
          Error::validation(QStringLiteral("у CSV потрібні колонки дати і суми")));
    }
  }
  StatementParse parsed;
  for (int i = start; i < rawLines.size(); ++i) {
    const QStringList row = splitCsvLine(rawLines.at(i), delimiter);
    if (row.size() == 1 && row.front().isEmpty()) {
      continue;
    }
    StatementRow item;
    item.date = parseDate(cell(row, dateCol));
    const Result<Money> amount = parseAmount(cell(row, amountCol));
    bool knownDirection = false;
    item.incoming = looksIncoming(cell(row, directionCol), &knownDirection);
    item.purpose = cell(row, purposeCol);
    item.edrpou = cell(row, edrpouCol).trimmed();
    item.partyName = cell(row, nameCol).trimmed();
    item.contractNumber = cell(row, contractCol).trimmed();
    applyContractNumber(item);
    if (!item.date.isValid()) {
      item.error = QStringLiteral("некоректна дата");
    } else if (!amount) {
      item.error = amount.message();
    } else if (amount.value().isZero()) {
      item.error = QStringLiteral("нульова сума");
    } else {
      Money value = amount.value();
      if (value.isNegative()) {
        item.incoming = false;
        const Result<Money> abs = value.negated();
        if (!abs) {
          item.error = abs.message();
        } else {
          value = abs.value();
        }
      } else if (!knownDirection && cell(row, directionCol).trimmed().isEmpty()) {
        item.incoming = true;
      }
      item.amount = value;
    }
    parsed.rows.push_back(item);
  }
  if (parsed.rows.empty()) {
    return Result<StatementParse>::fail(
        Error::validation(QStringLiteral("у файлі немає рядків виписки")));
  }
  return Result<StatementParse>::ok(std::move(parsed));
}

QString xmlText(QXmlStreamReader & xml)
{
  return xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
}

Result<StatementParse> parseXml(const QString & text)
{
  QXmlStreamReader xml(text);
  StatementParse parsed;
  StatementRow current;
  bool inRow = false;
  auto flush = [&]() {
    if (!inRow) {
      return;
    }
    if (!current.date.isValid() && current.amount.isZero() && current.purpose.isEmpty() &&
        current.edrpou.isEmpty()) {
      current = StatementRow{};
      inRow = false;
      return;
    }
    if (!current.date.isValid()) {
      current.error = QStringLiteral("некоректна дата");
    } else if (!current.amount.isPositive()) {
      current.error = QStringLiteral("сума має бути більшою за нуль");
    }
    applyContractNumber(current);
    parsed.rows.push_back(current);
    current = StatementRow{};
    inRow = false;
  };
  while (!xml.atEnd()) {
    const QXmlStreamReader::TokenType token = xml.readNext();
    if (token != QXmlStreamReader::StartElement) {
      continue;
    }
    const QString name = xml.name().toString().toLower();
    if (name == QStringLiteral("row") || name == QStringLiteral("statementline")) {
      flush();
      inRow = true;
      current.date = parseDate(xml.attributes().value(QStringLiteral("date")).toString());
      const QString incomingAttr = xml.attributes().value(QStringLiteral("incoming")).toString();
      bool known = false;
      current.incoming = looksIncoming(incomingAttr, &known);
      const QString amountAttr = xml.attributes().value(QStringLiteral("amount")).toString();
      if (!amountAttr.trimmed().isEmpty()) {
        const Result<Money> amount = parseAmount(amountAttr);
        if (amount) {
          Money value = amount.value();
          if (value.isNegative()) {
            current.incoming = false;
            const Result<Money> abs = value.negated();
            if (abs) {
              value = abs.value();
            }
          }
          current.amount = value;
        } else {
          current.error = amount.message();
        }
      }
      current.purpose = xml.attributes().value(QStringLiteral("purpose")).toString();
      current.edrpou = xml.attributes().value(QStringLiteral("edrpou")).toString();
      current.partyName = xml.attributes().value(QStringLiteral("name")).toString();
      current.contractNumber = xml.attributes().value(QStringLiteral("contract")).toString();
      continue;
    }
    if (!inRow) {
      continue;
    }
    const QString value = xmlText(xml);
    if (name == QStringLiteral("date") || name == QStringLiteral("transdate")) {
      current.date = parseDate(value);
    } else if (name == QStringLiteral("amount") || name == QStringLiteral("sum")) {
      const Result<Money> amount = parseAmount(value);
      if (!amount) {
        current.error = amount.message();
      } else if (amount.value().isNegative()) {
        current.incoming = false;
        const Result<Money> abs = amount.value().negated();
        current.amount = abs ? abs.value() : Money::zero();
      } else {
        current.amount = amount.value();
      }
    } else if (name == QStringLiteral("incoming") || name == QStringLiteral("direction")) {
      current.incoming = looksIncoming(value, nullptr);
    } else if (name == QStringLiteral("purpose") || name == QStringLiteral("osnd") ||
               name == QStringLiteral("comment")) {
      current.purpose = value;
    } else if (name == QStringLiteral("edrpou") || name == QStringLiteral("okpo") ||
               name == QStringLiteral("aut_cntr_crf")) {
      current.edrpou = value;
    } else if (name == QStringLiteral("name") || name == QStringLiteral("aut_cntr_nam")) {
      current.partyName = value;
    } else if (name == QStringLiteral("contract") || name == QStringLiteral("dogovor")) {
      current.contractNumber = value;
    }
  }
  flush();
  if (xml.hasError()) {
    return Result<StatementParse>::fail(
        Error::validation(QStringLiteral("XML виписки: %1").arg(xml.errorString())));
  }
  if (parsed.rows.empty()) {
    return Result<StatementParse>::fail(
        Error::validation(QStringLiteral("у XML немає рядків виписки")));
  }
  return Result<StatementParse>::ok(std::move(parsed));
}

} // namespace

Result<StatementParse> parseBankStatement(const QString & text, const QString & fileName)
{
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) {
    return Result<StatementParse>::fail(Error::validation(QStringLiteral("файл виписки порожній")));
  }
  const QString suffix = QFileInfo(fileName).suffix().toLower();
  if (suffix == QStringLiteral("xml") || trimmed.startsWith(QLatin1Char('<'))) {
    return parseXml(trimmed);
  }
  return parseCsv(trimmed);
}

Result<int> importBankStatement(LedgerSession & session, StatementParse & parsed)
{
  Status access = Access::requireImportBank(session.handle());
  if (!access) {
    return Result<int>::fail(access.error());
  }
  classifyBankStatement(session, parsed);
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  if (!accounts) {
    return Result<int>::fail(accounts.error());
  }
  int created = 0;
  for (StatementRow & row : parsed.rows) {
    if (!row.error.isEmpty() || row.duplicate) {
      continue;
    }
    BankLine line;
    line.amount = row.amount;
    line.description = row.purpose;
    const Result<std::vector<DocumentLine>> composed =
        composeBank(accounts.value(), row.incoming, {line});
    if (!composed) {
      return Result<int>::fail(composed.error());
    }
    Result<Counterparty> party = session.counterparties().findByEdrpou(row.edrpou);
    if (!party && party.error().code == ErrorCode::NotFound) {
      Counterparty partyCard;
      partyCard.name = row.partyName.trimmed();
      if (partyCard.name.isEmpty()) {
        partyCard.name = QStringLiteral("ЄДРПОУ %1").arg(row.edrpou.trimmed());
      }
      partyCard.edrpou = row.edrpou.trimmed();
      partyCard.isCustomer = true;
      partyCard.isSupplier = true;
      partyCard.notes = QStringLiteral("створено з виписки банку");
      const Result<CounterpartyId> id = session.counterparties().insert(partyCard);
      if (!id) {
        row.error = id.message();
        continue;
      }
      partyCard.id = id.value();
      party = Result<Counterparty>::ok(std::move(partyCard));
    }
    if (!party) {
      return Result<int>::fail(party.error());
    }
    Document document;
    document.type = DocumentType::BankPayment;
    document.date = row.date;
    document.counterpartyId = party.value().id;
    document.contractId = row.contractId;
    document.comment = row.purpose;
    document.lines = composed.value();
    const Result<QString> number = session.documents().allocateNumber(DocumentType::BankPayment);
    if (!number) {
      return Result<int>::fail(number.error());
    }
    document.number = number.value();
    const Result<DocumentId> id = session.documents().insert(document);
    if (!id) {
      return Result<int>::fail(id.error());
    }
    row.duplicate = true;
    ++created;
  }
  return Result<int>::ok(created);
}

void classifyBankStatement(LedgerSession & session, StatementParse & parsed)
{
  const Result<std::vector<Account>> accounts = session.accounts().listPostable();
  QSet<QString> existing;
  if (accounts) {
    const Result<std::vector<DocumentHeader>> headers =
        session.documents().list(QDate(1991, 1, 1), QDate(2100, 12, 31), DocumentType::BankPayment);
    if (headers) {
      for (const DocumentHeader & header : headers.value()) {
        const Result<Document> document = session.documents().findById(header.id);
        if (!document) {
          continue;
        }
        const std::vector<BankLine> rows =
            extractBankLines(document.value().lines, accounts.value());
        const bool incoming = bankDocumentIncoming(document.value().lines, accounts.value());
        for (const BankLine & row : rows) {
          existing.insert(
              fingerprint(document.value().date, row.amount, incoming,
                          row.description.isEmpty() ? document.value().comment : row.description));
        }
      }
    }
  }
  QSet<QString> inFile;
  for (StatementRow & row : parsed.rows) {
    if (!row.error.isEmpty()) {
      continue;
    }
    if (!row.date.isValid() || !row.amount.isPositive()) {
      row.error = QStringLiteral("некоректний рядок виписки");
      continue;
    }
    const QString key = fingerprint(row.date, row.amount, row.incoming, row.purpose);
    if (inFile.contains(key) || existing.contains(key)) {
      row.duplicate = true;
      continue;
    }
    inFile.insert(key);
    if (row.edrpou.trimmed().isEmpty()) {
      row.error = QStringLiteral("немає ЄДРПОУ — рядок пропущено");
      continue;
    }
    const Result<Counterparty> party = session.counterparties().findByEdrpou(row.edrpou);
    if (!party && party.error().code != ErrorCode::NotFound) {
      row.error = party.message();
      continue;
    }
    if (party) {
      row.contractId = matchContract(session, party.value().id, row.contractNumber);
    }
  }
}

} // namespace fin
