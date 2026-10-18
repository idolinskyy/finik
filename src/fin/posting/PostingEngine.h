#pragma once

// Проведення: подвійний запис у ledger_entries в одній SQLite-транзакції.

#include "fin/core/Result.h"
#include "fin/core/Ids.h"

#include <QDate>
#include <QSqlDatabase>

namespace fin {

class PostingEngine
{
 public:
  explicit PostingEngine(QSqlDatabase db);

  // Проводить документ: формує рядки Головної книги та складські рухи.
  // Перед записом:
  //   1. Перевіряє права доступу поточного користувача.
  //   2. Відхиляє CustomerInvoice (рахунок на оплату не проводиться).
  //   3. Перевіряє, що дата документа не потрапляє у закритий місяць.
  //   4. Запускає DocumentValidator для перевірки кореспонденцій.
  //   5. Обчислює середньозважену собівартість товарів (InventoryCosting).
  //   6. Автоматично додає рядки заліку авансів (681↔361, 371↔631).
  //   7. Для ПН з датою реєстрації — додає рядки 643→641 або 644→641.
  Status post(DocumentId documentId);

  // Знімає проведення: видаляє рядки Головної книги та складські рухи.
  // Перед операцією перевіряє конфлікти:
  //   – для Payroll: чи є проведений документ виплати зарплати
  //   – для будь-якого документа: чи є проведена ПН «на підставі»
  //   – для ОЗ: чи є проведений акт списання (FaWriteOff)
  Status unpost(DocumentId documentId);

  // Реєструє вже проведену ПН в ЄРПН (встановлює vatRegistrationDate).
  // Додає до рядків документа проводку 643→641 (вихідна ПН) або
  // 644→641 (вхідна ПН) датою реєстрації, що може відрізнятися від
  // дати документа (при міжмісячній реєстрації).
  Status registerTaxInvoice(DocumentId documentId, QDate registeredOn);

  // Скасовує реєстрацію ПН в ЄРПН: видаляє рядки 643/644→641.
  // Дата реєстрації в документа очищується, Головна книга оновлюється.
  Status unregisterTaxInvoice(DocumentId documentId);

 private:
  QSqlDatabase db_;
};

} // namespace fin
