#include "fin/auth/Access.h"

#include "fin/db/UserRepository.h"

namespace fin {
namespace {

Status require(QSqlDatabase db, bool (*allowed)(UserRole), const QString & denied)
{
  const Result<UserRole> role = Access::currentRole(db);
  if (!role) {
    return role.asStatus();
  }
  if (!allowed(role.value())) {
    return Status::fail(Error::validation(denied));
  }
  return Status::ok();
}

QString roleDenied(UserRole role, const QString & action)
{
  return QStringLiteral("%1 (роль: %2)").arg(action, userRoleLabelUk(role));
}

} // namespace

Result<User> Access::currentUser(QSqlDatabase db)
{
  return UserRepository(std::move(db)).currentUser();
}

Result<UserRole> Access::currentRole(QSqlDatabase db)
{
  const Result<User> user = currentUser(std::move(db));
  if (!user) {
    return Result<UserRole>::fail(user.error());
  }
  return Result<UserRole>::ok(user.value().role);
}

UserRole Access::currentRoleOrViewer(QSqlDatabase db)
{
  const Result<UserRole> role = currentRole(std::move(db));
  return role ? role.value() : UserRole::Viewer;
}

bool Access::canManageUsers(UserRole role)
{
  return role == UserRole::Admin;
}

bool Access::canRestore(UserRole role)
{
  return role == UserRole::Admin;
}

bool Access::canSeedDemo(UserRole role)
{
  return role == UserRole::Admin;
}

bool Access::canBackup(UserRole role)
{
  return role == UserRole::Admin || role == UserRole::Accountant;
}

bool Access::canEditCompany(UserRole role)
{
  return role == UserRole::Admin || role == UserRole::Accountant;
}

bool Access::canClosePeriod(UserRole role)
{
  return role == UserRole::Admin || role == UserRole::Accountant;
}

bool Access::canEditDirectories(UserRole role)
{
  return role == UserRole::Admin || role == UserRole::Accountant;
}

bool Access::canImportBank(UserRole role)
{
  return role == UserRole::Admin || role == UserRole::Accountant;
}

bool Access::canWriteDocument(UserRole role, DocumentType type)
{
  if (role == UserRole::Admin || role == UserRole::Accountant) {
    return true;
  }
  if (role == UserRole::Cashier) {
    return type == DocumentType::CashIn || type == DocumentType::CashOut;
  }
  return false;
}

Status Access::requireManageUsers(QSqlDatabase db)
{
  return require(std::move(db), canManageUsers,
                 QStringLiteral("керувати користувачами може лише адміністратор"));
}

Status Access::requireRestore(QSqlDatabase db)
{
  return require(std::move(db), canRestore,
                 QStringLiteral("відновлення з копії доступне лише адміністратору"));
}

Status Access::requireSeedDemo(QSqlDatabase db)
{
  return require(std::move(db), canSeedDemo,
                 QStringLiteral("демо-дані може засіяти лише адміністратор"));
}

Status Access::requireBackup(QSqlDatabase db)
{
  return require(std::move(db), canBackup,
                 QStringLiteral("резервну копію може зробити бухгалтер або адміністратор"));
}

Status Access::requireEditCompany(QSqlDatabase db)
{
  return require(std::move(db), canEditCompany,
                 QStringLiteral("картку підприємства змінює бухгалтер або адміністратор"));
}

Status Access::requireClosePeriod(QSqlDatabase db)
{
  return require(std::move(db), canClosePeriod,
                 QStringLiteral("закривати період може бухгалтер або адміністратор"));
}

Status Access::requireEditDirectories(QSqlDatabase db)
{
  return require(std::move(db), canEditDirectories,
                 QStringLiteral("довідники змінює бухгалтер або адміністратор"));
}

Status Access::requireImportBank(QSqlDatabase db)
{
  return require(std::move(db), canImportBank,
                 QStringLiteral("імпорт виписки доступний бухгалтеру або адміністратору"));
}

Status Access::requireDocumentWrite(QSqlDatabase db, DocumentType type)
{
  const Result<UserRole> role = currentRole(db);
  if (!role) {
    return role.asStatus();
  }
  if (canWriteDocument(role.value(), type)) {
    return Status::ok();
  }
  if (role.value() == UserRole::Cashier) {
    return Status::fail(Error::validation(roleDenied(
        role.value(),
        QStringLiteral("касир може змінювати лише прибуткові та видаткові касові ордери"))));
  }
  return Status::fail(Error::validation(
      roleDenied(role.value(), QStringLiteral("недостатньо прав для зміни документа"))));
}

} // namespace fin
