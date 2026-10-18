#pragma once

#include "fin/edo/PayrollTaxDigest.h"
#include "fin/session/LedgerSession.h"

#include <QWidget>

class QComboBox;
class QSpinBox;
class QLabel;
class QTableWidget;

namespace fin {

class ConsolidatedTaxPage : public QWidget
{
  Q_OBJECT

 public:
  explicit ConsolidatedTaxPage(LedgerSession & session, QWidget * parent = nullptr);

  void rebuild();

 private:
  void doExportMain();
  void doExport4Df();
  void doExportD1();
  void doExportD5();
  bool ensureReport();
  bool saveXmlFile(const QString & title, const QString & suggestedName, const QString & xml);

  LedgerSession & session_;
  QSpinBox * year_ = nullptr;
  QComboBox * quarter_ = nullptr;

  QLabel * lblGross_ = nullptr;
  QLabel * lblPit_ = nullptr;
  QLabel * lblMilitary_ = nullptr;
  QLabel * lblSsc_ = nullptr;
  QLabel * lblPaid_ = nullptr;
  QLabel * lblCount_ = nullptr;
  QTableWidget * table_ = nullptr;

  ConsolidatedTaxReport report_;
};

} // namespace fin
