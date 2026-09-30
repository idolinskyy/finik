#pragma once

#include "fin/core/Result.h"
#include "fin/domain/LowValueItem.h"

#include <vector>

#include <QSqlDatabase>
#include <QString>

namespace fin {

class LowValueRepository
{
 public:
  explicit LowValueRepository(QSqlDatabase db);

  Result<LowValueItem> findById(LowValueId id) const;
  Result<LowValueItem> findByInventoryNo(const QString & inventoryNo) const;
  Result<std::vector<LowValueItem>> listAll() const;
  Result<std::vector<LowValueItem>> listActive() const;
  Result<LowValueId> insert(const LowValueItem & item);
  Status update(const LowValueItem & item);
  Status remove(LowValueId id);
  Status receive(LowValueId id, Quantity quantity, Money amount);
  Status issue(LowValueId id, Quantity quantity, Money amount);
  Status revertReceive(LowValueId id, Quantity quantity, Money amount);
  Status revertIssue(LowValueId id, Quantity quantity, Money amount);

 private:
  Result<std::vector<LowValueItem>> listWhere(const QString & where) const;
  Status shiftStock(LowValueId id, Quantity quantity, Money amount, const QString & role);

  QSqlDatabase db_;
};

} // namespace fin
