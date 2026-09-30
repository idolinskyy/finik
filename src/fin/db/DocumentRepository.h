#pragma once

// documents / document_lines: CRUD, автонумерація, фільтри журналу.

#include "fin/core/Result.h"
#include "fin/domain/Document.h"

#include <optional>
#include <vector>

#include <QDate>
#include <QSqlDatabase>
#include <QString>

namespace fin {

class DocumentRepository
{
 public:
  explicit DocumentRepository(QSqlDatabase db);

  Result<Document> findById(DocumentId id) const;
  Result<int> count() const;
  Result<std::vector<DocumentHeader>> list(QDate from, QDate to,
                                           std::optional<DocumentType> type = std::nullopt,
                                           CounterpartyId counterparty = {}) const;
  Result<std::vector<DocumentHeader>> listRecent(int limit) const;

  // Наступний номер за типом (наприклад «РН-000042»)
  Result<QString> allocateNumber(DocumentType type);
  Result<DocumentId> insert(const Document & document);
  Status update(const Document & document);
  // Лише непроведені чернетки
  Status remove(DocumentId id);

  // Оптимістичний контроль попереднього стану posted
  Status setPosted(DocumentId id, bool posted, bool expectedPosted);
  Status setVatRegistration(DocumentId id, QDate date);
  Status setEdoPayload(DocumentId id, const QString & xml, const QString & sha256, bool imported);
  Status setFiscalPayload(DocumentId id, const QString & receiptId, const QString & fiscalCode,
                          const QString & status);

  Result<Document> findByTypeAndNumber(DocumentType type, const QString & number) const;
  // Суми рядків після середньозваженої собівартості (лише непроведений документ)
  Status updateLineAmounts(DocumentId id, const std::vector<DocumentLine> & lines);
  // Повна заміна рядків. За замовчуванням лише непроведений документ.
  // allowPosted — лише для проведеної податкової накладної (реєстрація/скасування ЄРПН).
  Status replaceLines(DocumentId id, const std::vector<DocumentLine> & lines,
                      bool allowPosted = false);

 private:
  Result<std::vector<DocumentLine>> loadLines(DocumentId id) const;
  Result<DocumentType> typeOf(DocumentId id) const;
  // Роль має право запису для типу цього документа (касир — лише ПКО/ВКО)
  Status requireWriteAccess(DocumentId id) const;
  // Запис рядків: роль + документ ще не проведено
  Status requireWritableDraft(DocumentId id) const;

  QSqlDatabase db_;
};

} // namespace fin
