#pragma once

// Валідація документа перед проведенням.

#include "fin/core/Result.h"
#include "fin/domain/Account.h"
#include "fin/domain/Document.h"

#include <QSqlDatabase>

namespace fin {

class DocumentValidator
{
 public:
  explicit DocumentValidator(QSqlDatabase db);

  // Виконує комплексну перевірку документа залежно від його типу
  Status validate(const Document & document) const;

 private:
  Result<Account> requirePostable(AccountId id, const QString & role) const;
  Status validateOpening(const Document & document) const;
  Status validateCashIn(const Document & document) const;
  Status validateCashOut(const Document & document) const;
  Status validatePurchase(const Document & document) const;
  Status validateSale(const Document & document) const;
  Status validateBank(const Document & document) const;
  Status validateSaleReturn(const Document & document) const;
  Status validatePurchaseReturn(const Document & document) const;
  Status validateWriteOff(const Document & document) const;
  Status validateStocktake(const Document & document) const;
  Status validatePeriodResult(const Document & document) const;
  Status validateServiceSale(const Document & document) const;
  Status validateServicePurchase(const Document & document) const;
  Status validateCustomerInvoice(const Document & document) const;
  Status validateFaCommission(const Document & document) const;
  Status validateFaDepreciation(const Document & document) const;
  Status validateFaWriteOff(const Document & document) const;
  Status validateIaCommission(const Document & document) const;
  Status validateIaDepreciation(const Document & document) const;
  Status validateIaWriteOff(const Document & document) const;
  Status validateMsReceipt(const Document & document) const;
  Status validateMsIssue(const Document & document) const;
  Status validateTaxInvoice(const Document & document) const;
  Status validatePayroll(const Document & document) const;
  Status validateStockTransfer(const Document & document) const;
  Status validateKitDocument(const Document & document) const;
  Status validateMaterialIssue(const Document & document) const;
  Status validateProductionReport(const Document & document) const;
  Status validateExpenseReport(const Document & document) const;

  QSqlDatabase db_;
};

} // namespace fin
