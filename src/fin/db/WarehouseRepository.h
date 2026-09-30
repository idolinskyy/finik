#pragma once

#include "fin/core/Result.h"
#include "fin/domain/Warehouse.h"

#include <vector>

#include <QSqlDatabase>
#include <QString>

namespace fin {

class WarehouseRepository
{
 public:
  explicit WarehouseRepository(QSqlDatabase db);

  Result<Warehouse> findById(WarehouseId id) const;
  Result<Warehouse> findByCode(const QString & code) const;
  Result<std::vector<Warehouse>> listAll(bool activeOnly = false) const;
  Result<WarehouseId> insert(const Warehouse & warehouse);
  Status update(const Warehouse & warehouse);
  Status remove(WarehouseId id);

 private:
  QSqlDatabase db_;
};

} // namespace fin
