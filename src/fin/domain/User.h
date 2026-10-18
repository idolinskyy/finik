#pragma once

// Користувач і роль доступу.

#include "fin/core/Ids.h"

#include <cstdint>
#include <optional>

#include <QDateTime>
#include <QString>

namespace fin {

enum class UserRole
{
  Viewer,
  Cashier,
  Accountant,
  Admin
};

std::optional<UserRole> userRoleFromString(const QString & text);
QString userRoleToString(UserRole role);
QString userRoleLabelUk(UserRole role);

struct User
{
  UserId id;
  QString login;                    // логін користувача (унікальний)
  QString displayName;              // відображуване ім'я / ПІБ
  UserRole role = UserRole::Viewer; // призначена роль доступу
  bool hasPassword = false; // ознака наявності встановленого хешу пароля
  bool isActive = true; // статус активності
};

// Запис системного журналу аудиту операцій
struct AuditEntry
{
  std::int64_t id = 0;
  QDateTime at;      // дата і час події
  UserId userId;     // ідентифікатор користувача
  QString userLogin; // логін на момент виконання дії
  QString action; // дія (створення, редагування, проведення, закриття періоду тощо)
  QString entity; // сутність (document, period, company, user)
  std::int64_t entityId = 0; // ідентифікатор цільової сутності
  QString details;           // змістовний опис внесених змін
};

} // namespace fin
