#include "fin/db/UserRepository.h"

#include "fin/auth/Access.h"
#include "fin/auth/Password.h"
#include "fin/core/Sql.h"
#include "fin/db/AuditLog.h"

#include <QVariant>

namespace fin {
namespace {

User userFromQuery(sql::Query & query)
{
  User user;
  user.id = UserId{query.value(0).toLongLong()};
  user.login = query.value(1).toString();
  user.displayName = query.value(2).toString();
  user.hasPassword = !query.value(3).toByteArray().isEmpty();
  user.isActive = query.value(4).toInt() != 0;
  user.role = userRoleFromString(query.value(5).toString()).value_or(UserRole::Viewer);
  return user;
}

const QString kSelect =
    QStringLiteral("SELECT id, login, display_name, password_hash, is_active, role FROM users");

Status validateUser(const User & user)
{
  if (user.login.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("логін обов'язковий")));
  }
  if (user.displayName.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("ім'я користувача обов'язкове")));
  }
  return Status::ok();
}

Result<int> countActiveAdmins(QSqlDatabase db)
{
  sql::Query query(db);
  Status status = query.exec(
      QStringLiteral("SELECT COUNT(*) FROM users WHERE role = 'admin' AND is_active = 1"));
  if (!status) {
    return Result<int>::fail(status.error());
  }
  if (!query.next()) {
    return Result<int>::ok(0);
  }
  return Result<int>::ok(query.value(0).toInt());
}

Status ensureNotLastAdmin(QSqlDatabase db, const User & existing, const User & updated)
{
  const bool wasAdmin = existing.role == UserRole::Admin && existing.isActive;
  const bool staysAdmin = updated.role == UserRole::Admin && updated.isActive;
  if (!wasAdmin || staysAdmin) {
    return Status::ok();
  }
  const Result<int> count = countActiveAdmins(db);
  if (!count) {
    return count.asStatus();
  }
  if (count.value() <= 1) {
    return Status::fail(
        Error::conflict(QStringLiteral("не можна зняти останнього адміністратора")));
  }
  return Status::ok();
}

} // namespace

UserRepository::UserRepository(QSqlDatabase db) : db_(std::move(db)) {}

Result<User> UserRepository::findById(UserId id) const
{
  if (!id) {
    return Result<User>::fail(Error::invalidArgument(QStringLiteral("порожній id користувача")));
  }
  sql::Query query(db_);
  Status status = query.prepare(kSelect + QStringLiteral(" WHERE id = :id"));
  if (!status) {
    return Result<User>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    return Result<User>::fail(status.error());
  }
  if (!query.next()) {
    return Result<User>::fail(Error::notFound(QStringLiteral("користувача не знайдено")));
  }
  return Result<User>::ok(userFromQuery(query));
}

Result<User> UserRepository::findByLogin(const QString & login) const
{
  const QString key = login.trimmed();
  if (key.isEmpty()) {
    return Result<User>::fail(Error::validation(QStringLiteral("логін обов'язковий")));
  }
  sql::Query query(db_);
  Status status = query.prepare(kSelect + QStringLiteral(" WHERE login = :login"));
  if (!status) {
    return Result<User>::fail(status.error());
  }
  query.bind(QStringLiteral(":login"), key);
  status = query.exec();
  if (!status) {
    return Result<User>::fail(status.error());
  }
  if (!query.next()) {
    return Result<User>::fail(Error::notFound(QStringLiteral("користувача не знайдено")));
  }
  return Result<User>::ok(userFromQuery(query));
}

Result<std::vector<User>> UserRepository::listAll() const
{
  sql::Query query(db_);
  Status status = query.exec(kSelect + QStringLiteral(" ORDER BY login COLLATE NOCASE"));
  if (!status) {
    return Result<std::vector<User>>::fail(status.error());
  }
  std::vector<User> rows;
  while (query.next()) {
    rows.push_back(userFromQuery(query));
  }
  return Result<std::vector<User>>::ok(std::move(rows));
}

