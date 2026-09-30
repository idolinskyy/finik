#pragma once

#include "fin/core/Result.h"
#include "fin/domain/Account.h"
#include "fin/session/LedgerSession.h"

#include <vector>

#include <QCalendarWidget>

class QComboBox;
class QDate;
class QLineEdit;
class QPixmap;
class QTableWidget;
class QWidget;
class QIcon;

namespace fin::ui {

struct CalendarConfig
{
  // 0 = системна локаль; інакше Qt::DayOfWeek (1 = Понеділок … 7 = Неділя).
  int firstDayOfWeek = static_cast<int>(Qt::Monday);
  bool showGrid = true;
  bool showWeekNumbers = true;
  QCalendarWidget::HorizontalHeaderFormat dayNamesFormat = QCalendarWidget::ShortDayNames;
  bool showNavigation = true;
  bool highlightWeekends = true;
};

CalendarConfig loadCalendarConfig();
void saveCalendarConfig(const CalendarConfig & config);
void applyCalendarConfig(QCalendarWidget * cal, const CalendarConfig & config);
void applyCalendarConfig(QCalendarWidget * cal);
void reapplyCalendarConfigToAll();
void installCalendarConfigFilter();

struct PrintConfig
{
  bool pageNumbers = true;
  bool repeatTableHeaders = true;
  int marginLeftMm = 8;
  int marginRightMm = 8;
  int marginTopMm = 8;
  int marginBottomMm = 8;
  double fontPt = 11.0;
  QString watermarkText;
  // 0 = усі, 1 = лише непарні, 2 = лише парні.
  int pageFilter = 0;
};

PrintConfig loadPrintConfig();
void savePrintConfig(const PrintConfig & config);

QIcon themeIcon(const QString & path);
QIcon themeIcon(const QString & path, const QColor & color);

void showError(QWidget * parent, const Error & error);
void showError(QWidget * parent, const Status & status);
void showError(QWidget * parent, const QString & message);
void showToast(QWidget * parent, const QString & message);
void applyThemeAndStyle();
void exportTableToCsv(const QTableWidget * table, QWidget * parent);
void setRowBold(QTableWidget * table, int row);

QString accountLabel(const Account & account);
void fillAccountCombo(QComboBox * combo, const std::vector<Account> & accounts,
                      AccountId selected = {});
AccountId selectedAccount(const QComboBox * combo);

void fillCounterpartyCombo(QComboBox * combo, LedgerSession & session, CounterpartyId selected = {},
                           bool optional = true);
CounterpartyId selectedCounterparty(const QComboBox * combo);

void fillNomenclatureCombo(QComboBox * combo, LedgerSession & session,
                           NomenclatureId selected = {});
NomenclatureId selectedNomenclature(const QComboBox * combo);

void fillFixedAssetCombo(QComboBox * combo, LedgerSession & session, FixedAssetId selected = {},
                         bool draftsOnly = false, bool inUseOnly = false);
FixedAssetId selectedFixedAsset(const QComboBox * combo);

void fillIntangibleCombo(QComboBox * combo, LedgerSession & session, IntangibleId selected = {},
                         bool draftsOnly = false, bool inUseOnly = false);
IntangibleId selectedIntangible(const QComboBox * combo);

void fillLowValueCombo(QComboBox * combo, LedgerSession & session, LowValueId selected = {});
LowValueId selectedLowValue(const QComboBox * combo);

void fillWarehouseCombo(QComboBox * combo, LedgerSession & session, WarehouseId selected = {},
                        bool includeAll = false);
WarehouseId selectedWarehouse(const QComboBox * combo);

void fillContractCombo(QComboBox * combo, LedgerSession & session, CounterpartyId cpId,
                       ContractId selected = {}, bool includeEmpty = true);
ContractId selectedContract(const QComboBox * combo);

void fillCurrencyCombo(QComboBox * combo, LedgerSession & session, const QString & selected = {});
QString selectedCurrencyCode(const QComboBox * combo);
QString currencyRateToDisplay(std::int64_t rateMicros, int nominal = 1);
Status currencyRateFromDisplay(const QString & text, int nominal, std::int64_t & rateMicros);
void loadCurrencyRateForDate(QLineEdit * rateEdit, LedgerSession & session, const QString & code,
                             QDate date);

void fillEmployeeCombo(QComboBox * combo, LedgerSession & session, EmployeeId selected = {},
                       bool optional = false);
EmployeeId selectedEmployee(const QComboBox * combo);

QPixmap brandLogo(int width);
QString brandNameUk();
bool selectTableRowById(QTableWidget * table, qlonglong id);

} // namespace fin::ui
