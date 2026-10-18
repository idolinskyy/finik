#include "ui/AuthDialogs.h"

#include "ui/HelpBrowser.h"
#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include "fin/db/AuditLog.h"
#include "fin/db/Database.h"
#include "fin/db/UserRepository.h"
#include "fin/domain/User.h"
#include "fin/integrity/IntegrityChecker.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QtGlobal>
#include <QShortcut>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fin {

LoginDialog::LoginDialog(LedgerSession & session, QWidget * parent)
    : QDialog(parent), session_(session)
{
  setWindowTitle(QStringLiteral("Вхід у Фінік"));
  setModal(true);
  resize(400, 280);
  auto * logo = new QLabel;
  logo->setAlignment(Qt::AlignHCenter);
  logo->setPixmap(ui::brandLogo(96));
  auto * title = new QLabel(ui::brandNameUk());
  title->setAlignment(Qt::AlignHCenter);
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 4);
  titleFont.setBold(true);
  title->setFont(titleFont);

  login_ = new QComboBox;
  UserRepository users(session_.handle());
  if (Result<std::vector<User>> list = users.listAll()) {
    for (const User & u : list.value()) {
      if (u.isActive) {
        // Display login, or login + name
        QString text = u.login;
        if (!u.displayName.isEmpty() && u.displayName != u.login) {
          text += QStringLiteral(" (") + u.displayName + QLatin1Char(')');
        }
        login_->addItem(text, u.login);
      }
    }
  } else {
    login_->addItem(QStringLiteral("admin"), QStringLiteral("admin"));
  }

  password_ = new QLineEdit;
  password_->setEchoMode(QLineEdit::Password);

  auto * showPassword = new QCheckBox(QStringLiteral("Показати пароль"));
  connect(showPassword, &QCheckBox::toggled, this, [this](bool checked) {
    password_->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
  });

  reason_ = new QLabel(QStringLiteral("У базі встановлено пароль."));
  reason_->setWordWrap(true);
  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Логін"), login_);
  form->addRow(QStringLiteral("Пароль"), password_);
  form->addRow(QString(), showPassword);

  auto * buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Увійти"));
  buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Вихід"));
  auto * layout = new QVBoxLayout(this);
  layout->addWidget(logo);
  layout->addWidget(title);
  layout->addLayout(form);
  layout->addWidget(reason_);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &LoginDialog::tryLogin);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  password_->setFocus();
}

void LoginDialog::tryLogin()
{
  UserRepository users(session_.handle());
  QString userLogin = login_->currentData().toString();
  if (userLogin.isEmpty()) {
    userLogin = login_->currentText();
  }
  const Result<User> user = users.authenticate(userLogin, password_->text());
  if (!user) {
    reason_->setText(user.message());
    return;
  }
  const Status set = users.setCurrentUser(user.value().id);
  if (!set) {
    reason_->setText(set.message());
    return;
  }
  accepted_ = true;
  accept();
}

PasswordDialog::PasswordDialog(LedgerSession & session, QWidget * parent)
    : QDialog(parent), session_(session)
{
  setWindowTitle(QStringLiteral("Пароль"));
  setModal(true);
  resize(420, 200);
  password_ = new QLineEdit;
  password_->setEchoMode(QLineEdit::Password);
  repeat_ = new QLineEdit;
  repeat_->setEchoMode(QLineEdit::Password);
  reason_ =
      new QLabel(QStringLiteral("Порожній пароль вимикає запит при старті. Мінімум 6 символів."));
  reason_->setWordWrap(true);
  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Новий пароль"), password_);
  form->addRow(QStringLiteral("Ще раз"), repeat_);
  auto * buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("Зберегти"));
  buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Скасувати"));
  auto * layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(reason_);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &PasswordDialog::save);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void PasswordDialog::save()
{
  if (password_->text() != repeat_->text()) {
    reason_->setText(QStringLiteral("паролі не збігаються"));
    return;
  }
  UserRepository users(session_.handle());
  Result<User> current = users.currentUser();
  if (!current) {
    ui::showError(this, current.error());
    return;
  }
  const Status status = users.setPassword(current.value().id, password_->text());
  if (!status) {
    ui::showError(this, status);
    return;
  }
  accept();
}

