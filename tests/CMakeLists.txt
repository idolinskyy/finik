find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Sql Network Widgets Test)

add_executable(fin_tests
  main.cpp
  test_money.cpp
  test_posting.cpp
  test_reports.cpp
  test_directories.cpp
  test_inventory.cpp
  test_integrity.cpp
  test_period.cpp
  test_costing.cpp
  test_backup.cpp
  test_forms.cpp
  test_print.cpp
  test_company.cpp
  test_demo.cpp
  test_statement.cpp
  test_users.cpp
  test_help.cpp
  test_settlements.cpp
  test_fixed_assets.cpp
  test_intangibles.cpp
  test_low_value.cpp
  test_search.cpp
  test_vat.cpp
  test_edo.cpp
  test_payroll.cpp
  test_hr.cpp
  test_subconto.cpp
  test_trade.cpp
  test_currency.cpp
  test_production.cpp
  test_expense_report.cpp
  test_monobank.cpp
  test_prro.cpp
  test_document_editors.cpp
  test_ui_directories.cpp
  test_ui_dialogs.cpp
  test_ui_models.cpp
  test_ui_documents.cpp
  test_ui_reports.cpp
  test_settings.cpp
  test_packaging.cpp
  ../help/help.qrc
)

target_include_directories(fin_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(fin_tests PRIVATE
  fin_core
  fin_ui
  Catch2::Catch2
  Qt${QT_VERSION_MAJOR}::Core
  Qt${QT_VERSION_MAJOR}::Network
  Qt${QT_VERSION_MAJOR}::Widgets
  Qt${QT_VERSION_MAJOR}::Test)

if(MSVC)
  target_compile_options(fin_tests PRIVATE /W4 /permissive-)
else()
  target_compile_options(fin_tests PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor)
endif()
target_compile_definitions(fin_tests PRIVATE
  QT_DISABLE_DEPRECATED_BEFORE=0x060000
  QT_NO_CAST_FROM_ASCII
  QT_NO_CAST_TO_ASCII
  FIN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_test(NAME fin_tests COMMAND fin_tests)
