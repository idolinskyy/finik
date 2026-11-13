#include "fin/edo/XsdValidator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#ifdef FINIK_HAVE_LIBXML2
#include <libxml/parser.h>
#include <libxml/xmlschemas.h>
#endif

namespace fin {
namespace {

QString appDataXsdDir()
{
  const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (data.isEmpty()) {
    return QString();
  }
  return data + QStringLiteral("/xsd");
}

#ifdef FINIK_HAVE_LIBXML2
void xmlSilence(void * /*ctx*/, const char * /*msg*/, ...) {}
#endif

} // namespace

bool xsdValidationAvailable()
{
#ifdef FINIK_HAVE_LIBXML2
  return true;
#else
  return false;
#endif
}

QStringList xsdSearchDirectories()
{
  QStringList dirs;
  const QString env = qEnvironmentVariable("FINIK_XSD_DIR");
  if (!env.trimmed().isEmpty()) {
    dirs.append(env.trimmed());
  }
  const QString appData = appDataXsdDir();
  if (!appData.isEmpty()) {
    dirs.append(appData);
  }
  if (QCoreApplication::instance() != nullptr) {
    dirs.append(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("xsd")));
  }
  dirs.append(QStringLiteral("vendor/xsd"));
  return dirs;
}

QString findDeclarXsd(const QString & basename)
{
  const QString name = QFileInfo(basename).fileName();
  if (name.isEmpty() || !name.endsWith(QStringLiteral(".xsd"), Qt::CaseInsensitive)) {
    return QString();
  }
  for (const QString & dir : xsdSearchDirectories()) {
    const QString path = QDir(dir).filePath(name);
    if (QFileInfo::exists(path)) {
      return QFileInfo(path).absoluteFilePath();
    }
  }
  return QString();
}

Status validateAgainstXsdIfPresent(const QString & xml, const QString & xsdBasename)
{
  const QString path = findDeclarXsd(xsdBasename);
  if (path.isEmpty()) {
    return Status::ok();
  }
#ifndef FINIK_HAVE_LIBXML2
  return Status::fail(Error::validation(
      QStringLiteral("знайдено %1, але програму зібрано без libxml2").arg(path)));
#else
  if (xml.trimmed().isEmpty()) {
    return Status::fail(Error::validation(QStringLiteral("порожній XML")));
  }
  const QByteArray utf8 = xml.toUtf8();
  const QByteArray pathUtf8 = QFile::encodeName(path);

  xmlInitParser();
  xmlDocPtr doc = xmlReadMemory(utf8.constData(), static_cast<int>(utf8.size()), "declar.xml",
                                "UTF-8", 0);
  if (doc == nullptr) {
    return Status::fail(Error::validation(QStringLiteral("libxml2: не вдалося прочитати XML")));
  }
  xmlSchemaParserCtxtPtr parser = xmlSchemaNewParserCtxt(pathUtf8.constData());
  if (parser == nullptr) {
    xmlFreeDoc(doc);
    return Status::fail(Error::validation(QStringLiteral("libxml2: не вдалося відкрити XSD")));
  }
  xmlSchemaSetParserErrors(parser, xmlSilence, xmlSilence, nullptr);
  xmlSchemaPtr schema = xmlSchemaParse(parser);
  xmlSchemaFreeParserCtxt(parser);
  if (schema == nullptr) {
    xmlFreeDoc(doc);
    return Status::fail(Error::validation(QStringLiteral("libxml2: помилка розбору XSD %1").arg(path)));
  }
  xmlSchemaValidCtxtPtr valid = xmlSchemaNewValidCtxt(schema);
  xmlSchemaSetValidErrors(valid, xmlSilence, xmlSilence, nullptr);
  const int rc = xmlSchemaValidateDoc(valid, doc);
  xmlSchemaFreeValidCtxt(valid);
  xmlSchemaFree(schema);
  xmlFreeDoc(doc);
  if (rc != 0) {
    return Status::fail(Error::validation(
        QStringLiteral("XML не відповідає XSD %1 (перевірте файл у Кабінеті ДПС / M.E.Doc)")
            .arg(QFileInfo(path).fileName())));
  }
  return Status::ok();
#endif
}

} // namespace fin
