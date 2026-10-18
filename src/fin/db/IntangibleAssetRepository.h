#pragma once

#include "fin/core/Result.h"
#include "fin/domain/IntangibleAsset.h"

#include <vector>

#include <QSqlDatabase>
#include <QString>

namespace fin {

class IntangibleAssetRepository
{
 public:
  explicit IntangibleAssetRepository(QSqlDatabase db);

  Result<IntangibleAsset> findById(IntangibleId id) const;
  Result<IntangibleAsset> findByInventoryNo(const QString & inventoryNo) const;
  Result<std::vector<IntangibleAsset>> listAll() const;
  Result<std::vector<IntangibleAsset>> listDraft() const;
  Result<std::vector<IntangibleAsset>> listInUse() const;
  Result<IntangibleId> insert(const IntangibleAsset & asset);
  Status update(const IntangibleAsset & asset);
  Status remove(IntangibleId id);
  Status markCommissioned(IntangibleId id, QDate on, Money accumulated);
  Status revertCommission(IntangibleId id);
  Status markDisposed(IntangibleId id, QDate on);
  Status revertDispose(IntangibleId id);
  Status addDepreciation(IntangibleId id, Money amount);
  Status subtractDepreciation(IntangibleId id, Money amount);
  Result<bool> hasPostedDepreciation(IntangibleId id) const;
  Result<bool> hasPostedWriteOff(IntangibleId id) const;
  Result<QDate> lastPostedDepreciationDate(IntangibleId id) const;

 private:
  Result<std::vector<IntangibleAsset>> listWhere(const QString & where) const;

  QSqlDatabase db_;
};

} // namespace fin
