#pragma once

#include <QSizeF>

class QString;
class QWidget;

namespace fin::ui {

struct PrintConfig;

// Готує HTML до друку (шрифт, повтор заголовків). pageSize — область друку
// в device pixels; порожній розмір → типова A4 з відступами з config.
QString prepareHtmlForPrint(const QString & html, const PrintConfig & config,
                            const QSizeF & pageSize = QSizeF());

void printHtml(QWidget * parent, const QString & title, const QString & html);
void savePdf(QWidget * parent, const QString & suggestedName, const QString & html);
void saveCsv(QWidget * parent, const QString & suggestedName, const QString & csv);
void exportReport(QWidget * parent, const QString & title, const QString & pdfName,
                  const QString & html, const QString & csvName, const QString & csv);

} // namespace fin::ui
