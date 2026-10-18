#pragma once

// Паролі: PBKDF2-HMAC-SHA256 + сіль; порівняння з постійним часом.

#include "fin/core/Result.h"

#include <QByteArray>
#include <QString>

namespace fin {

// Обчислює PBKDF2-HMAC-SHA256 з заданою сіллю і кількістю ітерацій.
// Рекомендована кількість ітерацій: не менше 100 000 для продукційногосередовища.
Result<QByteArray> pbkdf2Sha256(const QString & password, const QByteArray & salt, int iterations);
// Генерує криптостійку сіль з випадкового джерела; bytes = 16 відповідає 128-бітній ентропії.
QByteArray randomSalt(int bytes = 16);
// Порівнює хеші за алгоритмом з постійним часом (не завершується при першому відмінному байті).
bool passwordHashEquals(const QByteArray & left, const QByteArray & right);

} // namespace fin
