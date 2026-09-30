#pragma once

#include "fin/core/Ids.h"
#include "fin/session/LedgerSession.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;

namespace fin {

class LoginDialog : public QDialog
{
  Q_OBJECT

 public:
  LoginDialog(LedgerSession & session, QWidget * parent = nullptr);
  bool acceptedLogin() const { return accepted_; }

 private:
  void tryLogin();

  LedgerSession & session_;
  bool accepted_ = false;
  QComboBox * login_ = nullptr;
  QLineEdit * password_ = nullptr;
  QLabel * reason_ = nullptr;
};

class PasswordDialog : public QDialog
{
  Q_OBJECT

 public:
  PasswordDialog(LedgerSession & session, QWidget * parent = nullptr);

 private:
  void save();

  LedgerSession & session_;
  QLineEdit * password_ = nullptr;
  QLineEdit * repeat_ = nullptr;
  QLabel * reason_ = nullptr;
};

class AuditDialog : public QDialog
{
  Q_OBJECT

 public:
  AuditDialog(LedgerSession & session, QWidget * parent = nullptr);

 private:
  void reload();
  void exportCsv();

  LedgerSession & session_;
  QComboBox * action_ = nullptr;
  QComboBox * entity_ = nullptr;
  QTableWidget * table_ = nullptr;
};

class AboutDialog : public QDialog
{
  Q_OBJECT

 public:
  AboutDialog(LedgerSession & session, QWidget * parent = nullptr);
};

class UsersDialog : public QDialog
{
  Q_OBJECT

 public:
  UsersDialog(LedgerSession & session, QWidget * parent = nullptr);

 private:
  void reload();
  void add();
  void editSelected();
  void setPasswordSelected();
  UserId selectedId() const;

  LedgerSession & session_;
  QTableWidget * table_ = nullptr;
};

} // namespace fin
