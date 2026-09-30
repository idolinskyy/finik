#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include <QAction>
#include <QCursor>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QIODevice>
#include <QMarginsF>
#include <QMenu>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QRegularExpression>
#include <QTextDocument>
#include <QWidget>

namespace fin::ui {
namespace {

bool landscapeFromHtml(const QString & html)
{
  return html.contains(QStringLiteral("name=\"fin-page\" content=\"landscape\""));
}

qreal mmToPx(qreal mm)
{
  return mm * 96.0 / 25.4;
}

QSizeF pageSizeForConfig(bool landscape, const PrintConfig & config)
{
  qreal bottomMm = static_cast<qreal>(config.marginBottomMm);
  if (config.pageNumbers) {
    bottomMm = qMax(bottomMm, 12.0);
  }
  const qreal pageWmm = landscape ? 297.0 : 210.0;
  const qreal pageHmm = landscape ? 210.0 : 297.0;
  const qreal w = mmToPx(pageWmm - config.marginLeftMm - config.marginRightMm);
  qreal h = mmToPx(pageHmm - config.marginTopMm - bottomMm);
  // Смуга під номер сторінки всередині printable area.
  if (config.pageNumbers) {
    h -= mmToPx(5.0);
  }
  return QSizeF(w, qMax(h, 120.0));
}

qreal pageNumberBandPx(const PrintConfig & config)
{
  return config.pageNumbers ? mmToPx(5.0) : 0.0;
}

QSizeF contentPageSize(const QSizeF & printerPageSize, const PrintConfig & config)
{
  const qreal band = pageNumberBandPx(config);
  if (band <= 0) {
    return printerPageSize;
  }
  QSizeF size = printerPageSize;
  size.setHeight(qMax(120.0, printerPageSize.height() - band));
  return size;
}

void applyPaper(QPrinter & printer, bool landscape, const PrintConfig & config)
{
  // CSS px ≈ 1/96 in. HighResolution (≈1200 dpi) treats 12pt as 12 device
  // pixels, so the table sits in a stamp-sized box with huge empty margins.
  printer.setResolution(96);
  qreal bottomMm = static_cast<qreal>(config.marginBottomMm);
  if (config.pageNumbers) {
    bottomMm = qMax(bottomMm, 12.0);
  }
  QPageLayout layout(
      QPageSize(QPageSize::A4), landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
      QMarginsF(config.marginLeftMm, config.marginTopMm, config.marginRightMm, bottomMm),
      QPageLayout::Millimeter);
  printer.setPageLayout(layout);
}

QStringList extractTableRows(const QString & inner)
{
  QStringList rows;
  static const QRegularExpression rowRe(QStringLiteral("<tr\\b[^>]*>[\\s\\S]*?</tr>"),
                                        QRegularExpression::CaseInsensitiveOption);
  QRegularExpressionMatchIterator it = rowRe.globalMatch(inner);
  while (it.hasNext()) {
    rows.append(it.next().captured());
  }
  return rows;
}

bool rowHasTh(const QString & row)
{
  return row.contains(QStringLiteral("<th"), Qt::CaseInsensitive);
}

bool isSheetTable(const QString & attrs)
{
  return attrs.contains(QStringLiteral("sheet"), Qt::CaseInsensitive);
}

QString extractHeadInner(const QString & html)
{
  static const QRegularExpression headRe(QStringLiteral("<head[^>]*>([\\s\\S]*?)</head>"),
                                         QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch match = headRe.match(html);
  if (match.hasMatch()) {
    return match.captured(1);
  }
  return {};
}

QString wrapHtmlBody(const QString & headInner, const QString & bodyInner)
{
  return QStringLiteral("<!DOCTYPE html><html lang=\"uk\"><head>%1</head>"
                        "<body>%2</body></html>")
      .arg(headInner, bodyInner);
}

int documentPageCount(const QString & html, const QSizeF & pageSize)
{
  QTextDocument document;
  document.setDocumentMargin(0);
  document.setHtml(html);
  document.setPageSize(pageSize);
  return qMax(1, document.pageCount());
}

QString pageBreakMarker()
{
  return QStringLiteral("<p style=\"page-break-before:always;margin:0;padding:0;"
                        "font-size:1px;line-height:1px;\">&nbsp;</p>");
}

QString buildTableHtml(const QString & openAttrs, const QString & header, const QStringList & body,
                       int offset, int count)
{
  QString out = QStringLiteral("<table%1>").arg(openAttrs);
  out += header;
  const int end = qMin(offset + count, body.size());
  for (int i = offset; i < end; ++i) {
    out += body.at(i);
  }
  out += QStringLiteral("</table>");
  return out;
}

// Максимум рядків body[offset..), щоб preamble+table зайняли не більше
// maxPages сторінок (зазвичай 1).
int maxRowsFitting(const QString & headInner, const QString & preamble, const QString & openAttrs,
                   const QString & header, const QStringList & body, int offset,
                   const QSizeF & pageSize, int maxPages)
{
  const int remaining = static_cast<int>(body.size() - offset);
  if (remaining <= 0) {
    return 0;
  }

  auto pagesForCount = [&](int count) {
    const QString bodyHtml = preamble + buildTableHtml(openAttrs, header, body, offset, count);
    return documentPageCount(wrapHtmlBody(headInner, bodyHtml), pageSize);
  };

  if (pagesForCount(remaining) <= maxPages) {
    return remaining;
  }

  int lo = 1;
  int hi = remaining;
  int best = 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    if (pagesForCount(mid) <= maxPages) {
      best = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return best;
}

QString splitTableMeasured(const QString & headInner, const QString & preamble,
                           const QString & openAttrs, const QString & inner,
                           const QSizeF & pageSize)
{
  const QStringList rows = extractTableRows(inner);
  if (rows.isEmpty() || !rowHasTh(rows.first()) || rows.size() <= 1) {
    return preamble + QStringLiteral("<table%1>%2</table>").arg(openAttrs, inner);
  }

  const QString header = rows.first();
  QStringList body;
  body.reserve(rows.size() - 1);
  for (int i = 1; i < rows.size(); ++i) {
    body.append(rows.at(i));
  }

  QString out = preamble;
  int offset = 0;
  bool first = true;
  while (offset < body.size()) {
    // Наступні фрагменти міряємо БЕЗ page-break маркера: інакше
    // page-break-before дає pageCount≥2 і бінарний пошук лишає 1 рядок.
    const QString measurePreamble = first ? preamble : QString();
    int maxPages = 1;
    if (first && !preamble.trimmed().isEmpty()) {
      maxPages = documentPageCount(wrapHtmlBody(headInner, preamble), pageSize);
    }

    int count = maxRowsFitting(headInner, measurePreamble, openAttrs, header, body, offset,
                               pageSize, maxPages);
    if (count < 1) {
      count = 1;
    }

    if (!first) {
      // Якщо попередній фрагмент уже заповнив сторінку до кінця, примусовий
      // page-break дає порожню сторінку. Break потрібен лише коли на останній
      // сторінці ще є місце (інакше заголовок опиниться посередині).
      const QString probe =
          QStringLiteral("<p style=\"margin:0;padding:0;font-size:1px;line-height:1px;\">x</p>");
      const int pagesNow = documentPageCount(wrapHtmlBody(headInner, out), pageSize);
      const int pagesWithProbe = documentPageCount(wrapHtmlBody(headInner, out + probe), pageSize);
      if (pagesWithProbe <= pagesNow) {
        out += pageBreakMarker();
      }
    }
    out += buildTableHtml(openAttrs, header, body, offset, count);
    offset += count;
    first = false;
  }
  return out;
}

QString repeatTableHeadersMeasured(const QString & html, const QSizeF & pageSize)
{
  const QString headInner = extractHeadInner(html);
  static const QRegularExpression bodyRe(QStringLiteral("<body[^>]*>([\\s\\S]*?)</body>"),
                                         QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch bodyMatch = bodyRe.match(html);
  if (!bodyMatch.hasMatch()) {
    return html;
  }
  const QString body = bodyMatch.captured(1);

  static const QRegularExpression tableRe(QStringLiteral("<table(\\b[^>]*)>([\\s\\S]*?)</table>"),
                                          QRegularExpression::CaseInsensitiveOption);

  QString rebuilt;
  int last = 0;
  QRegularExpressionMatchIterator it = tableRe.globalMatch(body);
  while (it.hasNext()) {
    const QRegularExpressionMatch match = it.next();
    const QString preamble = body.mid(last, match.capturedStart() - last);
    const QString attrs = match.captured(1);
    const QString inner = match.captured(2);
    if (isSheetTable(attrs)) {
      rebuilt += preamble;
      rebuilt += match.captured(0);
    } else {
      rebuilt += splitTableMeasured(headInner, preamble, attrs, inner, pageSize);
    }
    last = static_cast<int>(match.capturedEnd());
  }
  rebuilt += body.mid(last);

  QString result = html;
  result.replace(bodyMatch.capturedStart(1), bodyMatch.capturedLength(1), rebuilt);
  return result;
}

double effectiveFontPt(const PrintConfig & config, bool landscape)
{
  if (landscape) {
    return qMax(8.0, config.fontPt - 1.5);
  }
  return qBound(8.0, config.fontPt, 14.0);
}

QString injectBodyFontSize(QString html, double fontPt)
{
  static const QRegularExpression bodyFontRe(
      QStringLiteral("(body\\{[^}]*font-size:)([\\d.]+)(pt)"),
      QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch match = bodyFontRe.match(html);
  if (!match.hasMatch()) {
    return html;
  }
  html.replace(match.capturedStart(2), match.capturedLength(2), QString::number(fontPt, 'f', 1));
  return html;
}

bool pageIncluded(int pageOneBased, int filter)
{
  if (filter == 1) {
    return (pageOneBased % 2) == 1;
  }
  if (filter == 2) {
    return (pageOneBased % 2) == 0;
  }
  return true;
}

void drawWatermark(QPainter & painter, const QRectF & pageRect, const QString & text)
{
  if (text.trimmed().isEmpty()) {
    return;
  }
  painter.save();
  painter.setOpacity(0.18);
  QFont font = painter.font();
  font.setPointSizeF(qMax(28.0, qMin(pageRect.width(), pageRect.height()) / 10.0));
  font.setBold(true);
  painter.setFont(font);
  painter.setPen(QColor(80, 80, 80));
  painter.translate(pageRect.center());
  painter.rotate(-45.0);
  const QRectF band(-pageRect.width(), -pageRect.height() / 2.0, pageRect.width() * 2.0,
                    pageRect.height());
  painter.drawText(band, Qt::AlignCenter, text.trimmed());
  painter.restore();
}

void paintHtml(QPrinter & printer, const QString & html)
{
  const PrintConfig config = loadPrintConfig();
  const QSizeF pageSize = printer.pageRect(QPrinter::DevicePixel).size();
  const QSizeF contentSize = contentPageSize(pageSize, config);
  const QString prepared = prepareHtmlForPrint(html, config, contentSize);

  QTextDocument document;
  document.setDocumentMargin(0);
  document.setHtml(prepared);
  document.setPageSize(contentSize);

  const int pageCount = document.pageCount();
  if (pageCount <= 0) {
    return;
  }

  QPainter painter(&printer);
  bool firstPainted = true;
  for (int i = 0; i < pageCount; ++i) {
    if (!pageIncluded(i + 1, config.pageFilter)) {
      continue;
    }
    if (!firstPainted) {
      printer.newPage();
    }
    firstPainted = false;

    painter.save();
    const QRectF contentRect(QPointF(0, 0), contentSize);
    painter.setClipRect(contentRect);
    painter.translate(0, -i * contentSize.height());
    const QRectF view(0, i * contentSize.height(), contentSize.width(), contentSize.height());
    document.drawContents(&painter, view);
    painter.restore();

    const QRectF pageRect(QPointF(0, 0), pageSize);
    drawWatermark(painter, pageRect, config.watermarkText);

    if (config.pageNumbers) {
      const QRectF footer(0, contentSize.height(), pageSize.width(),
                          pageSize.height() - contentSize.height());
      painter.drawText(footer.adjusted(0, 0, -4, 0), Qt::AlignRight | Qt::AlignVCenter,
                       QStringLiteral("%1/%2").arg(i + 1).arg(pageCount));
    }
  }
}

} // namespace

QString prepareHtmlForPrint(const QString & html, const PrintConfig & config,
                            const QSizeF & pageSize)
{
  const bool landscape = landscapeFromHtml(html);
  QString prepared = html;
  prepared = injectBodyFontSize(prepared, effectiveFontPt(config, landscape));
  if (!config.repeatTableHeaders) {
    return prepared;
  }
  const QSizeF size = pageSize.isValid() && pageSize.width() > 0 && pageSize.height() > 0
                          ? pageSize
                          : pageSizeForConfig(landscape, config);
  return repeatTableHeadersMeasured(prepared, size);
}

void printHtml(QWidget * parent, const QString & title, const QString & html)
{
  const PrintConfig config = loadPrintConfig();
  QPrinter printer(QPrinter::ScreenResolution);
  applyPaper(printer, landscapeFromHtml(html), config);
  printer.setDocName(title);
  QPrintPreviewDialog preview(&printer, parent);
  preview.setWindowTitle(title);
  preview.setWindowFlags(preview.windowFlags() | Qt::WindowMaximizeButtonHint |
                         Qt::WindowCloseButtonHint);
  preview.resize(1024, 768);
  QObject::connect(&preview, &QPrintPreviewDialog::paintRequested, &preview,
                   [html](QPrinter * out) { paintHtml(*out, html); });
  preview.exec();
}

void savePdf(QWidget * parent, const QString & suggestedName, const QString & html)
{
  const QString path = QFileDialog::getSaveFileName(parent, QStringLiteral("Зберегти PDF"),
                                                    suggestedName, QStringLiteral("PDF (*.pdf)"));
  if (path.isEmpty()) {
    return;
  }
  const PrintConfig config = loadPrintConfig();
  QPrinter printer(QPrinter::ScreenResolution);
  applyPaper(printer, landscapeFromHtml(html), config);
  printer.setOutputFormat(QPrinter::PdfFormat);
  printer.setOutputFileName(path);
  printer.setDocName(suggestedName);
  paintHtml(printer, html);
}

void saveCsv(QWidget * parent, const QString & suggestedName, const QString & csv)
{
  const QString path = QFileDialog::getSaveFileName(parent, QStringLiteral("Зберегти CSV"),
                                                    suggestedName, QStringLiteral("CSV (*.csv)"));
  if (path.isEmpty()) {
    return;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    return;
  }
  file.write("\xEF\xBB\xBF");
  file.write(csv.toUtf8());
}

void exportReport(QWidget * parent, const QString & title, const QString & pdfName,
                  const QString & html, const QString & csvName, const QString & csv)
{
  QMenu menu(parent);
  QAction * preview = menu.addAction(QStringLiteral("Друк…"));
  QAction * pdf = menu.addAction(QStringLiteral("Зберегти PDF…"));
  QAction * csvAction = nullptr;
  if (!csv.isEmpty()) {
    csvAction = menu.addAction(QStringLiteral("Зберегти CSV…"));
  }
  QAction * chosen = menu.exec(QCursor::pos());
  if (chosen == preview) {
    printHtml(parent, title, html);
  } else if (chosen == pdf) {
    savePdf(parent, pdfName, html);
  } else if (chosen == csvAction) {
    saveCsv(parent, csvName, csv);
  }
}

} // namespace fin::ui
