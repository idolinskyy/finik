#pragma once

#include "fin/session/LedgerSession.h"

#include <QWidget>

class QDateEdit;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace fin {

class NbuRateClient;

class CurrencyPage : public QWidget
{
  Q_OBJECT

 public:
  explicit CurrencyPage(LedgerSession & session, QWidget * parent = nullptr);
  void reload();

 private:
  void reloadRates();
  void addCurrency();
  void setRate();
  void removeRate();
  void calculate();
  void syncFromNbu();

  LedgerSession & session_;
  NbuRateClient * nbuClient_ = nullptr;
  QTableWidget * currencyTable_ = nullptr;
  QTableWidget * ratesTable_ = nullptr;
  QPushButton * addCurrencyBtn_ = nullptr;
  QPushButton * setRateBtn_ = nullptr;
  QPushButton * removeRateBtn_ = nullptr;
  QPushButton * syncNbuBtn_ = nullptr;

  // Калькулятор
  QLineEdit * calcForeignEdit_ = nullptr;
  QLineEdit * calcUahEdit_ = nullptr;
  QDateEdit * calcDateEdit_ = nullptr;
  QPushButton * calcToUahBtn_ = nullptr;
  QPushButton * calcToForeignBtn_ = nullptr;
};

} // namespace fin
