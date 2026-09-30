#pragma once

// Специфікації BOM (готова продукція та комплектуючі).

#include "fin/core/Result.h"
#include "fin/domain/BillOfMaterials.h"

#include <vector>

#include <QSqlDatabase>

namespace fin {

class BomRepository
{
 public:
  explicit BomRepository(QSqlDatabase db);

  Result<std::vector<BillOfMaterials>> listAll(bool activeOnly = false) const;
  Result<std::vector<BillOfMaterials>> listForProduct(NomenclatureId productId,
                                                      bool activeOnly = false) const;
  Result<BillOfMaterials> findById(BomId id) const;
  Result<BillOfMaterials> findDefault(NomenclatureId productId) const;
  Result<BomId> insert(const BillOfMaterials & bom);
  Status update(const BillOfMaterials & bom);
  Status remove(BomId id);

 private:
  Result<std::vector<BomLine>> loadLines(BomId bomId) const;

  QSqlDatabase db_;
};

} // namespace fin