Result<User> UserRepository::currentUser() const
{
  sql::Query query(db_);
  Status status = query.exec(QStringLiteral("SELECT user_id FROM app_context WHERE id = 1"));
  if (!status) {
    return Result<User>::fail(status.error());
  }
  if (!query.next() || query.value(0).isNull()) {
    return Result<User>::fail(Error::validation(
        QStringLiteral("поточний користувач не встановлений — увійдіть у систему")));
  }
  return findById(UserId{query.value(0).toLongLong()});
}

Status UserRepository::setCurrentUser(UserId id)
{
  const Result<User> user = findById(id);
  if (!user) {
    return user.asStatus();
  }
  if (!user.value().isActive) {
    return Status::fail(Error::validation(QStringLiteral("користувач неактивний")));
  }
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("UPDATE app_context SET user_id = :id WHERE id = 1"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  return query.exec();
}

Status UserRepository::clearCurrentUser()
{
  sql::Query query(db_);
  return query.exec(QStringLiteral("UPDATE app_context SET user_id = NULL WHERE id = 1"));
}

Result<UserId> UserRepository::insert(const User & user)
{
  Status access = Access::requireManageUsers(db_);
  if (!access) {
    return Result<UserId>::fail(access.error());
  }
  access = validateUser(user);
  if (!access) {
    return Result<UserId>::fail(access.error());
  }
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("INSERT INTO users(login, display_name, role, is_active) "
                                   "VALUES(:login, :name, :role, :active)"));
  if (!status) {
    return Result<UserId>::fail(status.error());
  }
  query.bind(QStringLiteral(":login"), user.login.trimmed());
  query.bind(QStringLiteral(":name"), user.displayName.trimmed());
  query.bind(QStringLiteral(":role"), userRoleToString(user.role));
  query.bind(QStringLiteral(":active"), user.isActive ? 1 : 0);
  status = query.exec();
  if (!status) {
    if (status.message().contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive)) {
      return Result<UserId>::fail(Error::conflict(QStringLiteral("логін уже використовується")));
    }
    return Result<UserId>::fail(status.error());
  }
  const UserId id{query.lastInsertId()};
  status = appendAudit(db_, QStringLiteral("insert"), QStringLiteral("user"), id.value,
                       user.login.trimmed() + QLatin1Char(' ') + userRoleLabelUk(user.role));
  if (!status) {
    return Result<UserId>::fail(status.error());
  }
  return Result<UserId>::ok(id);
}

Status UserRepository::update(const User & user)
{
  Status access = Access::requireManageUsers(db_);
  if (!access) {
    return access;
  }
  if (!user.id) {
    return Status::fail(Error::invalidArgument(QStringLiteral("порожній id користувача")));
  }
  access = validateUser(user);
  if (!access) {
    return access;
  }
  const Result<User> existing = findById(user.id);
  if (!existing) {
    return existing.asStatus();
  }
  access = ensureNotLastAdmin(db_, existing.value(), user);
  if (!access) {
    return access;
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("UPDATE users SET login = :login, display_name = :name, role = :role, "
                     "is_active = :active WHERE id = :id"));
  if (!status) {
    return status;
  }
  query.bind(QStringLiteral(":login"), user.login.trimmed());
  query.bind(QStringLiteral(":name"), user.displayName.trimmed());
  query.bind(QStringLiteral(":role"), userRoleToString(user.role));
  query.bind(QStringLiteral(":active"), user.isActive ? 1 : 0);
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(user.id.value));
  status = query.exec();
  if (!status) {
    if (status.message().contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive)) {
      return Status::fail(Error::conflict(QStringLiteral("логін уже використовується")));
    }
    return status;
  }
  return appendAudit(db_, QStringLiteral("update"), QStringLiteral("user"), user.id.value,
                     user.login.trimmed() + QLatin1Char(' ') + userRoleLabelUk(user.role));
}

