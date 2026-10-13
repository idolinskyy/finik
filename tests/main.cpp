#include <QApplication>
#include <QCoreApplication>
#include <QStandardPaths>

#include <catch2/catch_session.hpp>

int main(int argc, char * argv[])
{
  QStandardPaths::setTestModeEnabled(true);
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("Finik"));
  QCoreApplication::setApplicationName(QStringLiteral("Finik"));
  return Catch::Session().run(argc, argv);
}
