#pragma once

// Легке «запечатування» секретів at rest (наприклад, Monobank-токен у SQLite).
// Це не HSM/OS keychain: захищає від випадкового читання дампу БД, але не від
// атаки з доступом до того ж застосунку на тій самій машині.

#include "fin/core/Result.h"

#include <QString>

namespace fin {

Result<QString> sealSecret(const QString & plain);
Result<QString> unsealSecret(const QString & stored);

} // namespace fin
