#include "fin/domain/User.h"

#include <QLatin1String>

namespace fin {

std::optional<UserRole> userRoleFromString(const QString & text)
{
  const QString key = text.trimmed().toLower();
  if (key == QLatin1String("admin")) {
    return UserRole::Admin;
  }
  if (key == QLatin1String("accountant")) {
    return UserRole::Accountant;
  }
  if (key == QLatin1String("cashier")) {
    return UserRole::Cashier;
  }
  if (key == QLatin1String("viewer")) {
    return UserRole::Viewer;
  }
  return std::nullopt;
}

QString userRoleToString(UserRole role)
{
  switch (role) {
  case UserRole::Admin:
    return QStringLiteral("admin");
  case UserRole::Accountant:
    return QStringLiteral("accountant");
  case UserRole::Cashier:
    return QStringLiteral("cashier");
  case UserRole::Viewer:
    return QStringLiteral("viewer");
  }
  return QStringLiteral("viewer");
}

QString userRoleLabelUk(UserRole role)
{
  switch (role) {
  case UserRole::Admin:
    return QStringLiteral("адміністратор");
  case UserRole::Accountant:
    return QStringLiteral("бухгалтер");
  case UserRole::Cashier:
    return QStringLiteral("касир");
  case UserRole::Viewer:
    return QStringLiteral("перегляд");
  }
  return QStringLiteral("перегляд");
}

} // namespace fin
