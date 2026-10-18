#pragma once

#include "fin/core/Result.h"
#include "fin/domain/Contract.h"

#include <vector>

#include <QSqlDatabase>
#include <QString>

namespace fin {

class ContractRepository
{
 public:
  explicit ContractRepository(QSqlDatabase db);

  Result<Contract> findById(ContractId id) const;
  Result<std::vector<Contract>> listByCounterparty(CounterpartyId counterpartyId,
                                                   bool activeOnly = false) const;
  Result<std::vector<Contract>> listAll(bool activeOnly = false) const;
  Result<ContractId> insert(const Contract & contract);
  Status update(const Contract & contract);
  Status remove(ContractId id);

 private:
  QSqlDatabase db_;
};

} // namespace fin
