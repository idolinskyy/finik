#pragma once

// RAII-транзакція SQLite (BEGIN IMMEDIATE / commit / rollback).

#include "fin/core/Result.h"

#include <QSqlDatabase>

namespace fin {

class Transaction
{
 public:
  explicit Transaction(QSqlDatabase db);
  ~Transaction();

  Transaction(const Transaction &) = delete;
  Transaction & operator=(const Transaction &) = delete;

  bool started() const { return started_; }
  Status startError() const { return started_ ? Status::ok() : Status::fail(startError_); }

  // Фіксує транзакцію. Повторні виклики або виклик після помилки повертають статус.
  Status commit();

 private:
  QSqlDatabase db_;
  bool started_ = false;
  bool committed_ = false;
  Error startError_;
};

} // namespace fin
