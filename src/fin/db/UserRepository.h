#pragma once

// users: хеш пароля, ролі, поточний сеанс.

#include "fin/core/Result.h"
#include "fin/domain/User.h"

#include <vector>

#include <QSqlDatabase>
#include <QString>

namespace fin {

class UserRepository
{
 public:
  explicit UserRepository(QSqlDatabase db);

  Result<User> findById(UserId id) const;
  Result<User> findByLogin(const QString & login) const;
  Result<std::vector<User>> listAll() const;
  Result<User> currentUser() const;
  Status setCurrentUser(UserId id);
  Status clearCurrentUser();
  Result<bool> passwordRequired() const;
  Result<UserId> insert(const User & user);
  Status update(const User & user);
  Status setPassword(UserId id, const QString & password);
  Result<User> authenticate(const QString & login, const QString & password) const;

 private:
  QSqlDatabase db_;
};

} // namespace fin
