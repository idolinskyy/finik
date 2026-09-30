#include "ui/FormActions.h"

#include "fin/auth/Access.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/posting/DocumentValidator.h"

#include <QLabel>
#include <QPushButton>

namespace fin::ui {

void applyFormButtons(const FormButtons & bar, bool posted, const Status & saveReady,
                      const Status & postReady, const Status & unpostReady)
{
  if (bar.save != nullptr) {
    bar.save->setEnabled(!posted && bool(saveReady));
    bar.save->setToolTip(
        !posted && saveReady
            ? QString()
            : (posted ? QStringLiteral("проведений документ не змінюється") : saveReady.message()));
  }
  if (bar.post != nullptr) {
    bar.post->setEnabled(!posted && bool(postReady));
    bar.post->setToolTip(!posted && postReady ? QString()
                                              : (posted ? QStringLiteral("документ уже проведено")
                                                        : postReady.message()));
  }
  if (bar.unpost != nullptr) {
    bar.unpost->setEnabled(posted && bool(unpostReady));
    bar.unpost->setToolTip(
        posted && unpostReady
            ? QString()
            : (posted ? unpostReady.message() : QStringLiteral("документ не проведено")));
  }
  if (bar.print != nullptr) {
    const bool printable = posted || bool(saveReady);
    bar.print->setEnabled(printable);
    bar.print->setToolTip(printable ? QString() : (saveReady ? QString() : saveReady.message()));
  }
  if (bar.reason == nullptr) {
    return;
  }
  if (posted) {
    bar.reason->setText(unpostReady ? QStringLiteral("Проведено. Поля заблоковано.")
                                    : unpostReady.message());
    return;
  }
  if (!postReady) {
    QString text = postReady.message();
    if (saveReady) {
      text += QStringLiteral(" Чернетку можна зберегти.");
    }
    bar.reason->setText(text);
    return;
  }
  bar.reason->setText(QStringLiteral("Можна зберегти й провести."));
}

void refreshDocumentButtons(LedgerSession & session, const FormButtons & bar, bool posted,
                            QDate currentDate, const std::function<Status(Document &)> & collect)
{
  Document collected;
  Status collectedOk = collect(collected);
  Status dataSave = collectedOk;
  if (dataSave) {
    if (collected.number.trimmed().isEmpty()) {
      dataSave = Status::fail(Error::validation(QStringLiteral("номер документа обов'язковий")));
    } else {
      dataSave = session.period().ensureDateWritable(collected.date);
    }
  }
  Status saveReady = dataSave;
  if (saveReady) {
    const Status access = Access::requireDocumentWrite(session.handle(), collected.type);
    if (!access) {
      saveReady = access;
    }
  }
  Status postReady = saveReady;
  if (postReady) {
    if (!requiresLedgerPosting(collected.type)) {
      postReady = Status::fail(Error::validation(
          QStringLiteral("рахунок на оплату не проводиться — лише збереження і друк")));
    } else {
      postReady = DocumentValidator(session.handle()).validate(collected);
    }
  }
  Status unpostReady = Status::fail(Error::validation(QStringLiteral("документ не проведено")));
  if (posted) {
    unpostReady = session.period().ensureDateWritable(currentDate);
    if (unpostReady && collectedOk) {
      const Status access = Access::requireDocumentWrite(session.handle(), collected.type);
      if (!access) {
        unpostReady = access;
      }
    }
  }
  applyFormButtons(bar, posted, saveReady, postReady, unpostReady);
  if (bar.print == nullptr) {
    return;
  }
  const bool printable = posted || bool(dataSave);
  bar.print->setEnabled(printable);
  bar.print->setToolTip(printable ? QString() : (dataSave ? QString() : dataSave.message()));
}

} // namespace fin::ui
