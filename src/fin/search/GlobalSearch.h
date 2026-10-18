#pragma once

// Глобальний пошук по документах і довідниках.

#include "fin/core/Result.h"

#include <cstdint>
#include <vector>

#include <QDate>
#include <QSqlDatabase>
#include <QString>

namespace fin {

// Категорія знайденого об'єкта
enum class SearchKind
{
  Document,
  Counterparty,
  Nomenclature,
  Account,
  FixedAsset,
  Intangible,
  LowValue,
  Employee
};

// Елемент результату глобального пошуку
struct SearchHit
{
  SearchKind kind = SearchKind::Document; // категорія
  std::int64_t objectId = 0; // первинний ключ сутності для переходу
  QString title;             // основний заголовок
  QString subtitle;          // деталізація / реквізити
  QDate date;                // дата (для документів)
  bool accountIsFolder = false; // ознака папки для рахунку
};

QString searchKindLabelUk(SearchKind kind);

class GlobalSearch
{
 public:
  explicit GlobalSearch(QSqlDatabase db);

  // Виконує пошук за всіма сутностями з обмеженням кількості результатів
  Result<std::vector<SearchHit>> find(const QString & query, int limit = 80) const;

 private:
  QSqlDatabase db_;
};

} // namespace fin
