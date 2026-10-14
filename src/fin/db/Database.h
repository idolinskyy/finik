#pragma once

// Відкриття SQLite, міграції схеми, PRAGMA.

#include "fin/core/Result.h"

#include <QSqlDatabase>
#include <QString>

namespace fin {

class Database
{
 public:
  // Версія схеми БД. При зміні структури таблиць збільшуйте цей номер
  // і додавайте міграційний крок у Database::migrate().
  static constexpr int kSchemaVersion = 27;

  // Відкриває або створює файл *.fin; повертає помилку, якщо схема
  // несумісна або файл пошкоджений.
  static Result<Database> openFile(const QString & path);

  // Відкриває БД в пам'яті без персистентності (для тестів).
  // Після знищення об'єкта всі дані зникають.
  static Result<Database> openMemory();

  Database(Database && other) noexcept;
  Database & operator=(Database && other) noexcept;
  ~Database();

  // Заборонено, щоб уникнути випадкового дублювання іменованого з'єднання.
  Database(const Database &) = delete;
  Database & operator=(const Database &) = delete;

  QSqlDatabase handle() const { return db_; }
  QString connectionName() const { return connectionName_; }
  QString databaseName() const { return db_.databaseName(); }

  // Гаряче резервне копіювання через SQLite Online Backup API.
  // Повертає шлях до скопійованого файлу або помилку.
  Result<QString> backupTo(const QString & destinationPath) const;

 private:
  Database() = default;

  static Result<Database> open(const QString & connectionName, const QString & databaseName);
  // Налаштовує WAL, foreign_keys, journal_mode та інші PRAGMA.
  Status applyPragmas();
  // Застосовує міграції: порівнює user_version з kSchemaVersion.
  Status migrate();
  // Перевіряє доступний простір і права запису до директорії файлу.
  Status checkStorage();

  // Унікальне ім'я Qt SQL-з'єднання (UUID-рядок); знімається з реєстрації
  // в деструкторі через QSqlDatabase::removeDatabase().
  QString connectionName_;
  QSqlDatabase db_;
};

} // namespace fin