AuditDialog::AuditDialog(LedgerSession & session, QWidget * parent)
    : QDialog(parent), session_(session)
{
  setWindowTitle(QStringLiteral("Журнал змін"));
  setModal(true);
  resize(920, 520);
  action_ = new QComboBox;
  action_->addItem(QStringLiteral("Усі дії"), QString());
  for (const char * name : {"insert", "update", "delete", "post", "unpost"}) {
    action_->addItem(QString::fromLatin1(name), QString::fromLatin1(name));
  }
  entity_ = new QComboBox;
  entity_->addItem(QStringLiteral("Усі об'єкти"), QString());
  for (const char * name : {"document", "period", "company", "password", "user"}) {
    entity_->addItem(QString::fromLatin1(name), QString::fromLatin1(name));
  }
  auto * filterBar = new QHBoxLayout;
  filterBar->addWidget(new QLabel(QStringLiteral("Дія")));
  filterBar->addWidget(action_);
  filterBar->addWidget(new QLabel(QStringLiteral("Об'єкт")));
  filterBar->addWidget(entity_);
  auto * refresh = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/refresh.svg")),
                                   QStringLiteral("Оновити"));
  auto * csv = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                               QStringLiteral("CSV"));
  filterBar->addWidget(refresh);
  filterBar->addWidget(csv);
  filterBar->addStretch();

  table_ = new QTableWidget;
  table_->setColumnCount(6);
  table_->setHorizontalHeaderLabels({QStringLiteral("Час"), QStringLiteral("Користувач"),
                                     QStringLiteral("Дія"), QStringLiteral("Об'єкт"),
                                     QStringLiteral("Id"), QStringLiteral("Деталі")});
  table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);
  auto * close = new QPushButton(QStringLiteral("Закрити"));
  auto * layout = new QVBoxLayout(this);
  layout->addLayout(filterBar);
  layout->addWidget(table_, 1);
  layout->addWidget(close, 0, Qt::AlignRight);
  connect(refresh, &QPushButton::clicked, this, &AuditDialog::reload);
  connect(action_, &QComboBox::currentIndexChanged, this, &AuditDialog::reload);
  connect(entity_, &QComboBox::currentIndexChanged, this, &AuditDialog::reload);
  connect(csv, &QPushButton::clicked, this, &AuditDialog::exportCsv);
  connect(close, &QPushButton::clicked, this, &QDialog::accept);
  reload();
}

