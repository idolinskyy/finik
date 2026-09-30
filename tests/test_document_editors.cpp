#include <catch2/catch_test_macros.hpp>

#include "fin/domain/Document.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/Employee.h"

#include <QSet>

using namespace fin;

namespace {

DocumentEditorKind expectedEditor(DocumentType type)
{
  // Явна таблиця очікувань — окремо від documentEditorKind(), щоб регресія
  // «відпускні → зарплата» знову не прослизнула через копіпасту порядку if.
  switch (type) {
  case DocumentType::Purchase:
  case DocumentType::Sale:
  case DocumentType::SaleReturn:
  case DocumentType::PurchaseReturn:
  case DocumentType::WriteOff:
  case DocumentType::Stocktake:
  case DocumentType::ServiceSale:
  case DocumentType::CustomerInvoice:
  case DocumentType::StockTransfer:
  case DocumentType::KitAssembly:
  case DocumentType::KitDisassembly:
  case DocumentType::CustomerOrder:
  case DocumentType::SupplierOrder:
  case DocumentType::MaterialIssue:
  case DocumentType::ProductionReport:
    return DocumentEditorKind::Goods;
  case DocumentType::CashIn:
  case DocumentType::CashOut:
    return DocumentEditorKind::Cash;
  case DocumentType::BankPayment:
    return DocumentEditorKind::Bank;
  case DocumentType::ServicePurchase:
    return DocumentEditorKind::ServicePurchase;
  case DocumentType::OpeningBalance:
    return DocumentEditorKind::Opening;
  case DocumentType::FaCommission:
    return DocumentEditorKind::FaCommission;
  case DocumentType::FaDepreciation:
    return DocumentEditorKind::FaDepreciation;
  case DocumentType::FaWriteOff:
    return DocumentEditorKind::FaWriteOff;
  case DocumentType::IaCommission:
    return DocumentEditorKind::IaCommission;
  case DocumentType::IaDepreciation:
    return DocumentEditorKind::IaDepreciation;
  case DocumentType::IaWriteOff:
    return DocumentEditorKind::IaWriteOff;
  case DocumentType::MsReceipt:
  case DocumentType::MsIssue:
    return DocumentEditorKind::Ms;
  case DocumentType::TaxInvoice:
    return DocumentEditorKind::TaxInvoice;
  case DocumentType::VacationPayroll:
    return DocumentEditorKind::VacationPayroll;
  case DocumentType::SickLeavePayroll:
    return DocumentEditorKind::SickLeavePayroll;
  case DocumentType::Payroll:
    return DocumentEditorKind::Payroll;
  case DocumentType::ExpenseReport:
    return DocumentEditorKind::ExpenseReport;
  case DocumentType::Manual:
  case DocumentType::PeriodResult:
    return DocumentEditorKind::Generic;
  }
  return DocumentEditorKind::Generic;
}

} // namespace

TEST_CASE("documentEditorKind routes every registered DocumentType", "[documents][editors]")
{
  const std::vector<DocumentType> types = allDocumentTypes();
  REQUIRE(types.size() >= 30);

  QSet<QString> seenIds;
  for (DocumentType type : types) {
    const QString id = documentTypeToString(type);
    REQUIRE_FALSE(id.isEmpty());
    REQUIRE_FALSE(seenIds.contains(id));
    seenIds.insert(id);

    const DocumentEditorKind got = documentEditorKind(type);
    const DocumentEditorKind want = expectedEditor(type);
    INFO(documentTypeLabelUk(type).toStdString()
         << " (" << id.toStdString() << ") -> "
         << documentEditorKindLabel(got).toStdString() << " expected "
         << documentEditorKindLabel(want).toStdString());
    REQUIRE(got == want);
  }
}

TEST_CASE("vacation and sick leave must not open payroll editor", "[documents][editors]")
{
  REQUIRE(documentEditorKind(DocumentType::VacationPayroll) ==
          DocumentEditorKind::VacationPayroll);
  REQUIRE(documentEditorKind(DocumentType::SickLeavePayroll) ==
          DocumentEditorKind::SickLeavePayroll);
  REQUIRE(documentEditorKind(DocumentType::Payroll) == DocumentEditorKind::Payroll);

  REQUIRE_FALSE(usesPayrollForm(DocumentType::VacationPayroll));
  REQUIRE_FALSE(usesPayrollForm(DocumentType::SickLeavePayroll));
  REQUIRE(usesPayrollForm(DocumentType::Payroll));

  // Родина для друку / довідки лишається спільною.
  REQUIRE(isPayrollFamily(DocumentType::VacationPayroll));
  REQUIRE(isPayrollFamily(DocumentType::SickLeavePayroll));
  REQUIRE(isPayrollFamily(DocumentType::Payroll));
}

TEST_CASE("documentEditorKind is unique mapping for specialized payroll forms",
          "[documents][editors]")
{
  // Якщо хтось знову розширить usesPayrollForm на відпускні — цей тест падає,
  // навіть якщо порядок if у openDocument «випадково» правильний.
  REQUIRE(documentEditorKind(DocumentType::VacationPayroll) != DocumentEditorKind::Payroll);
  REQUIRE(documentEditorKind(DocumentType::SickLeavePayroll) != DocumentEditorKind::Payroll);
}
