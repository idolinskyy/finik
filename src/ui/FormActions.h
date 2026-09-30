#pragma once

#include "fin/core/Result.h"
#include "fin/domain/Document.h"
#include "fin/session/LedgerSession.h"

#include <functional>

class QLabel;
class QPushButton;

namespace fin::ui {

struct FormButtons
{
  QPushButton * save = nullptr;
  QPushButton * post = nullptr;
  QPushButton * unpost = nullptr;
  QPushButton * print = nullptr;
  QLabel * reason = nullptr;
};

void applyFormButtons(const FormButtons & bar, bool posted, const Status & saveReady,
                      const Status & postReady, const Status & unpostReady);

void refreshDocumentButtons(LedgerSession & session, const FormButtons & bar, bool posted,
                            QDate currentDate, const std::function<Status(Document &)> & collect);

} // namespace fin::ui