void AuditDialog::reload()
{
  const QString action = action_->currentData().toString();
  const QString entity = entity_->currentData().toString();
  const Result<std::vector<AuditEntry>> rows =
      AuditLog(session_.handle()).listRecent(500, action, entity);
  if (!rows) {
    ui::showError(this, rows.error());
    return;
  }
  table_->setRowCount(static_cast<int>(rows.value().size()));
  int row = 0;
  for (const AuditEntry & item : rows.value()) {
    table_->setItem(row, 0,
                    new QTableWidgetItem(item.at.toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"))));
    table_->setItem(row, 1, new QTableWidgetItem(item.userLogin));
    table_->setItem(row, 2, new QTableWidgetItem(item.action));
    table_->setItem(row, 3, new QTableWidgetItem(item.entity));
    table_->setItem(
        row, 4,
        new QTableWidgetItem(item.entityId > 0 ? QString::number(item.entityId) : QString()));
    table_->setItem(row, 5, new QTableWidgetItem(item.details));
    ++row;
  }
}

void AuditDialog::exportCsv()
{
  QString csv = QStringLiteral("Час;Користувач;Дія;Об'єкт;Id;Деталі\n");
  for (int row = 0; row < table_->rowCount(); ++row) {
    QStringList cells;
    for (int col = 0; col < table_->columnCount(); ++col) {
      QTableWidgetItem * item = table_->item(row, col);
      QString text = item ? item->text() : QString();
      text.replace(QLatin1Char('"'), QStringLiteral("\"\""));
      if (text.contains(QLatin1Char(';')) || text.contains(QLatin1Char('"'))) {
        text = QStringLiteral("\"%1\"").arg(text);
      }
      cells.push_back(text);
    }
    csv += cells.join(QLatin1Char(';')) + QLatin1Char('\n');
  }
  ui::saveCsv(this, QStringLiteral("audit.csv"), csv);
}

AboutDialog::AboutDialog(LedgerSession & session, QWidget * parent) : QDialog(parent)
{
  setWindowTitle(QStringLiteral("Про програму"));
  setModal(true);
  resize(560, 440);
  QString version = QStringLiteral("0.1");
#ifdef FIN_VERSION
  version = QString::fromLatin1(FIN_VERSION);
#endif
  QString user = QStringLiteral("admin");
  const Result<User> current = UserRepository(session.handle()).currentUser();
  if (current) {
    user = current.value().login + QStringLiteral(" (") + userRoleLabelUk(current.value().role) +
           QLatin1Char(')');
  }
  auto * logo = new QLabel;
  logo->setPixmap(ui::brandLogo(96));
  logo->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
  auto * text = new QLabel(
      QStringLiteral(      "Фінік (Finik) — настільний облік\n\n"
                     "Версія: %1\n"
                     "Схема бази: %2\n"
                     "Файл: %3\n"
                     "Користувач: %4\n\n"
                     "Copyright (C) 2026 Ihor Dolinskyi\n"
                     "Гроші в копійках, проведення лише через журнал.")
          .arg(version, QString::number(Database::kSchemaVersion), session.databaseName(), user));
  text->setWordWrap(true);
  text->setTextInteractionFlags(Qt::TextSelectableByMouse);
  auto * legal = new QLabel(
      QStringLiteral("Інтерфейс побудовано з Qt %1 (The Qt Company Ltd.). Qt — LGPL/GPL; "
                     "подробиці: Довідка → Про Qt… та файл NOTICE.\n"
                     "Фінік поширюється на умовах GNU GPL v3 (LICENSE), «як є», без гарантій.")
          .arg(QString::fromLatin1(qVersion())));
  legal->setWordWrap(true);
  legal->setTextInteractionFlags(Qt::TextSelectableByMouse);
  auto * check = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/sparkles.svg")),
                                 QStringLiteral("Перевірити базу"));
  auto * aboutQt = new QPushButton(QStringLiteral("Про Qt…"));
  auto * close = new QPushButton(QStringLiteral("Закрити"));
  auto * buttons = new QHBoxLayout;
  buttons->addWidget(check);
  buttons->addWidget(aboutQt);
  buttons->addStretch();
  buttons->addWidget(close);
  auto * header = new QHBoxLayout;
  header->addWidget(logo);
  header->addWidget(text, 1);
  auto * layout = new QVBoxLayout(this);
  layout->addLayout(header);
  layout->addWidget(legal);
  layout->addLayout(buttons);
  connect(close, &QPushButton::clicked, this, &QDialog::accept);
  connect(aboutQt, &QPushButton::clicked, this, [this]() { QMessageBox::aboutQt(this); });
  connect(check, &QPushButton::clicked, this, [this, &session]() {
    const Status status = IntegrityChecker(session.handle()).verifyEngine();
    if (!status) {
      ui::showError(this, status);
      return;
    }
    QMessageBox::information(this, QStringLiteral("Перевірка бази"),
                             QStringLiteral("Перевірка цілісності пройшла успішно."));
  });
}

namespace {

void fillRoleCombo(QComboBox * combo, UserRole current)
{
  const UserRole roles[] = {UserRole::Admin, UserRole::Accountant, UserRole::Cashier,
                            UserRole::Viewer};
  combo->clear();
  int index = 0;
  int selected = 0;
  for (UserRole role : roles) {
    combo->addItem(userRoleLabelUk(role), static_cast<int>(role));
    if (role == current) {
      selected = index;
    }
    ++index;
  }
  combo->setCurrentIndex(selected);
}

bool editUserCard(QWidget * parent, User & user)
{
  QDialog dialog(parent);
  dialog.setWindowTitle(user.id ? QStringLiteral("Користувач")
                                : QStringLiteral("Новий користувач"));
  auto * login = new QLineEdit(user.login);
  auto * name = new QLineEdit(user.displayName);
  auto * role = new QComboBox;
  fillRoleCombo(role, user.role);
  auto * active = new QCheckBox(QStringLiteral("Активний"));
  active->setChecked(user.isActive);
  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Логін"), login);
  form->addRow(QStringLiteral("Ім'я"), name);
  form->addRow(QStringLiteral("Роль"), role);
  form->addRow(QString(), active);
  auto * buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  auto * layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(buttons);
  dialog.resize(380, 220);
  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }
  user.login = login->text();
  user.displayName = name->text();
  user.role = static_cast<UserRole>(role->currentData().toInt());
  user.isActive = active->isChecked();
  return true;
}

} // namespace

