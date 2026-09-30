#include "fin/search/GlobalSearch.h"

#include "fin/core/Sql.h"
#include "fin/db/AccountRepository.h"
#include "fin/db/CounterpartyRepository.h"
#include "fin/db/EmployeeRepository.h"
#include "fin/db/FixedAssetRepository.h"
#include "fin/db/IntangibleAssetRepository.h"
#include "fin/db/LowValueRepository.h"
#include "fin/db/NomenclatureRepository.h"
#include "fin/domain/Document.h"

#include <algorithm>
#include <initializer_list>
#include <utility>

#include <QStringList>

namespace fin {
namespace {

constexpr int kMinQuery = 2;
constexpr int kPerKind = 25;

int fieldScore(const QString & field, const QString & needle)
{
  const QString text = field.trimmed();
  if (text.isEmpty()) {
    return 100;
  }
  if (text.compare(needle, Qt::CaseInsensitive) == 0) {
    return 0;
  }
  if (text.startsWith(needle, Qt::CaseInsensitive)) {
    return 1;
  }
  if (text.contains(needle, Qt::CaseInsensitive)) {
    return 2;
  }
  return 100;
}

int bestScore(const QString & needle, std::initializer_list<QString> fields)
{
  int best = 100;
  for (const QString & field : fields) {
    best = std::min(best, fieldScore(field, needle));
  }
  return best;
}

struct RankedHit
{
  SearchHit hit;
  int score = 100;
};

void takeKind(std::vector<RankedHit> & ranked, SearchKind kind, int perKind,
              std::vector<SearchHit> & out)
{
  std::vector<RankedHit> slice;
  for (const RankedHit & row : ranked) {
    if (row.hit.kind == kind) {
      slice.push_back(row);
    }
  }
  std::sort(slice.begin(), slice.end(), [](const RankedHit & a, const RankedHit & b) {
    if (a.score != b.score) {
      return a.score < b.score;
    }
    if (a.hit.date != b.hit.date) {
      return a.hit.date > b.hit.date;
    }
    return a.hit.title.localeAwareCompare(b.hit.title) < 0;
  });
  const int cap = std::min(perKind, static_cast<int>(slice.size()));
  for (int i = 0; i < cap; ++i) {
    out.push_back(slice[static_cast<std::size_t>(i)].hit);
  }
}

} // namespace

QString searchKindLabelUk(SearchKind kind)
{
  switch (kind) {
  case SearchKind::Document:
    return QStringLiteral("Документ");
  case SearchKind::Counterparty:
    return QStringLiteral("Контрагент");
  case SearchKind::Nomenclature:
    return QStringLiteral("Номенклатура");
  case SearchKind::Account:
    return QStringLiteral("Рахунок");
  case SearchKind::FixedAsset:
    return QStringLiteral("Основний засіб");
  case SearchKind::Intangible:
    return QStringLiteral("НМА");
  case SearchKind::LowValue:
    return QStringLiteral("МШП");
  case SearchKind::Employee:
    return QStringLiteral("Працівник");
  }
  return {};
}

GlobalSearch::GlobalSearch(QSqlDatabase db) : db_(std::move(db)) {}

Result<std::vector<SearchHit>> GlobalSearch::find(const QString & query, int limit) const
{
  const QString needle = query.trimmed();
  if (needle.size() < kMinQuery) {
    return Result<std::vector<SearchHit>>::ok({});
  }
  const int cap = limit > 0 ? limit : 80;
  const int perKind = std::max(1, std::min(kPerKind, cap));
  std::vector<RankedHit> ranked;

  sql::Query docs(db_);
  Status status =
      docs.exec(QStringLiteral("SELECT d.id, d.type, d.number, d.date, d.posted, "
                               "COALESCE(d.comment, ''), COALESCE(c.name, ''), "
                               "COALESCE((SELECT GROUP_CONCAT(l.description, ' ') "
                               "          FROM document_lines l WHERE l.document_id = d.id), '') "
                               "FROM documents d "
                               "LEFT JOIN counterparties c ON c.id = d.counterparty_id"));
  if (!status) {
    return Result<std::vector<SearchHit>>::fail(status.error());
  }
  while (docs.next()) {
    const QString typeId = docs.value(1).toString();
    const std::optional<DocumentType> type = documentTypeFromString(typeId);
    const QString number = docs.value(2).toString();
    const QString comment = docs.value(5).toString();
    const QString party = docs.value(6).toString();
    const QString lines = docs.value(7).toString();
    const QString typeLabel = type ? documentTypeLabelUk(*type) : typeId;
    const int score = bestScore(needle, {number, comment, party, lines, typeLabel});
    if (score >= 100) {
      continue;
    }
    RankedHit row;
    row.score = score;
    row.hit.kind = SearchKind::Document;
    row.hit.objectId = docs.value(0).toLongLong();
    row.hit.title = number;
    row.hit.date = QDate::fromString(docs.value(3).toString(), Qt::ISODate);
    const bool posted = docs.value(4).toInt() != 0;
    QStringList bits;
    bits.push_back(typeLabel);
    if (row.hit.date.isValid()) {
      bits.push_back(row.hit.date.toString(QStringLiteral("dd.MM.yyyy")));
    }
    if (!party.trimmed().isEmpty()) {
      bits.push_back(party.trimmed());
    }
    bits.push_back(posted ? QStringLiteral("проведено") : QStringLiteral("чернетка"));
    if (!comment.trimmed().isEmpty()) {
      bits.push_back(comment.trimmed());
    }
    row.hit.subtitle = bits.join(QStringLiteral(" · "));
    ranked.push_back(std::move(row));
  }

  const Result<std::vector<Counterparty>> counterparties = CounterpartyRepository(db_).listAll();
  if (!counterparties) {
    return Result<std::vector<SearchHit>>::fail(counterparties.error());
  }
  for (const Counterparty & row : counterparties.value()) {
    const int score = bestScore(needle, {row.name, row.edrpou, row.iban, row.bankName, row.notes});
    if (score >= 100) {
      continue;
    }
    RankedHit hit;
    hit.score = score;
    hit.hit.kind = SearchKind::Counterparty;
    hit.hit.objectId = row.id.value;
    hit.hit.title = row.name;
    QStringList bits;
    if (!row.edrpou.trimmed().isEmpty()) {
      bits.push_back(QStringLiteral("ЄДРПОУ %1").arg(row.edrpou.trimmed()));
    }
    if (!row.iban.trimmed().isEmpty()) {
      bits.push_back(row.iban.trimmed());
    }
    hit.hit.subtitle = bits.join(QStringLiteral(" · "));
    ranked.push_back(std::move(hit));
  }

  const Result<std::vector<Nomenclature>> items = NomenclatureRepository(db_).listAll();
  if (!items) {
    return Result<std::vector<SearchHit>>::fail(items.error());
  }
  for (const Nomenclature & row : items.value()) {
    const int score = bestScore(needle, {row.name, row.sku});
    if (score >= 100) {
      continue;
    }
    RankedHit hit;
    hit.score = score;
    hit.hit.kind = SearchKind::Nomenclature;
    hit.hit.objectId = row.id.value;
    hit.hit.title = row.name;
    QStringList bits;
    if (!row.sku.trimmed().isEmpty()) {
      bits.push_back(row.sku.trimmed());
    }
    bits.push_back(row.unit);
    if (!row.isActive) {
      bits.push_back(QStringLiteral("неактивна"));
    }
    hit.hit.subtitle = bits.join(QStringLiteral(" · "));
    ranked.push_back(std::move(hit));
  }

  const Result<std::vector<Account>> accounts = AccountRepository(db_).listAll();
  if (!accounts) {
    return Result<std::vector<SearchHit>>::fail(accounts.error());
  }
  for (const Account & row : accounts.value()) {
    const int score = bestScore(needle, {row.code, row.name});
    if (score >= 100) {
      continue;
    }
    RankedHit hit;
    hit.score = score;
    hit.hit.kind = SearchKind::Account;
    hit.hit.objectId = row.id.value;
    hit.hit.title = row.code + QStringLiteral("  ") + row.name;
    hit.hit.accountIsFolder = row.isFolder;
    hit.hit.subtitle =
        row.isFolder ? QStringLiteral("група")
                     : (row.postable() ? QStringLiteral("проводок") : QStringLiteral("неактивний"));
    ranked.push_back(std::move(hit));
  }

  const Result<std::vector<FixedAsset>> assets = FixedAssetRepository(db_).listAll();
  if (!assets) {
    return Result<std::vector<SearchHit>>::fail(assets.error());
  }
  for (const FixedAsset & row : assets.value()) {
    const int score = bestScore(needle, {row.inventoryNo, row.name});
    if (score >= 100) {
      continue;
    }
    RankedHit hit;
    hit.score = score;
    hit.hit.kind = SearchKind::FixedAsset;
    hit.hit.objectId = row.id.value;
    hit.hit.title = row.inventoryNo;
    hit.hit.subtitle = row.name + QStringLiteral(" · ") + fixedAssetStatusLabelUk(row.status);
    ranked.push_back(std::move(hit));
  }

  const Result<std::vector<IntangibleAsset>> intangibles = IntangibleAssetRepository(db_).listAll();
  if (!intangibles) {
    return Result<std::vector<SearchHit>>::fail(intangibles.error());
  }
  for (const IntangibleAsset & row : intangibles.value()) {
    const int score = bestScore(needle, {row.inventoryNo, row.name});
    if (score >= 100) {
      continue;
    }
    RankedHit hit;
    hit.score = score;
    hit.hit.kind = SearchKind::Intangible;
    hit.hit.objectId = row.id.value;
    hit.hit.title = row.inventoryNo;
    hit.hit.subtitle = row.name + QStringLiteral(" · ") + intangibleStatusLabelUk(row.status);
    ranked.push_back(std::move(hit));
  }

  const Result<std::vector<LowValueItem>> lowValue = LowValueRepository(db_).listAll();
  if (!lowValue) {
    return Result<std::vector<SearchHit>>::fail(lowValue.error());
  }
  for (const LowValueItem & row : lowValue.value()) {
    const int score = bestScore(needle, {row.inventoryNo, row.name});
    if (score >= 100) {
      continue;
    }
    RankedHit hit;
    hit.score = score;
    hit.hit.kind = SearchKind::LowValue;
    hit.hit.objectId = row.id.value;
    hit.hit.title = row.inventoryNo;
    hit.hit.subtitle = row.name;
    ranked.push_back(std::move(hit));
  }

  const Result<std::vector<Employee>> staff = EmployeeRepository(db_).listAll();
  if (!staff) {
    return Result<std::vector<SearchHit>>::fail(staff.error());
  }
  for (const Employee & row : staff.value()) {
    const int score = bestScore(needle, {row.taxId, row.name});
    if (score >= 100) {
      continue;
    }
    RankedHit hit;
    hit.score = score;
    hit.hit.kind = SearchKind::Employee;
    hit.hit.objectId = row.id.value;
    hit.hit.title = row.name;
    hit.hit.subtitle = row.taxId;
    ranked.push_back(std::move(hit));
  }

  std::vector<SearchHit> out;
  out.reserve(static_cast<std::size_t>(cap));
  const SearchKind order[] = {SearchKind::Document,     SearchKind::Counterparty,
                              SearchKind::Nomenclature, SearchKind::Account,
                              SearchKind::FixedAsset,   SearchKind::Intangible,
                              SearchKind::LowValue,     SearchKind::Employee};
  for (SearchKind kind : order) {
    takeKind(ranked, kind, perKind, out);
    if (static_cast<int>(out.size()) >= cap) {
      break;
    }
  }
  if (static_cast<int>(out.size()) > cap) {
    out.resize(static_cast<std::size_t>(cap));
  }
  return Result<std::vector<SearchHit>>::ok(std::move(out));
}

} // namespace fin
