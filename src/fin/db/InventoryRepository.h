#pragma once

#include "fin/core/Result.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/InventoryMovement.h"

#include <vector>

#include <QSqlDatabase>

namespace fin {

class InventoryRepository
{
 public:
  explicit InventoryRepository(QSqlDatabase db);

  Status replaceForDocument(DocumentId documentId,
                            const std::vector<InventoryMovement> & movements);
  Status removeForDocument(DocumentId documentId);
  Result<std::vector<StockBalance>>
  balances(std::optional<WarehouseId> warehouseId = std::nullopt) const;

 private:
  QSqlDatabase db_;
};

} // namespace fin
