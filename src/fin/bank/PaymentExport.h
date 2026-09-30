#pragma once

// Експорт вихідних банківських платежів у файл для імпорту в клієнт-банк
// (CSV у стилі iBank / iTiny: Приват24 для бізнесу Автоклієнт, Ощадбанк тощо).

#include "fin/core/Result.h"

#include <vector>

#include <QDate>
#include <QString>

namespace fin {

class LedgerSession;

struct PaymentExportSkip
{
  QString number;
  QDate date;
  QString reason;
};

struct PaymentExportResult
{
  QString csv; // готовий текст файлу (UTF-8, ;-роздільник)
  int exported = 0;
  std::vector<PaymentExportSkip> skipped;
};

// Збирає вихідні BankPayment за період і будує CSV для завантаження в клієнт-банк.
// Вхідні платежі пропускаються. Без IBAN підприємства / одержувача — у skipped.
Result<PaymentExportResult> exportOutgoingPaymentsCsv(LedgerSession & session, QDate from,
                                                      QDate to);

} // namespace fin
