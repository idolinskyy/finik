// Точка входу в десктопну бухгалтерську систему «Фінік».
//
// Життєвий цикл запуску:
//   1. Ініціалізація QApplication, налаштування локалі, іконки та назви
//   програми;
//   2. Визначення шляху до файлу робочої бази даних SQLite (каталог AppData);
//   3. Перевірка та застосування відкладеного відновлення з резервної копії
//   (applyStagedRestore);
//   4. Відкриття облікової сесії LedgerSession (підключення БД, налаштування
//   PRAGMA foreign_keys, міграції);
//   5. Аутентифікація: якщо в системі встановлено паролі — відображення вікна
//   входу (LoginDialog);
//   6. Запуск головного вікна інтерфейсу MainWindow та циклу обробки подій Qt.

#include "ui/AuthDialogs.h"
#include "ui/MainWindow.h"
#include "ui/UiSupport.h"

#include "fin/db/AppPaths.h"
#include "fin/db/Restore.h"
#include "fin/db/UserRepository.h"
#include "fin/session/LedgerSession.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTranslator>

namespace {

void installUkrainianUi()
{
  const QLocale uk(QLocale::Ukrainian, QLocale::Ukraine);
  QLocale::setDefault(uk);

  const QString translationsPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
  // Стандартні кнопки Qt (OK/Cancel/Yes/No, QFileDialog тощо).
  auto * qtTranslator = new QTranslator(qApp);
  if (qtTranslator->load(uk, QStringLiteral("qt"), QStringLiteral("_"), translationsPath) ||
      qtTranslator->load(QStringLiteral("qt_uk"), translationsPath)) {
    QCoreApplication::installTranslator(qtTranslator);
  } else {
    delete qtTranslator;
  }
  auto * qtBaseTranslator = new QTranslator(qApp);
  if (qtBaseTranslator->load(uk, QStringLiteral("qtbase"), QStringLiteral("_"), translationsPath) ||
      qtBaseTranslator->load(QStringLiteral("qtbase_uk"), translationsPath)) {
    QCoreApplication::installTranslator(qtBaseTranslator);
  } else {
    delete qtBaseTranslator;
  }
}

} // namespace

int main(int argc, char * argv[])
{
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("Finik"));
  QCoreApplication::setApplicationName(QStringLiteral("Finik"));
  QApplication::setWindowIcon(QIcon(QStringLiteral(":/help/logo.png")));
  QApplication::setApplicationDisplayName(QStringLiteral("Фінік"));
  QGuiApplication::setDesktopFileName(QStringLiteral("finik"));
  installUkrainianUi();

  QSettings settings;
  int fontSize = settings.value(QStringLiteral("ui/font_size"), 10).toInt();

  int fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/help/fonts/Inter-Regular.otf"));
  if (fontId != -1) {
    QString family = QFontDatabase::applicationFontFamilies(fontId).at(0);
    QFont font(family, fontSize);
    QApplication::setFont(font);
  } else {
    QFont font(QStringLiteral("sans-serif"), fontSize);
    QApplication::setFont(font);
  }

  fin::ui::applyThemeAndStyle();
  fin::ui::installCalendarConfigFilter();

  QString path;
  QString customPath = settings.value(QStringLiteral("database/custom_path")).toString();

  if (!customPath.isEmpty()) {
    path = customPath;
    QDir().mkpath(QFileInfo(path).absolutePath());
  } else {
    path = fin::resolveDefaultDatabasePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
      fin::ui::showError(nullptr, QStringLiteral("не вдалося створити каталог даних"));
      return 1;
    }
  }
  const fin::Status restored = fin::applyStagedRestore(path);
  if (!restored) {
    fin::ui::showError(nullptr, restored);
    return 1;
  }
  fin::Result<fin::LedgerSession> session = fin::LedgerSession::openFile(path);
  if (!session) {
    fin::ui::showError(nullptr, session.error());
    return 1;
  }
  fin::UserRepository users(session.value().handle());
  const fin::Result<bool> needLogin = users.passwordRequired();
  if (!needLogin) {
    fin::ui::showError(nullptr, needLogin.error());
    return 1;
  }
  if (needLogin.value()) {
    fin::LoginDialog login(session.value());
    if (login.exec() != QDialog::Accepted || !login.acceptedLogin()) {
      return 0;
    }
  }

  fin::MainWindow window(session.value());
  window.show();
  return QApplication::exec();
}
