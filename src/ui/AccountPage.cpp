#include "ui/AccountPage.h"

#include "ui/UiSupport.h"

#include "fin/auth/Access.h"
#include "fin/domain/Subconto.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSet>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <cstdint>

namespace fin {
namespace {

QString flagsText(const Account & account, const std::vector<AccountSubconto> & sub)
{
  QString flags;
  if (account.isFolder) {
    flags = QStringLiteral("група");
  } else {
    flags = account.normalSide == NormalSide::Debit ? QStringLiteral("А") : QStringLiteral("П");
    if (account.hasCounterparty) {
      flags += QStringLiteral(", контрагент");
    }
    if (!sub.empty()) {
      QStringList parts;
      for (const AccountSubconto & s : sub) {
        parts.append(QStringLiteral("С%1").arg(s.lineNo));
      }
      flags += QStringLiteral(", субконто: ") + parts.join(QLatin1Char('+'));
    }
  }
  return flags;
}

bool editAccountSubconto(QWidget * parent, LedgerSession & session, AccountId accountId,
                         const QString & code)
{
  const Result<std::vector<SubcontoKind>> kindsRes = session.subconto().listKinds();
  if (!kindsRes) {
    ui::showError(parent, kindsRes.error());
    return false;
  }
  const Result<std::vector<AccountSubconto>> current = session.subconto().listForAccount(accountId);
  if (!current) {
    ui::showError(parent, current.error());
    return false;
  }

  QDialog dialog(parent);
  dialog.setWindowTitle(QStringLiteral("Субконто рахунку %1").arg(code));
  dialog.resize(420, 220);
  auto * form = new QFormLayout(&dialog);
  form->addRow(new QLabel(QStringLiteral(
      "До трьох аналітичних розрізів (субконто 1–3). Порожній вид — не використовується.")));

  struct RowUi
  {
    QComboBox * kind = nullptr;
    QCheckBox * required = nullptr;
  };
  RowUi rows[3];
  for (int i = 0; i < 3; ++i) {
    rows[i].kind = new QComboBox;
    rows[i].kind->addItem(QStringLiteral("(немає)"), QString());
    for (const SubcontoKind & k : kindsRes.value()) {
      rows[i].kind->addItem(k.nameUa, k.id);
    }
    rows[i].required = new QCheckBox(QStringLiteral("обов'язкове"));
    auto * wrap = new QHBoxLayout;
    wrap->addWidget(rows[i].kind, 1);
    wrap->addWidget(rows[i].required);
    form->addRow(QStringLiteral("Субконто %1").arg(i + 1), wrap);
  }

  for (const AccountSubconto & s : current.value()) {
    if (s.lineNo < 1 || s.lineNo > 3)
      continue;
    RowUi & ui = rows[s.lineNo - 1];
    const int idx = ui.kind->findData(s.subcontoKindId);
    if (idx >= 0)
      ui.kind->setCurrentIndex(idx);
    ui.required->setChecked(s.isRequired);
  }

  auto * buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  form->addRow(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted)
    return false;

  std::vector<AccountSubconto> settings;
  QSet<QString> usedKinds;
  for (int i = 0; i < 3; ++i) {
    const QString kindId = rows[i].kind->currentData().toString();
    if (kindId.isEmpty())
      continue;
    if (usedKinds.contains(kindId)) {
      QMessageBox::warning(parent, QStringLiteral("Субконто"),
                           QStringLiteral("Один вид субконто не можна призначити двічі."));
      return false;
    }
    usedKinds.insert(kindId);
    AccountSubconto s;
    s.accountId = accountId;
    s.lineNo = i + 1;
    s.subcontoKindId = kindId;
    s.isRequired = rows[i].required->isChecked();
    settings.push_back(std::move(s));
  }

  const Status saved = session.subconto().setForAccount(accountId, settings);
  if (!saved) {
    ui::showError(parent, saved.error());
    return false;
  }
  return true;
}

} // namespace

AccountPage::AccountPage(LedgerSession & session, QWidget * parent)
    : QWidget(parent), session_(session)
{
  auto * layout = new QVBoxLayout(this);
  tree_ = new QTreeWidget(this);
  tree_->setColumnCount(3);
  tree_->setHeaderLabels(
      {QStringLiteral("Код"), QStringLiteral("Назва"), QStringLiteral("Ознаки")});
  tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
  tree_->setRootIsDecorated(true);
  tree_->setUniformRowHeights(true);
  tree_->setContextMenuPolicy(Qt::CustomContextMenu);
  layout->addWidget(tree_);

  connect(tree_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint & pos) {
    QTreeWidgetItem * item = tree_->itemAt(pos);
    if (item == nullptr)
      return;
    const AccountId id{item->data(0, Qt::UserRole).toLongLong()};
    if (!id)
      return;
    const Result<Account> acc = session_.accounts().findById(id);
    if (!acc || acc.value().isFolder)
      return;
    QMenu menu(this);
    QAction * act = menu.addAction(QStringLiteral("Субконто…"));
    act->setEnabled(canEditSubconto());
    if (menu.exec(tree_->viewport()->mapToGlobal(pos)) == act) {
      if (editAccountSubconto(this, session_, id, acc.value().code))
        reload();
    }
  });

  connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem * item, int) {
    if (item == nullptr)
      return;
    if (!canEditSubconto())
      return;
    const AccountId id{item->data(0, Qt::UserRole).toLongLong()};
    if (!id)
      return;
    const Result<Account> acc = session_.accounts().findById(id);
    if (!acc || acc.value().isFolder)
      return;
    if (editAccountSubconto(this, session_, id, acc.value().code))
      reload();
  });

  const Status writable = Access::requireEditDirectories(session_.handle());
  if (!writable) {
    layout->addWidget(new QLabel(writable.message(), this));
  }

  reload();
}

bool AccountPage::canEditSubconto() const
{
  return Access::canEditDirectories(Access::currentRoleOrViewer(session_.handle()));
}

void AccountPage::reload()
{
  tree_->clear();
  const Result<std::vector<Account>> list = session_.accounts().listAll();
  if (!list) {
    ui::showError(this, list.error());
    return;
  }

  QHash<std::int64_t, QTreeWidgetItem *> items;
  std::vector<Account> pending = list.value();
  int guard = 0;
  while (!pending.empty() && guard < 1000) {
    ++guard;
    std::vector<Account> leftover;
    leftover.reserve(pending.size());
    for (const Account & account : pending) {
      QTreeWidgetItem * parentItem = nullptr;
      if (account.parentId) {
        parentItem = items.value(account.parentId.value, nullptr);
        if (parentItem == nullptr) {
          leftover.push_back(account);
          continue;
        }
      }
      auto * item = parentItem ? new QTreeWidgetItem(parentItem) : new QTreeWidgetItem(tree_);
      item->setText(0, account.code);
      item->setText(1, account.name);
      item->setData(0, Qt::UserRole, static_cast<qlonglong>(account.id.value));
      std::vector<AccountSubconto> sub;
      if (!account.isFolder) {
        const Result<std::vector<AccountSubconto>> subRes =
            session_.subconto().listForAccount(account.id);
        if (subRes)
          sub = subRes.value();
      }
      item->setText(2, flagsText(account, sub));
      items.insert(account.id.value, item);
    }
    if (leftover.size() == pending.size()) {
      break;
    }
    pending = std::move(leftover);
  }
  tree_->expandAll();
}

void AccountPage::reveal(AccountId id)
{
  reload();
  if (!id) {
    return;
  }
  const auto find = [&](auto && self, QTreeWidgetItem * item) -> QTreeWidgetItem * {
    if (item->data(0, Qt::UserRole).toLongLong() == id.value) {
      return item;
    }
    for (int i = 0; i < item->childCount(); ++i) {
      if (QTreeWidgetItem * found = self(self, item->child(i))) {
        return found;
      }
    }
    return nullptr;
  };
  QTreeWidgetItem * found = nullptr;
  for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
    found = find(find, tree_->topLevelItem(i));
    if (found != nullptr) {
      break;
    }
  }
  if (found == nullptr) {
    return;
  }
  tree_->setCurrentItem(found);
  tree_->scrollToItem(found);
}

} // namespace fin
