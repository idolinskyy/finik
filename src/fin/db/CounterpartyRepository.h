#pragma once

#include "fin/core/Result.h"
#include "fin/domain/Counterparty.h"

#include <vector>

#include <QSqlDatabase>
#include <QString>

namespace fin {

class CounterpartyRepository
{
 public:
  explicit CounterpartyRepository(QSqlDatabase db);

  Result<Counterparty> findById(CounterpartyId id) const;
  Result<Counterparty> findByEdrpou(const QString & edrpou) const;
  Result<std::vector<Counterparty>> listAll() const;
  Result<CounterpartyId> insert(const Counterparty & counterparty);
  Status update(const Counterparty & counterparty);
  Status remove(CounterpartyId id);

 private:
  QSqlDatabase db_;
};

} // namespace fin
