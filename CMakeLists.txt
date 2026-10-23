cmake_minimum_required(VERSION 3.16)

project(Finik VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Sql PrintSupport Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Sql PrintSupport Network)
find_package(Catch2 3 REQUIRED)

function(fin_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor)
  endif()
  target_compile_definitions(${target} PRIVATE
    QT_DISABLE_DEPRECATED_BEFORE=0x060000
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII)
endfunction()

set(FIN_CORE_SOURCES
  src/fin/core/Money.cpp
  src/fin/core/Quantity.cpp
  src/fin/core/Transaction.cpp
  src/fin/domain/Document.cpp
  src/fin/domain/DocumentBlueprint.cpp
  src/fin/domain/Company.cpp
  src/fin/domain/BankRequisites.cpp
  src/fin/domain/MonthClose.cpp
  src/fin/domain/ExchangeDifferenceEngine.cpp
  src/fin/domain/User.cpp
  src/fin/domain/FixedAsset.cpp
  src/fin/domain/IntangibleAsset.cpp
  src/fin/domain/LowValueItem.cpp
  src/fin/domain/Vat.cpp
  src/fin/domain/Employee.cpp
  src/fin/currency/NbuRateClient.cpp
  src/fin/edo/TaxInvoiceXml.cpp
  src/fin/edo/DeclarationXml.cpp
  src/fin/edo/PayrollTaxDigest.cpp
  src/fin/auth/Access.cpp
  src/fin/auth/Password.cpp
  src/fin/auth/SecretStore.cpp
  src/fin/bank/StatementImport.cpp
  src/fin/bank/PaymentExport.cpp
  src/fin/bank/MonobankClient.cpp
  src/fin/prro/CheckboxClient.cpp
  src/fin/db/Database.cpp
  src/fin/db/AccountRepository.cpp
  src/fin/db/CounterpartyRepository.cpp
  src/fin/db/WarehouseRepository.cpp
  src/fin/db/ContractRepository.cpp
  src/fin/db/CurrencyRepository.cpp
  src/fin/db/BomRepository.cpp
  src/fin/db/NomenclatureRepository.cpp
  src/fin/db/FixedAssetRepository.cpp
  src/fin/db/IntangibleAssetRepository.cpp
  src/fin/db/LowValueRepository.cpp
  src/fin/db/EmployeeRepository.cpp
  src/fin/db/HrOrderRepository.cpp
  src/fin/db/TimesheetRepository.cpp
  src/fin/db/DocumentRepository.cpp
  src/fin/db/LedgerRepository.cpp
  src/fin/db/SubcontoRepository.cpp
  src/fin/db/InventoryRepository.cpp
  src/fin/db/PeriodRepository.cpp
  src/fin/db/CompanyRepository.cpp
  src/fin/db/UserRepository.cpp
  src/fin/db/AuditLog.cpp
  src/fin/db/Restore.cpp
  src/fin/db/AppPaths.cpp
  src/fin/posting/DocumentValidator.cpp
  src/fin/posting/PostingEngine.cpp
  src/fin/posting/InventoryCosting.cpp
  src/fin/posting/AdvanceOffset.cpp
  src/fin/integrity/IntegrityChecker.cpp
  src/fin/reports/Reports.cpp
  src/fin/print/AmountInWords.cpp
  src/fin/print/PrintForms.cpp
  src/fin/demo/DemoSeed.cpp
  src/fin/search/GlobalSearch.cpp
  src/fin/session/LedgerSession.cpp
)

