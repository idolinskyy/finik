#include "support/Session.h"

#include <QDir>
#include <QFile>
#include <QIODevice>

using namespace fin::test;

namespace {

QString readSource(const QString & relative)
{
  const QString path = QDir(QString::fromUtf8(FIN_SOURCE_DIR)).filePath(relative);
  QFile file(path);
  REQUIRE(file.exists());
  REQUIRE(file.open(QIODevice::ReadOnly | QIODevice::Text));
  return QString::fromUtf8(file.readAll());
}

} // namespace

TEST_CASE("packaging desktop file and package script", "[packaging][help]")
{
  const QString desktop = readSource(QStringLiteral("packaging/finik.desktop"));
  REQUIRE(desktop.contains(QStringLiteral("Type=Application")));
  REQUIRE(desktop.contains(QStringLiteral("Exec=Finik")));
  REQUIRE(desktop.contains(QStringLiteral("Icon=finik")));
  REQUIRE(desktop.contains(QStringLiteral("Categories=Office;Finance;Qt;")));
  REQUIRE(desktop.contains(QStringLiteral("StartupWMClass=Finik")));

  const QString cmake = readSource(QStringLiteral("CMakeLists.txt"));
  REQUIRE(cmake.contains(QStringLiteral("install(TARGETS Finik")));
  REQUIRE(cmake.contains(QStringLiteral("include(CPack)")));
  REQUIRE(cmake.contains(QStringLiteral("DEB;TGZ")));
  REQUIRE(cmake.contains(QStringLiteral("packaging/finik.desktop")));

  const QString script = readSource(QStringLiteral("scripts/package.sh"));
  REQUIRE(script.contains(QStringLiteral("linuxdeploy")));
  REQUIRE(script.contains(QStringLiteral("AppDir")));
  REQUIRE(script.contains(QStringLiteral("--target package")));

  REQUIRE(QFile::exists(QDir(QString::fromUtf8(FIN_SOURCE_DIR))
                            .filePath(QStringLiteral("packaging/icons/hicolor/256x256/apps/finik.png"))));
}
