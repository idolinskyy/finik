#pragma once

// Запечатування секретів at rest (Monobank / Checkbox) ключем інсталяції.
// Файл finik-secrets.key у каталозі даних програми; без нього ciphertext
// з sqlite не розкривається. Це не HSM/OS keychain: хто має ключ і базу —
// читає токени. Формат v1 (pepper у бінарнику) ще розпечатується.

#include "fin/core/Result.h"

#include <QString>

namespace fin {

Result<QString> sealSecret(const QString & plain);
Result<QString> unsealSecret(const QString & stored);

} // namespace fin
