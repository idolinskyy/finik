#pragma once

#include "fin/core/Result.h"
#include "fin/domain/Nomenclature.h"

#include <vector>

#include <QSqlDatabase>
#include <QString>

namespace fin {

class NomenclatureRepository
{
 public:
  explicit NomenclatureRepository(QSqlDatabase db);

  Result<Nomenclature> findById(NomenclatureId id) const;
  Result<std::vector<Nomenclature>> listAll() const;
  Result<std::vector<Nomenclature>> listActive() const;
  Result<NomenclatureId> insert(const Nomenclature & item);
  Status update(const Nomenclature & item);
  Status remove(NomenclatureId id);

 private:
  QSqlDatabase db_;
};

} // namespace fin
