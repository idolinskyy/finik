#pragma once

// План рахунків у БД.

#include "fin/core/Result.h"
#include "fin/domain/Account.h"

#include <vector>

#include <QSqlDatabase>
#include <QString>

namespace fin {

class AccountRepository
{
 public:
  explicit AccountRepository(QSqlDatabase db);

  Status seedChartIfEmpty();
  Result<Account> findById(AccountId id) const;
  Result<Account> findByCode(const QString & code) const;
  Result<std::vector<Account>> listAll() const;
  Result<std::vector<Account>> listPostable() const;

 private:
  QSqlDatabase db_;
};

} // namespace fin