Result<bool> UserRepository::passwordRequired() const
{
  // Логін потрібен лише коли хоча б один активний користувач має пароль.
  // Кількість користувачів сама по собі не вмикає LoginDialog — інакше
  // демо-користувачі без хеша блокують вхід назавжди.
  sql::Query query(db_);
  Status status = query.exec(
      QStringLiteral("SELECT 1 FROM users WHERE is_active = 1 AND password_hash IS NOT NULL "
                     "AND length(password_hash) > 0 LIMIT 1"));
  if (!status) {
    return Result<bool>::fail(status.error());
  }
  return Result<bool>::ok(query.next());
}

Status UserRepository::setPassword(UserId id, const QString & password)
{
  const Result<User> user = findById(id);
  if (!user) {
    return user.asStatus();
  }
  const Result<User> current = currentUser();
  if (!current) {
    return current.asStatus();
  }
  if (current.value().id != id) {
    const Status access = Access::requireManageUsers(db_);
    if (!access) {
      return access;
    }
  }
  if (password.isEmpty() && user.value().role == UserRole::Admin) {
    return Status::fail(
        Error::validation(QStringLiteral("не можна зняти пароль у адміністратора")));
  }
  sql::Query query(db_);
  Status status =
      query.prepare(QStringLiteral("UPDATE users SET password_hash = :hash, password_salt = :salt, "
                                   "iterations = :iter WHERE id = :id"));
  if (!status) {
    return status;
  }
  if (password.isEmpty()) {
    query.bind(QStringLiteral(":hash"), QVariant());
    query.bind(QStringLiteral(":salt"), QVariant());
    query.bind(QStringLiteral(":iter"), 100000);
  } else if (password.size() < 6) {
    return Status::fail(
        Error::validation(QStringLiteral("пароль має містити щонайменше 6 символів")));
  } else {
    const QByteArray salt = randomSalt();
    const Result<QByteArray> hash = pbkdf2Sha256(password, salt, 100000);
    if (!hash) {
      return hash.asStatus();
    }
    query.bind(QStringLiteral(":hash"), hash.value());
    query.bind(QStringLiteral(":salt"), salt);
    query.bind(QStringLiteral(":iter"), 100000);
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(id.value));
  status = query.exec();
  if (!status) {
    return status;
  }
  return appendAudit(db_, QStringLiteral("password"), QStringLiteral("user"), id.value,
                     password.isEmpty() ? QStringLiteral("пароль знято")
                                        : QStringLiteral("пароль змінено"));
}

Result<User> UserRepository::authenticate(const QString & login, const QString & password) const
{
  const Result<User> user = findByLogin(login);
  if (!user || !user.value().isActive) {
    const QByteArray dummySalt(16, '\0');
    (void)pbkdf2Sha256(password.isEmpty() ? QStringLiteral("x") : password, dummySalt, 100000);
    return Result<User>::fail(Error::validation(QStringLiteral("невірний логін або пароль")));
  }
  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("SELECT password_hash, password_salt, iterations FROM users WHERE id = :id"));
  if (!status) {
    return Result<User>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), static_cast<qlonglong>(user.value().id.value));
  status = query.exec();
  if (!status) {
    return Result<User>::fail(status.error());
  }
  if (!query.next()) {
    return Result<User>::fail(Error::notFound(QStringLiteral("користувача не знайдено")));
  }
  const QByteArray stored = query.value(0).toByteArray();
  const QByteArray salt = query.value(1).toByteArray();
  const int iterations = query.value(2).toInt();
  if (stored.isEmpty() || salt.isEmpty() || iterations < 1000) {
    return Result<User>::fail(
        Error::validation(QStringLiteral("для цього користувача потрібно встановити пароль")));
  }
  const Result<QByteArray> computed = pbkdf2Sha256(password, salt, iterations);
  if (!computed) {
    return Result<User>::fail(computed.error());
  }
  if (!passwordHashEquals(stored, computed.value())) {
    return Result<User>::fail(Error::validation(QStringLiteral("невірний логін або пароль")));
  }
  return Result<User>::ok(user.value());
}

} // namespace fin
