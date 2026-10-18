#pragma once

// Журнал дій користувачів.

#include "fin/core/Result.h"
#include "fin/domain/User.h"

#include <vector>

#include <QSqlDatabase>
#include <QString>

namespace fin {

// Записує нову подію в журнал аудиту від імені поточного авторизованого користувача
Status appendAudit(QSqlDatabase db, const QString & action, const QString & entity,
                   std::int64_t entityId, const QString & details);

class AuditLog
{
 public:
  explicit AuditLog(QSqlDatabase db);

  // Отримує список останніх подій аудиту з можливістю фільтрації за типом дії та сутністю
  Result<std::vector<AuditEntry>> listRecent(int limit = 200, const QString & action = QString(),
                                             const QString & entity = QString()) const;

 private:
  QSqlDatabase db_;
};

} // namespace fin