UsersDialog::UsersDialog(LedgerSession & session, QWidget * parent)
    : QDialog(parent), session_(session)
{
  setWindowTitle(QStringLiteral("Користувачі"));
  setModal(true);
  resize(640, 420);
  auto * addBtn = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/add.svg")),
                                  QStringLiteral("Додати"));
  auto * editBtn = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/edit.svg")),
                                   QStringLiteral("Змінити"));
  auto * passwordBtn = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/settings.svg")),
                                       QStringLiteral("Пароль"));
  auto * bar = new QHBoxLayout;
  bar->addWidget(addBtn);
  bar->addWidget(editBtn);
  bar->addWidget(passwordBtn);
  bar->addStretch();
  table_ = new QTableWidget;
  table_->setColumnCount(5);
  table_->setHorizontalHeaderLabels({QStringLiteral("Логін"), QStringLiteral("Ім'я"),
                                     QStringLiteral("Роль"), QStringLiteral("Активний"),
                                     QStringLiteral("Пароль")});
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);
  auto * hint = new QLabel(
      QStringLiteral("Адміністратор керує ролями. Не можна зняти останнього адміністратора. "
                     "Касир — лише ПКО/ВКО; перегляд — читання і друк."));
  hint->setWordWrap(true);
  auto * close = new QPushButton(QStringLiteral("Закрити"));
  auto * layout = new QVBoxLayout(this);
  layout->addLayout(bar);
  layout->addWidget(table_, 1);
  layout->addWidget(hint);
  layout->addWidget(close, 0, Qt::AlignRight);
  connect(addBtn, &QPushButton::clicked, this, &UsersDialog::add);
  connect(editBtn, &QPushButton::clicked, this, &UsersDialog::editSelected);
  connect(passwordBtn, &QPushButton::clicked, this, &UsersDialog::setPasswordSelected);
  connect(close, &QPushButton::clicked, this, &QDialog::accept);
  connect(table_, &QTableWidget::doubleClicked, this,
          [this](const QModelIndex &) { editSelected(); });
  auto * f1 = new QShortcut(QKeySequence::HelpContents, this);
  connect(f1, &QShortcut::activated, this,
          [this]() { HelpBrowser::openTopic(this, QStringLiteral("security.html")); });
  reload();
}

void UsersDialog::reload()
{
  UserRepository users(session_.handle());
  const Result<std::vector<User>> list = users.listAll();
  if (!list) {
    ui::showError(this, list.error());
    return;
  }
  table_->setRowCount(static_cast<int>(list.value().size()));
  int row = 0;
  for (const User & user : list.value()) {
    auto * login = new QTableWidgetItem(user.login);
    login->setData(Qt::UserRole, static_cast<qlonglong>(user.id.value));
    table_->setItem(row, 0, login);
    table_->setItem(row, 1, new QTableWidgetItem(user.displayName));
    table_->setItem(row, 2, new QTableWidgetItem(userRoleLabelUk(user.role)));
    table_->setItem(
        row, 3, new QTableWidgetItem(user.isActive ? QStringLiteral("так") : QStringLiteral("ні")));
    table_->setItem(
        row, 4,
        new QTableWidgetItem(user.hasPassword ? QStringLiteral("є") : QStringLiteral("немає")));
    ++row;
  }
}

UserId UsersDialog::selectedId() const
{
  const int row = table_->currentRow();
  if (row < 0) {
    return {};
  }
  QTableWidgetItem * cell = table_->item(row, 0);
  if (cell == nullptr) {
    return {};
  }
  return UserId{cell->data(Qt::UserRole).toLongLong()};
}

void UsersDialog::add()
{
  User user;
  user.role = UserRole::Accountant;
  user.isActive = true;
  if (!editUserCard(this, user)) {
    return;
  }
  UserRepository users(session_.handle());
  const Result<UserId> id = users.insert(user);
  if (!id) {
    ui::showError(this, id.error());
    return;
  }
  reload();
}

void UsersDialog::editSelected()
{
  const UserId id = selectedId();
  if (!id) {
    return;
  }
  UserRepository users(session_.handle());
  Result<User> user = users.findById(id);
  if (!user) {
    ui::showError(this, user.error());
    return;
  }
  if (!editUserCard(this, user.value())) {
    return;
  }
  const Status status = users.update(user.value());
  if (!status) {
    ui::showError(this, status);
    return;
  }
  reload();
}

void UsersDialog::setPasswordSelected()
{
  const UserId id = selectedId();
  if (!id) {
    return;
  }
  QDialog dialog(this);
  dialog.setWindowTitle(QStringLiteral("Пароль користувача"));
  auto * password = new QLineEdit;
  password->setEchoMode(QLineEdit::Password);
  auto * repeat = new QLineEdit;
  repeat->setEchoMode(QLineEdit::Password);
  auto * reason = new QLabel(
      QStringLiteral("Порожній пароль знімає пароль. Мінімум 6 символів, якщо задаєте."));
  reason->setWordWrap(true);
  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Новий пароль"), password);
  form->addRow(QStringLiteral("Ще раз"), repeat);
  auto * buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("Зберегти"));
  buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Скасувати"));
  auto * layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(reason);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
    if (password->text() != repeat->text()) {
      reason->setText(QStringLiteral("паролі не збігаються"));
      return;
    }
    dialog.accept();
  });
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  UserRepository users(session_.handle());
  const Status status = users.setPassword(id, password->text());
  if (!status) {
    ui::showError(this, status);
    return;
  }
  reload();
}

} // namespace fin
