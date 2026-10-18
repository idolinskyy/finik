#pragma once

#include "fin/domain/Document.h"

#include <QDialog>
#include <QString>

class QListWidget;
class QTextBrowser;

namespace fin {

class HelpBrowser : public QDialog
{
  Q_OBJECT

 public:
  explicit HelpBrowser(QWidget * parent = nullptr);

  static void openTopic(QWidget * parent, const QString & page);
  static QString topicForDocument(DocumentType type);
  static QString topicForNavRow(int row);
  // Сторінки бічної панелі довідки (у порядку показу)
  static QStringList topicFiles();

  void showPage(const QString & page);

 private:
  void loadSelected();

  QListWidget * topics_ = nullptr;
  QTextBrowser * browser_ = nullptr;
};

} // namespace fin
