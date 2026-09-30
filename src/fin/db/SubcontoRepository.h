#pragma once

// Аналітика субконто.

#include "fin/core/Ids.h"
#include "fin/core/Result.h"
#include "fin/domain/Subconto.h"

#include <vector>

#include <QSqlDatabase>
#include <QString>

namespace fin {

class SubcontoRepository
{
 public:
  explicit SubcontoRepository(QSqlDatabase db);

  // Отримує повний перелік зареєстрованих видів субконто
  Result<std::vector<SubcontoKind>> listKinds() const;

  // Отримує опис виду субконто за його ідентифікатором
  Result<SubcontoKind> findKind(const QString & kindId) const;

  // Отримує список налаштованих аналітичних розрізів (до 3) для вказаного рахунку
  Result<std::vector<AccountSubconto>> listForAccount(AccountId accountId) const;

  // Замінює налаштування субконто для вказаного рахунку (потрібне право редагувати довідники)
  Status setForAccount(AccountId accountId, const std::vector<AccountSubconto> & settings);

  // Отримує назву сутності за її видом та первинним ключем (для відображення у звітах)
  Result<QString> resolveName(const QString & kindId, std::int64_t entityId) const;

  // Заповнює текстові назви для всіх непустих значень у наборі субконто
  Status resolveNames(SubcontoSet & set) const;

 private:
  QSqlDatabase db_;
};

} // namespace fin
