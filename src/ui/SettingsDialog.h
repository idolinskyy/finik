#pragma once

#include "fin/session/LedgerSession.h"

#include <QCalendarWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QStackedWidget>

namespace fin {

class SettingsDialog : public QDialog
{
  Q_OBJECT

 public:
  explicit SettingsDialog(LedgerSession & session, QWidget * parent = nullptr);

 private slots:
  void browseDbPath();
  void resetDbPath();
  void acceptChanges();
  void resetUiState();
  void updateCalendarPreview();

 private:
  QWidget * createGeneralPage();
  QWidget * createDatabasePage();
  QWidget * createAppearancePage();
  QWidget * createPrintPage();
  QWidget * createBackupPage();
  QWidget * createAutomationPage();
  QWidget * createAccountingPage();

  LedgerSession & session_;
  QListWidget * sidebar_;
  QStackedWidget * stack_;

  // General settings
  QComboBox * fontSizeCombo_;
  QCheckBox * confirmExitCheck_;
  QCheckBox * restoreTabsCheck_;

  // Appearance settings
  QComboBox * themeCombo_;
  QComboBox * styleCombo_;
  QCheckBox * hideSidebarCheck_;
  QComboBox * enterBehaviorCombo_;

  QComboBox * calFirstDayCombo_;
  QComboBox * calHeaderFormatCombo_;
  QCheckBox * calGridCheck_;
  QCheckBox * calWeekNumbersCheck_;
  QCheckBox * calHighlightWeekendsCheck_;
  QCheckBox * calNavVisibleCheck_;
  QCalendarWidget * calPreview_;

  // Print settings
  QCheckBox * printPageNumbersCheck_;
  QCheckBox * printRepeatHeadersCheck_;
  QSpinBox * printMarginLeftSpin_;
  QSpinBox * printMarginRightSpin_;
  QSpinBox * printMarginTopSpin_;
  QSpinBox * printMarginBottomSpin_;
  QDoubleSpinBox * printFontSpin_;
  QLineEdit * printWatermarkEdit_;
  QComboBox * printPageFilterCombo_;

  // Backup settings
  QCheckBox * suggestBackupCheck_;
  QSpinBox * autoLockSpinBox_;

  // Automation settings
  QCheckBox * syncNbuCheck_;
  QCheckBox * soundEnabledCheck_;

  // Accounting settings
  QCheckBox * allowNegativeStockCheck_;
  QLabel * accountingGateLabel_ = nullptr;
  QComboBox * prefixStrategyCombo_;
  QLineEdit * docPrefixEdit_;

  // Database settings
  QLineEdit * dbPathEdit_;
  QString originalPath_;
};

} // namespace fin
