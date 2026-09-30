#pragma once

// company_profile: один рядок (id=1) — реквізити підприємства для друку й ПДВ.

#include "fin/core/Result.h"
#include "fin/domain/Company.h"

#include <QSqlDatabase>

namespace fin {

class CompanyRepository
{
 public:
  explicit CompanyRepository(QSqlDatabase db);

  Result<CompanyProfile> load() const;
  Status save(const CompanyProfile & profile);

 private:
  QSqlDatabase db_;
};

} // namespace fin
