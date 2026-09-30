#pragma once

#include "fin/core/Result.h"
#include "fin/domain/FixedAsset.h"

#include <vector>

#include <QSqlDatabase>
#include <QString>

namespace fin {

class FixedAssetRepository
{
 public:
  explicit FixedAssetRepository(QSqlDatabase db);

  Result<FixedAsset> findById(FixedAssetId id) const;
  Result<FixedAsset> findByInventoryNo(const QString & inventoryNo) const;
  Result<std::vector<FixedAsset>> listAll() const;
  Result<std::vector<FixedAsset>> listDraft() const;
  Result<std::vector<FixedAsset>> listInUse() const;
  Result<FixedAssetId> insert(const FixedAsset & asset);
  Status update(const FixedAsset & asset);
  Status remove(FixedAssetId id);
  Status markCommissioned(FixedAssetId id, QDate on, Money accumulated);
  Status revertCommission(FixedAssetId id);
  Status markDisposed(FixedAssetId id, QDate on);
  Status revertDispose(FixedAssetId id);
  Status addDepreciation(FixedAssetId id, Money amount);
  Status subtractDepreciation(FixedAssetId id, Money amount);
  Result<bool> hasPostedDepreciation(FixedAssetId id) const;
  Result<bool> hasPostedWriteOff(FixedAssetId id) const;
  Result<QDate> lastPostedDepreciationDate(FixedAssetId id) const;

 private:
  Result<std::vector<FixedAsset>> listWhere(const QString & where) const;

  QSqlDatabase db_;
};

} // namespace fin
