#pragma once

#include <QWizard>
#include <QDate>
#include "fin/session/LedgerSession.h"

class QDateEdit;
class QTableWidget;
class QComboBox;

namespace fin {

class OpeningBalancesWizard : public QWizard
{
  Q_OBJECT

 public:
  explicit OpeningBalancesWizard(LedgerSession & session, QWidget * parent = nullptr);

  void accept() override;

 private:
  LedgerSession & session_;

  QDateEdit * dateEdit_ = nullptr;
  QTableWidget * moneyTable_ = nullptr;
  QTableWidget * inventoryTable_ = nullptr;
  QTableWidget * debtTable_ = nullptr;

  void setupIntroPage();
  void setupMoneyPage();
  void setupInventoryPage();
  void setupDebtPage();

  QWizardPage * createIntroPage();
  QWizardPage * createMoneyPage();
  QWizardPage * createInventoryPage();
  QWizardPage * createDebtPage();

  void addMoneyRow();
  void addInventoryRow();
  void addDebtRow();
};

} // namespace fin
