#pragma once

// RBAC (Admin / Accountant / Cashier / Viewer). can*() — UI; require*() — вхід операцій.

#include "fin/core/Result.h"
#include "fin/domain/Document.h"
#include "fin/domain/User.h"

#include <QSqlDatabase>
#include <QString>

namespace fin {

class Access
{
 public:
  // Повертає повну модель поточного користувача (ім'я, роль, хеш пароля).
  static Result<User> currentUser(QSqlDatabase db);
  static Result<UserRole> currentRole(QSqlDatabase db);
  // Повертає роль або Viewer за замовчуванням, якщо поточного сеансу немає.
  static UserRole currentRoleOrViewer(QSqlDatabase db);

  static bool canManageUsers(UserRole role);
  static bool canRestore(UserRole role);
  static bool canSeedDemo(UserRole role);
  static bool canBackup(UserRole role);
  static bool canEditCompany(UserRole role);
  static bool canClosePeriod(UserRole role);
  static bool canEditDirectories(UserRole role);
  static bool canImportBank(UserRole role);
  // Перевіряє чи роль дозволяє запис конкретного типу документа.
  // Касир може лише CashIn / CashOut; Viewer не може нічого.
  static bool canWriteDocument(UserRole role, DocumentType type);

  static Status requireManageUsers(QSqlDatabase db);
  static Status requireRestore(QSqlDatabase db);
  static Status requireSeedDemo(QSqlDatabase db);
  static Status requireBackup(QSqlDatabase db);
  static Status requireEditCompany(QSqlDatabase db);
  static Status requireClosePeriod(QSqlDatabase db);
  static Status requireEditDirectories(QSqlDatabase db);
  static Status requireImportBank(QSqlDatabase db);
  static Status requireDocumentWrite(QSqlDatabase db, DocumentType type);
};

} // namespace fin
