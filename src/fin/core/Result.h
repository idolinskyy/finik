#pragma once

// Status / Result<T>: помилка як значення бізнес-логіки, не виняток.
// [[nodiscard]] змушує обробляти результат (критично для проведення).

#include <optional>
#include <utility>

#include <QString>

namespace fin {

// Коди помилок бізнес-логіки (внутрішній домен).
enum class ErrorCode
{
  InvalidArgument, // некоректний вхідний параметр
  NotFound,        // об'єкт не знайдено в БД
  Validation,      // порушення бізнес-правила документа
  AlreadyPosted, // спроба повторно провести вже проведений документ
  NotPosted,    // операція вимагає проведеного документа
  PeriodClosed, // дата документа потрапляє у закритий обліковий місяць
  Database,     // помилка SQLite (обгортка над Qt SQL-помилкою)
  Conflict,     // залежний об'єкт блокує операцію
  Overflow      // арифметичне переповнення int64
};

struct Error
{
  ErrorCode code = ErrorCode::InvalidArgument;
  QString message;

  static Error invalidArgument(QString message)
  {
    return {ErrorCode::InvalidArgument, std::move(message)};
  }
  static Error notFound(QString message) { return {ErrorCode::NotFound, std::move(message)}; }
  static Error validation(QString message) { return {ErrorCode::Validation, std::move(message)}; }
  static Error alreadyPosted(QString message)
  {
    return {ErrorCode::AlreadyPosted, std::move(message)};
  }
  static Error notPosted(QString message) { return {ErrorCode::NotPosted, std::move(message)}; }
  static Error periodClosed(QString message)
  {
    return {ErrorCode::PeriodClosed, std::move(message)};
  }
  static Error database(QString message) { return {ErrorCode::Database, std::move(message)}; }
  static Error conflict(QString message) { return {ErrorCode::Conflict, std::move(message)}; }
  static Error overflow(QString message) { return {ErrorCode::Overflow, std::move(message)}; }
};

// Status — результат операції без значення, що повертається (void).
// Аналог Result<void>, але компактніший.
// Типове використання: методи репозиторіїв, що лише зберігають дані.
class [[nodiscard]] Status
{
 public:
  static Status ok() { return Status{}; }
  static Status fail(Error error) { return Status{std::move(error)}; }

  bool hasError() const { return error_.has_value(); }
  explicit operator bool() const { return !hasError(); }
  const Error & error() const { return *error_; }
  QString message() const { return hasError() ? error_->message : QString{}; }

 private:
  Status() = default;
  explicit Status(Error error) : error_(std::move(error)) {}

  std::optional<Error> error_;
};

// Result<T> — результат операції зі значенням типу T.
// Рівно один з value_ та error_ завжди contains_value.
// Типове використання: методи репозиторіїв (find, list), арифметика Money.
template <typename T> class [[nodiscard]] Result
{
 public:
  static Result ok(T value) { return Result{std::move(value), std::nullopt}; }
  static Result fail(Error error) { return Result{std::nullopt, std::move(error)}; }

  bool hasValue() const { return value_.has_value(); }
  explicit operator bool() const { return hasValue(); }

  const T & value() const & { return *value_; }
  T & value() & { return *value_; }
  T && value() && { return std::move(*value_); }

  const Error & error() const { return *error_; }
  QString message() const { return hasValue() ? QString{} : error_->message; }

  // Перетворює Result<T> на Status, відкидаючи значення.
  // Зручно для передачі помилки вгору по ланцюжку без unwrap.
  Status asStatus() const { return hasValue() ? Status::ok() : Status::fail(*error_); }

 private:
  Result(std::optional<T> value, std::optional<Error> error)
      : value_(std::move(value)), error_(std::move(error))
  {
  }

  std::optional<T> value_;
  std::optional<Error> error_;
};

} // namespace fin