add_library(fin_core STATIC ${FIN_CORE_SOURCES})
target_include_directories(fin_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(fin_core PUBLIC Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network)
target_compile_definitions(fin_core PUBLIC FIN_VERSION="${PROJECT_VERSION}")
fin_warnings(fin_core)

set(FIN_UI_SOURCES
  src/ui/MainWindow.cpp
  src/ui/SettingsDialog.cpp
  src/ui/MainWindow.h
  src/ui/SettingsDialog.h
  src/ui/AccountPage.cpp
  src/ui/AccountPage.h
  src/ui/CounterpartyPage.cpp
  src/ui/CounterpartyPage.h
  src/ui/ContractPage.cpp
  src/ui/ContractPage.h
  src/ui/NomenclaturePage.cpp
  src/ui/NomenclaturePage.h
  src/ui/BomPage.cpp
  src/ui/BomPage.h
  src/ui/WarehousePage.cpp
  src/ui/WarehousePage.h
  src/ui/FixedAssetPage.cpp
  src/ui/FixedAssetPage.h
  src/ui/FaDialogs.cpp
  src/ui/FaDialogs.h
  src/ui/IntangiblePage.cpp
  src/ui/IntangiblePage.h
  src/ui/IaDialogs.cpp
  src/ui/IaDialogs.h
  src/ui/LowValuePage.cpp
  src/ui/LowValuePage.h
  src/ui/EmployeePage.cpp
  src/ui/EmployeePage.h
  src/ui/TimesheetPage.cpp
  src/ui/TimesheetPage.h
  src/ui/CurrencyPage.cpp
  src/ui/CurrencyPage.h
  src/ui/MsDialogs.cpp
  src/ui/MsDialogs.h
  src/ui/DocumentJournalModel.cpp
  src/ui/DocumentJournalModel.h
  src/ui/DocumentJournalPage.cpp
  src/ui/DocumentJournalPage.h
  src/ui/DocumentDialog.cpp
  src/ui/DocumentDialog.h
  src/ui/HrOrderDialog.cpp
  src/ui/HrOrderDialog.h
  src/ui/VacationDialog.cpp
  src/ui/VacationDialog.h
  src/ui/SickLeaveDialog.cpp
  src/ui/SickLeaveDialog.h
  src/ui/models/TrialBalanceModel.cpp
  src/ui/models/TrialBalanceModel.h
  src/ui/models/AccountCardModel.cpp
  src/ui/models/AccountCardModel.h
  src/ui/models/AccountAnalysisModel.cpp
  src/ui/models/AccountAnalysisModel.h
  src/ui/models/SubcontoTrialBalanceModel.cpp
  src/ui/models/SubcontoTrialBalanceModel.h
  src/ui/models/CounterpartyTurnoverModel.cpp
  src/ui/models/CounterpartyTurnoverModel.h
  src/ui/models/CounterpartyModel.cpp
  src/ui/models/CounterpartyModel.h
  src/ui/models/NomenclatureModel.cpp
  src/ui/models/NomenclatureModel.h
  src/ui/models/EmployeeModel.cpp
  src/ui/models/EmployeeModel.h
  src/ui/TrialBalancePage.cpp
  src/ui/TrialBalancePage.h
  src/ui/AccountAnalysisPage.cpp
  src/ui/AccountAnalysisPage.h
  src/ui/SubcontoTrialBalancePage.cpp
  src/ui/SubcontoTrialBalancePage.h
  src/ui/AccountCardPage.cpp
  src/ui/AccountCardPage.h
  src/ui/CounterpartyTurnoverPage.cpp
  src/ui/CounterpartyTurnoverPage.h
  src/ui/StockPage.cpp
  src/ui/StockPage.h
  src/ui/PeriodDialog.cpp
  src/ui/PeriodDialog.h
  src/ui/CompanyDialog.cpp
  src/ui/CompanyDialog.h
  src/ui/CashBookPage.cpp
  src/ui/CashBookPage.h
  src/ui/FinancialStatementPage.cpp
  src/ui/FinancialStatementPage.h
  src/ui/CashFlowPage.cpp
  src/ui/CashFlowPage.h
  src/ui/IncomeBookPage.cpp
  src/ui/IncomeBookPage.h
  src/ui/AuthDialogs.cpp
  src/ui/AuthDialogs.h
  src/ui/ReconciliationPage.cpp
  src/ui/ReconciliationPage.h
  src/ui/SearchDialog.cpp
  src/ui/SearchDialog.h
  src/ui/TaxInvoiceDialog.cpp
  src/ui/TaxInvoiceDialog.h
  src/ui/DashboardPage.cpp
  src/ui/DashboardPage.h
  src/ui/OpeningBalancesWizard.cpp
  src/ui/PayrollDialog.cpp
  src/ui/PayrollDialog.h
  src/ui/VatStatementPage.cpp
  src/ui/VatStatementPage.h
  src/ui/ConsolidatedTaxPage.cpp
  src/ui/ConsolidatedTaxPage.h
  src/ui/SalePurchaseDialogs.cpp
  src/ui/SalePurchaseDialogs.h
  src/ui/CashExpenseDialogs.cpp
  src/ui/CashExpenseDialogs.h
  src/ui/TypedDialogs.cpp
  src/ui/TypedDialogs.h
  src/ui/FormActions.cpp
  src/ui/FormActions.h
  src/ui/PdfExport.cpp
  src/ui/PdfExport.h
  src/ui/HelpBrowser.cpp
  src/ui/HelpBrowser.h
  src/ui/UiSupport.cpp
  src/ui/UiSupport.h
)

add_library(fin_ui STATIC ${FIN_UI_SOURCES})
target_include_directories(fin_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(fin_ui PUBLIC fin_core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::PrintSupport)
fin_warnings(fin_ui)

set(FIN_APP_SOURCES
  src/main.cpp
  help/help.qrc
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
  qt_add_executable(Finik MANUAL_FINALIZATION ${FIN_APP_SOURCES})
else()
  add_executable(Finik ${FIN_APP_SOURCES})
endif()

target_include_directories(Finik PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(Finik PRIVATE fin_ui)
fin_warnings(Finik)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
  qt_add_executable(fin_shots MANUAL_FINALIZATION
    src/tools/CaptureHelpShots.cpp
    help/help.qrc)
else()
  add_executable(fin_shots
    src/tools/CaptureHelpShots.cpp
    help/help.qrc)
endif()
target_include_directories(fin_shots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(fin_shots PRIVATE fin_ui)
fin_warnings(fin_shots)
set_target_properties(fin_shots PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
  WIN32_EXECUTABLE FALSE)

if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER ua.finik.app)
endif()
set_target_properties(Finik PROPERTIES
  ${BUNDLE_ID_OPTION}
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
  MACOSX_BUNDLE TRUE
  WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS Finik
  BUNDLE DESTINATION .
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE"
  DESTINATION "${CMAKE_INSTALL_DOCDIR}")

set(FINIK_ICON_PNG
  "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/hicolor/256x256/apps/finik.png")
if(WIN32)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/finik.ico"
    "${CMAKE_CURRENT_BINARY_DIR}/finik.ico"
    COPYONLY)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/finik.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/finik.rc"
    COPYONLY)
  target_sources(Finik PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/finik.rc")
endif()

if(QT_VERSION_MAJOR EQUAL 6)
  qt_finalize_executable(Finik)
  qt_finalize_executable(fin_shots)
endif()

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/finik.desktop" CONTENT
"[Desktop Entry]
Type=Application
Version=1.0
Name=Фінік
Name[en]=Finik
GenericName=Бухгалтерський облік
Exec=$<TARGET_FILE:Finik>
Icon=${FINIK_ICON_PNG}
Path=$<TARGET_FILE_DIR:Finik>
Terminal=false
StartupNotify=true
StartupWMClass=Finik
Categories=Office;Finance;Qt;
")

foreach(_fin_icon_size 16 24 32 48 64 128 256 512)
  install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/hicolor/${_fin_icon_size}x${_fin_icon_size}/apps/finik.png"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${_fin_icon_size}x${_fin_icon_size}/apps")
endforeach()
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/finik.desktop"
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")

set(CPACK_PACKAGE_NAME "finik")
set(CPACK_PACKAGE_VENDOR "Finik")
set(CPACK_PACKAGE_CONTACT "Ihor Dolinskyi")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Desktop double-entry bookkeeping for Ukrainian SMEs")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_STRIP_FILES TRUE)
if(UNIX AND NOT APPLE)
  set(CPACK_GENERATOR "DEB;TGZ")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Ihor Dolinskyi")
  set(CPACK_DEBIAN_PACKAGE_SECTION "office")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
endif()
include(CPack)

enable_testing()
add_subdirectory(tests)
