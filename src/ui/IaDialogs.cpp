#include "ui/IaDialogs.h"

#include "ui/HelpBrowser.h"
#include "ui/PdfExport.h"
#include "ui/UiSupport.h"

#include "fin/domain/ChartCodes.h"
#include "fin/domain/DocumentBlueprint.h"
#include "fin/domain/Vat.h"
#include "fin/print/PrintForms.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QShortcut>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace fin {
namespace {

void fillCorrespondence(QComboBox * combo, const std::vector<Account> & accounts,
                        AccountId selected)
{
  combo->clear();
  const char * codes[] = {chart::kSuppliers, chart::kCapexIa, chart::kAuxiliary};
  int index = 0;
  int i = 0;
  for (const char * code : codes) {
    const AccountId id = accountIdByCode(accounts, code);
    QString name = QString::fromLatin1(code);
    for (const Account & account : accounts) {
      if (account.id == id) {
        name = ui::accountLabel(account);
        break;
      }
    }
    combo->addItem(name, QVariant::fromValue(static_cast<qlonglong>(id.value)));
    if (id && id == selected) {
      index = i;
    }
    ++i;
  }
  combo->setCurrentIndex(index);
}

AccountId correspondenceFromLines(const std::vector<DocumentLine> & lines,
                                  const std::vector<Account> & accounts)
{
  for (const DocumentLine & line : lines) {
    if (accountHasCode(accounts, line.creditAccountId, chart::kIaAmortization)) {
      continue;
    }
    return line.creditAccountId;
  }
  return {};
}

} // namespace

IaCommissionDialog::IaCommissionDialog(LedgerSession & session, Document document, QWidget * parent)
    : QDialog(parent), session_(session), document_(std::move(document))
{
  setWindowTitle(documentTypeLabelUk(document_.type));
  resize(920, 520);
  const Result<std::vector<Account>> accounts = session_.accounts().listPostable();
  if (accounts) {
    accounts_ = accounts.value();
  }
  number_ = new QLineEdit(document_.number);
  date_ = new QDateEdit(document_.date.isValid() ? document_.date : QDate::currentDate());
  date_->setCalendarPopup(true);
  date_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  counterparty_ = new QComboBox;
  ui::fillCounterpartyCombo(counterparty_, session_, document_.counterpartyId, true);
  correspondence_ = new QComboBox;
  fillCorrespondence(correspondence_, accounts_,
                     correspondenceFromLines(document_.lines, accounts_));
  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Номер"), number_);
  form->addRow(QStringLiteral("Дата"), date_);
  form->addRow(QStringLiteral("Постачальник"), counterparty_);
  form->addRow(QStringLiteral("Кореспонденція"), correspondence_);

  auto * addLine = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/add.svg")),
                                   QStringLiteral("Додати рядок"));
  auto * removeLine = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/delete.svg")),
                                      QStringLiteral("Видалити рядок"));
  auto * lineBar = new QHBoxLayout;
  lineBar->addWidget(addLine);
  lineBar->addWidget(removeLine);
  lineBar->addStretch();

  table_ = new QTableWidget;
  table_->setColumnCount(1);
  table_->setHorizontalHeaderLabels({QStringLiteral("Нематеріальний актив")});
  table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table_->verticalHeader()->setVisible(false);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);

  comment_ = new QTextEdit(document_.comment);
  comment_->setAcceptRichText(false);
  comment_->setMaximumHeight(70);
  reason_ = new QLabel;
  reason_->setWordWrap(true);
  saveBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                             QStringLiteral("Зберегти"));
  postBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/post.svg")),
                             QStringLiteral("Провести"));
  unpostBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/unpost.svg")),
                               QStringLiteral("Зняти проведення"));
  printBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/print.svg")),
                              QStringLiteral("Друк / PDF"));
  auto * help = new QPushButton(QStringLiteral("Довідка"));
  auto * close = new QPushButton(QStringLiteral("Закрити"));
  auto * buttons = new QHBoxLayout;
  buttons->addWidget(saveBtn_);
  buttons->addWidget(postBtn_);
  buttons->addWidget(unpostBtn_);
  buttons->addWidget(printBtn_);
  buttons->addWidget(help);
  buttons->addStretch();
  buttons->addWidget(close);

  auto * hint = new QLabel(
      QStringLiteral("Картку створюють у довіднику. 631 — купили й одразу ввели; 154 — з "
                     "капітальних інвестицій; 00 — залишок на початок (знос з картки). "
                     "Амортизація починається з наступного місяця (НП(С)БО 8)."));
  hint->setWordWrap(true);

  auto * layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(hint);
  layout->addLayout(lineBar);
  layout->addWidget(table_, 1);
  layout->addWidget(new QLabel(QStringLiteral("Коментар")));
  layout->addWidget(comment_);
  layout->addWidget(reason_);
  layout->addLayout(buttons);

  QSet<qlonglong> added;
  for (const DocumentLine & line : document_.lines) {
    if (!line.intangibleId || added.contains(line.intangibleId.value)) {
      continue;
    }
    added.insert(line.intangibleId.value);
    addRow(line.intangibleId);
  }
  if (table_->rowCount() == 0) {
    addRow({});
  }

  connect(addLine, &QPushButton::clicked, this, [this]() {
    addRow({});
    refreshGate();
  });
  connect(removeLine, &QPushButton::clicked, this, [this]() {
    const int row = table_->currentRow();
    if (row >= 0) {
      table_->removeRow(row);
      refreshGate();
    }
  });
  connect(saveBtn_, &QPushButton::clicked, this, [this]() {
    const Status status = this->save();
    if (!status) {
      ui::showError(this, status);
    } else {
      ui::showToast(this, QStringLiteral("Збережено"));
    }
    refreshGate();
  });
  connect(postBtn_, &QPushButton::clicked, this, [this]() {
    const Status status = saveAndPost();
    if (!status) {
      ui::showError(this, status);
    } else {
      ui::showToast(this, QStringLiteral("Проведено"));
    }
    refreshGate();
  });
  connect(unpostBtn_, &QPushButton::clicked, this, &IaCommissionDialog::unpost);
  connect(printBtn_, &QPushButton::clicked, this, &IaCommissionDialog::printForm);
  connect(help, &QPushButton::clicked, this, &IaCommissionDialog::openHelp);
  connect(close, &QPushButton::clicked, this, &QDialog::reject);
  auto * f1 = new QShortcut(QKeySequence::HelpContents, this);
  connect(f1, &QShortcut::activated, this, &IaCommissionDialog::openHelp);
  connect(number_, &QLineEdit::textChanged, this, &IaCommissionDialog::refreshGate);
  connect(date_, &QDateEdit::dateChanged, this, &IaCommissionDialog::refreshGate);
  connect(counterparty_, &QComboBox::currentIndexChanged, this, &IaCommissionDialog::refreshGate);
  connect(correspondence_, &QComboBox::currentIndexChanged, this, &IaCommissionDialog::refreshGate);
  updatePostedUi();
  refreshGate();
}

void IaCommissionDialog::addRow(IntangibleId id)
{
  const int index = table_->rowCount();
  table_->insertRow(index);
  auto * combo = new QComboBox;
  ui::fillIntangibleCombo(combo, session_, id, true);
  table_->setCellWidget(index, 0, combo);
  connect(combo, &QComboBox::currentIndexChanged, this, &IaCommissionDialog::refreshGate);
}

AccountId IaCommissionDialog::correspondenceId() const
{
  return AccountId{correspondence_->currentData().toLongLong()};
}

ui::FormButtons IaCommissionDialog::buttons() const
{
  return {saveBtn_, postBtn_, unpostBtn_, printBtn_, reason_};
}

Status IaCommissionDialog::collect(Document & out) const
{
  out = document_;
  out.number = number_->text();
  out.date = date_->date();
  out.counterpartyId = ui::selectedCounterparty(counterparty_);
  out.comment = comment_->toPlainText();
  std::vector<IntangibleAsset> assets;
  for (int row = 0; row < table_->rowCount(); ++row) {
    auto * combo = qobject_cast<QComboBox *>(table_->cellWidget(row, 0));
    if (combo == nullptr) {
      continue;
    }
    const IntangibleId id = ui::selectedIntangible(combo);
    if (!id) {
      continue;
    }
    Result<IntangibleAsset> asset = session_.intangibles().findById(id);
    if (!asset) {
      return asset.asStatus();
    }
    assets.push_back(asset.value());
  }
  const Result<std::vector<DocumentLine>> lines =
      composeIaCommission(accounts_, correspondenceId(), assets);
  if (!lines) {
    return lines.asStatus();
  }
  out.lines = lines.value();
  const Result<CompanyProfile> company = session_.company().load();
  if (company && company.value().vatPayer) {
    return stampSupplierVat(out.lines, accounts_, company.value().vatRateBp);
  }
  return Status::ok();
}

Status IaCommissionDialog::save()
{
  Document collected;
  Status status = collect(collected);
  if (!status) {
    return status;
  }
  if (!collected.id) {
    Result<DocumentId> id = session_.documents().insert(collected);
    if (!id) {
      return id.asStatus();
    }
    document_ = collected;
    document_.id = id.value();
  } else {
    status = session_.documents().update(collected);
    if (!status) {
      return status;
    }
    document_ = collected;
  }
  persisted_ = true;
  updatePostedUi();
  return Status::ok();
}

Status IaCommissionDialog::saveAndPost()
{
  Status status = save();
  if (!status) {
    return status;
  }
  status = session_.posting().post(document_.id);
  if (!status) {
    return status;
  }
  Result<Document> loaded = session_.documents().findById(document_.id);
  if (loaded) {
    document_ = loaded.value();
  } else {
    document_.posted = true;
  }
  persisted_ = true;
  updatePostedUi();
  return Status::ok();
}

void IaCommissionDialog::unpost()
{
  if (!document_.id) {
    return;
  }
  const Status status = session_.posting().unpost(document_.id);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  Result<Document> loaded = session_.documents().findById(document_.id);
  if (loaded) {
    document_ = loaded.value();
  }
  document_.posted = false;
  persisted_ = true;
  updatePostedUi();
  refreshGate();
}

void IaCommissionDialog::printForm()
{
  Status status = document_.id ? Status::ok() : save();
  if (!status) {
    ui::showError(this, status);
    return;
  }
  Result<Document> loaded = session_.documents().findById(document_.id);
  if (!loaded) {
    ui::showError(this, loaded.error());
    return;
  }
  const Result<QString> html = PrintForms(session_.handle()).documentHtml(loaded.value());
  if (!html) {
    ui::showError(this, html.error());
    return;
  }
  ui::exportReport(this, documentTypeLabelUk(loaded.value().type),
                   QStringLiteral("fa-%1.pdf").arg(loaded.value().number), html.value(), QString(),
                   QString());
}

void IaCommissionDialog::refreshGate()
{
  ui::refreshDocumentButtons(session_, buttons(), document_.posted, date_->date(),
                             [this](Document & out) { return collect(out); });
}

void IaCommissionDialog::updatePostedUi()
{
  const bool locked = document_.posted;
  number_->setReadOnly(locked);
  date_->setReadOnly(locked);
  counterparty_->setEnabled(!locked);
  correspondence_->setEnabled(!locked);
  comment_->setReadOnly(locked);
  table_->setEnabled(!locked);
}

void IaCommissionDialog::openHelp()
{
  HelpBrowser::openTopic(this, QStringLiteral("intangibles.html"));
}

IaDepreciationDialog::IaDepreciationDialog(LedgerSession & session, Document document,
                                           QWidget * parent)
    : QDialog(parent), session_(session), document_(std::move(document))
{
  setWindowTitle(documentTypeLabelUk(DocumentType::IaDepreciation));
  resize(860, 500);
  const QDate date = document_.date.isValid() ? document_.date : QDate::currentDate();
  number_ = new QLineEdit(document_.number);
  year_ = new QSpinBox;
  year_->setRange(1991, 2100);
  year_->setValue(date.year());
  month_ = new QComboBox;
  const QString months[] = {
      QStringLiteral("січень"),  QStringLiteral("лютий"),    QStringLiteral("березень"),
      QStringLiteral("квітень"), QStringLiteral("травень"),  QStringLiteral("червень"),
      QStringLiteral("липень"),  QStringLiteral("серпень"),  QStringLiteral("вересень"),
      QStringLiteral("жовтень"), QStringLiteral("листопад"), QStringLiteral("грудень")};
  int m = 0;
  for (const QString & name : months) {
    month_->addItem(name, ++m);
  }
  month_->setCurrentIndex(date.month() - 1);
  auto * build = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/refresh.svg")),
                                 QStringLiteral("Сформувати"));
  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Номер"), number_);
  auto * period = new QHBoxLayout;
  period->addWidget(month_, 1);
  period->addWidget(year_);
  period->addWidget(build);
  form->addRow(QStringLiteral("Місяць"), period);

  table_ = new QTableWidget;
  table_->setColumnCount(3);
  table_->setHorizontalHeaderLabels(
      {QStringLiteral("НМА"), QStringLiteral("Зміст"), QStringLiteral("Сума")});
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);

  comment_ = new QTextEdit(document_.comment);
  comment_->setAcceptRichText(false);
  comment_->setMaximumHeight(70);
  reason_ = new QLabel;
  reason_->setWordWrap(true);
  saveBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                             QStringLiteral("Зберегти"));
  postBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/post.svg")),
                             QStringLiteral("Провести"));
  unpostBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/unpost.svg")),
                               QStringLiteral("Зняти проведення"));
  printBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/print.svg")),
                              QStringLiteral("Друк / PDF"));
  auto * help = new QPushButton(QStringLiteral("Довідка"));
  auto * close = new QPushButton(QStringLiteral("Закрити"));
  auto * buttons = new QHBoxLayout;
  buttons->addWidget(saveBtn_);
  buttons->addWidget(postBtn_);
  buttons->addWidget(unpostBtn_);
  buttons->addWidget(printBtn_);
  buttons->addWidget(help);
  buttons->addStretch();
  buttons->addWidget(close);

  auto * hint = new QLabel(
      QStringLiteral("Прямолінійна амортизація з наступного місяця після введення. Один "
                     "проведений документ на місяць. Рахунок витрат — з картки (типово 92)."));
  hint->setWordWrap(true);

  auto * layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(hint);
  layout->addWidget(table_, 1);
  layout->addWidget(new QLabel(QStringLiteral("Коментар")));
  layout->addWidget(comment_);
  layout->addWidget(reason_);
  layout->addLayout(buttons);

  if (!document_.lines.empty()) {
    showLines(document_.lines);
  }

  connect(build, &QPushButton::clicked, this, &IaDepreciationDialog::rebuild);
  connect(saveBtn_, &QPushButton::clicked, this, [this]() {
    const Status status = this->save();
    if (!status) {
      ui::showError(this, status);
    } else {
      ui::showToast(this, QStringLiteral("Збережено"));
    }
    refreshGate();
  });
  connect(postBtn_, &QPushButton::clicked, this, [this]() {
    const Status status = saveAndPost();
    if (!status) {
      ui::showError(this, status);
    } else {
      ui::showToast(this, QStringLiteral("Проведено"));
    }
    refreshGate();
  });
  connect(unpostBtn_, &QPushButton::clicked, this, &IaDepreciationDialog::unpost);
  connect(printBtn_, &QPushButton::clicked, this, &IaDepreciationDialog::printForm);
  connect(help, &QPushButton::clicked, this, &IaDepreciationDialog::openHelp);
  connect(close, &QPushButton::clicked, this, &QDialog::reject);
  auto * f1 = new QShortcut(QKeySequence::HelpContents, this);
  connect(f1, &QShortcut::activated, this, &IaDepreciationDialog::openHelp);
  connect(number_, &QLineEdit::textChanged, this, &IaDepreciationDialog::refreshGate);
  connect(year_, &QSpinBox::valueChanged, this, &IaDepreciationDialog::refreshGate);
  connect(month_, &QComboBox::currentIndexChanged, this, &IaDepreciationDialog::refreshGate);
  updatePostedUi();
  refreshGate();
}

void IaDepreciationDialog::showLines(const std::vector<DocumentLine> & lines)
{
  table_->setRowCount(static_cast<int>(lines.size()));
  int row = 0;
  for (const DocumentLine & line : lines) {
    QString label;
    if (line.intangibleId) {
      const Result<IntangibleAsset> asset = session_.intangibles().findById(line.intangibleId);
      if (asset) {
        label = asset.value().inventoryNo + QStringLiteral("  ") + asset.value().name;
      }
    }
    table_->setItem(row, 0, new QTableWidgetItem(label));
    table_->setItem(row, 1, new QTableWidgetItem(line.description));
    auto * sum = new QTableWidgetItem(line.amount.toDisplay());
    sum->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setItem(row, 2, sum);
    ++row;
  }
}

void IaDepreciationDialog::rebuild()
{
  if (document_.posted) {
    return;
  }
  const Result<Document> built =
      buildIaDepreciationDocument(session_.handle(), year_->value(), month_->currentData().toInt());
  if (!built) {
    ui::showError(this, built.error());
    return;
  }
  if (built.value().id) {
    document_ = built.value();
    number_->setText(document_.number);
  } else {
    document_.lines = built.value().lines;
    document_.comment = built.value().comment;
    document_.date = built.value().date;
    if (number_->text().trimmed().isEmpty()) {
      number_->setText(built.value().number);
    }
  }
  comment_->setText(document_.comment);
  showLines(document_.lines);
  refreshGate();
}

ui::FormButtons IaDepreciationDialog::buttons() const
{
  return {saveBtn_, postBtn_, unpostBtn_, printBtn_, reason_};
}

Status IaDepreciationDialog::collect(Document & out) const
{
  out = document_;
  out.type = DocumentType::IaDepreciation;
  out.number = number_->text();
  const QDate from(year_->value(), month_->currentData().toInt(), 1);
  out.date = from.addMonths(1).addDays(-1);
  out.comment = comment_->toPlainText();
  if (out.lines.empty()) {
    return Status::fail(
        Error::validation(QStringLiteral("натисніть «Сформувати», щоб порахувати амортизацію")));
  }
  return Status::ok();
}

Status IaDepreciationDialog::save()
{
  Document collected;
  Status status = collect(collected);
  if (!status) {
    return status;
  }
  if (!collected.id) {
    Result<DocumentId> id = session_.documents().insert(collected);
    if (!id) {
      return id.asStatus();
    }
    document_ = collected;
    document_.id = id.value();
  } else {
    status = session_.documents().update(collected);
    if (!status) {
      return status;
    }
    document_ = collected;
  }
  persisted_ = true;
  updatePostedUi();
  return Status::ok();
}

Status IaDepreciationDialog::saveAndPost()
{
  Status status = save();
  if (!status) {
    return status;
  }
  status = session_.posting().post(document_.id);
  if (!status) {
    return status;
  }
  Result<Document> loaded = session_.documents().findById(document_.id);
  if (loaded) {
    document_ = loaded.value();
  } else {
    document_.posted = true;
  }
  persisted_ = true;
  updatePostedUi();
  return Status::ok();
}

void IaDepreciationDialog::unpost()
{
  if (!document_.id) {
    return;
  }
  const Status status = session_.posting().unpost(document_.id);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  Result<Document> loaded = session_.documents().findById(document_.id);
  if (loaded) {
    document_ = loaded.value();
  }
  document_.posted = false;
  persisted_ = true;
  updatePostedUi();
  refreshGate();
}

void IaDepreciationDialog::printForm()
{
  Status status = document_.id ? Status::ok() : save();
  if (!status) {
    ui::showError(this, status);
    return;
  }
  Result<Document> loaded = session_.documents().findById(document_.id);
  if (!loaded) {
    ui::showError(this, loaded.error());
    return;
  }
  const Result<QString> html = PrintForms(session_.handle()).documentHtml(loaded.value());
  if (!html) {
    ui::showError(this, html.error());
    return;
  }
  ui::exportReport(this, documentTypeLabelUk(loaded.value().type),
                   QStringLiteral("amort-%1.pdf").arg(loaded.value().number), html.value(),
                   QString(), QString());
}

void IaDepreciationDialog::refreshGate()
{
  const QDate from(year_->value(), month_->currentData().toInt(), 1);
  ui::refreshDocumentButtons(session_, buttons(), document_.posted, from.addMonths(1).addDays(-1),
                             [this](Document & out) { return collect(out); });
}

void IaDepreciationDialog::updatePostedUi()
{
  const bool locked = document_.posted;
  number_->setReadOnly(locked);
  year_->setReadOnly(locked);
  month_->setEnabled(!locked);
  comment_->setReadOnly(locked);
}

void IaDepreciationDialog::openHelp()
{
  HelpBrowser::openTopic(this, QStringLiteral("intangibles.html"));
}

IaWriteOffDialog::IaWriteOffDialog(LedgerSession & session, Document document, QWidget * parent)
    : QDialog(parent), session_(session), document_(std::move(document))
{
  setWindowTitle(documentTypeLabelUk(document_.type));
  resize(920, 520);
  const Result<std::vector<Account>> accounts = session_.accounts().listPostable();
  if (accounts) {
    accounts_ = accounts.value();
  }
  number_ = new QLineEdit(document_.number);
  date_ = new QDateEdit(document_.date.isValid() ? document_.date : QDate::currentDate());
  date_->setCalendarPopup(true);
  date_->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
  auto * form = new QFormLayout;
  form->addRow(QStringLiteral("Номер"), number_);
  form->addRow(QStringLiteral("Дата"), date_);

  auto * addLine = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/add.svg")),
                                   QStringLiteral("Додати рядок"));
  auto * removeLine = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/delete.svg")),
                                      QStringLiteral("Видалити рядок"));
  auto * lineBar = new QHBoxLayout;
  lineBar->addWidget(addLine);
  lineBar->addWidget(removeLine);
  lineBar->addStretch();

  table_ = new QTableWidget;
  table_->setColumnCount(1);
  table_->setHorizontalHeaderLabels({QStringLiteral("Нематеріальний актив")});
  table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table_->verticalHeader()->setVisible(false);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);

  comment_ = new QTextEdit(document_.comment);
  comment_->setAcceptRichText(false);
  comment_->setMaximumHeight(70);
  reason_ = new QLabel;
  reason_->setWordWrap(true);
  saveBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/document.svg")),
                             QStringLiteral("Зберегти"));
  postBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/post.svg")),
                             QStringLiteral("Провести"));
  unpostBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/unpost.svg")),
                               QStringLiteral("Зняти проведення"));
  printBtn_ = new QPushButton(ui::themeIcon(QStringLiteral(":/help/icons/print.svg")),
                              QStringLiteral("Друк / PDF"));
  auto * help = new QPushButton(QStringLiteral("Довідка"));
  auto * close = new QPushButton(QStringLiteral("Закрити"));
  auto * buttons = new QHBoxLayout;
  buttons->addWidget(saveBtn_);
  buttons->addWidget(postBtn_);
  buttons->addWidget(unpostBtn_);
  buttons->addWidget(printBtn_);
  buttons->addWidget(help);
  buttons->addStretch();
  buttons->addWidget(close);

  auto * hint = new QLabel(
      QStringLiteral("Списання з балансу: Дт 133 Кт 12x на знос і Дт витрат Кт 12x на "
                     "залишкову. Якщо амортизацію за місяць списання ще не проведено, рушій "
                     "нарахує її в цьому документі (НП(С)БО 8: місяць вибуття ще "
                     "амортизується)."));
  hint->setWordWrap(true);

  auto * layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(hint);
  layout->addLayout(lineBar);
  layout->addWidget(table_, 1);
  layout->addWidget(new QLabel(QStringLiteral("Коментар")));
  layout->addWidget(comment_);
  layout->addWidget(reason_);
  layout->addLayout(buttons);

  QSet<qlonglong> added;
  for (const DocumentLine & line : document_.lines) {
    if (!line.intangibleId || added.contains(line.intangibleId.value)) {
      continue;
    }
    added.insert(line.intangibleId.value);
    addRow(line.intangibleId);
  }
  if (table_->rowCount() == 0) {
    addRow({});
  }

  connect(addLine, &QPushButton::clicked, this, [this]() {
    addRow({});
    refreshGate();
  });
  connect(removeLine, &QPushButton::clicked, this, [this]() {
    const int row = table_->currentRow();
    if (row >= 0) {
      table_->removeRow(row);
      refreshGate();
    }
  });
  connect(saveBtn_, &QPushButton::clicked, this, [this]() {
    const Status status = this->save();
    if (!status) {
      ui::showError(this, status);
    } else {
      ui::showToast(this, QStringLiteral("Збережено"));
    }
    refreshGate();
  });
  connect(postBtn_, &QPushButton::clicked, this, [this]() {
    const Status status = saveAndPost();
    if (!status) {
      ui::showError(this, status);
    } else {
      ui::showToast(this, QStringLiteral("Проведено"));
    }
    refreshGate();
  });
  connect(unpostBtn_, &QPushButton::clicked, this, &IaWriteOffDialog::unpost);
  connect(printBtn_, &QPushButton::clicked, this, &IaWriteOffDialog::printForm);
  connect(help, &QPushButton::clicked, this, &IaWriteOffDialog::openHelp);
  connect(close, &QPushButton::clicked, this, &QDialog::reject);
  auto * f1 = new QShortcut(QKeySequence::HelpContents, this);
  connect(f1, &QShortcut::activated, this, &IaWriteOffDialog::openHelp);
  connect(number_, &QLineEdit::textChanged, this, &IaWriteOffDialog::refreshGate);
  connect(date_, &QDateEdit::dateChanged, this, &IaWriteOffDialog::refreshGate);
  updatePostedUi();
  refreshGate();
}

void IaWriteOffDialog::addRow(IntangibleId id)
{
  const int index = table_->rowCount();
  table_->insertRow(index);
  auto * combo = new QComboBox;
  ui::fillIntangibleCombo(combo, session_, id, false, true);
  table_->setCellWidget(index, 0, combo);
  connect(combo, &QComboBox::currentIndexChanged, this, &IaWriteOffDialog::refreshGate);
}

ui::FormButtons IaWriteOffDialog::buttons() const
{
  return {saveBtn_, postBtn_, unpostBtn_, printBtn_, reason_};
}

Status IaWriteOffDialog::collect(Document & out) const
{
  out = document_;
  out.number = number_->text();
  out.date = date_->date();
  out.comment = comment_->toPlainText();
  out.counterpartyId = {};
  std::vector<IntangibleAsset> assets;
  QSet<qlonglong> seen;
  for (int row = 0; row < table_->rowCount(); ++row) {
    auto * combo = qobject_cast<QComboBox *>(table_->cellWidget(row, 0));
    if (combo == nullptr) {
      continue;
    }
    const IntangibleId id = ui::selectedIntangible(combo);
    if (!id || seen.contains(id.value)) {
      continue;
    }
    seen.insert(id.value);
    Result<IntangibleAsset> asset = session_.intangibles().findById(id);
    if (!asset) {
      return asset.asStatus();
    }
    assets.push_back(asset.value());
  }
  const Result<std::vector<DocumentLine>> lines =
      composeIaWriteOff(session_.handle(), accounts_, assets, out.date);
  if (!lines) {
    return lines.asStatus();
  }
  out.lines = lines.value();
  return Status::ok();
}

Status IaWriteOffDialog::save()
{
  Document collected;
  Status status = collect(collected);
  if (!status) {
    return status;
  }
  if (!collected.id) {
    Result<DocumentId> id = session_.documents().insert(collected);
    if (!id) {
      return id.asStatus();
    }
    document_ = collected;
    document_.id = id.value();
  } else {
    status = session_.documents().update(collected);
    if (!status) {
      return status;
    }
    document_ = collected;
  }
  persisted_ = true;
  updatePostedUi();
  return Status::ok();
}

Status IaWriteOffDialog::saveAndPost()
{
  Status status = save();
  if (!status) {
    return status;
  }
  status = session_.posting().post(document_.id);
  if (!status) {
    return status;
  }
  Result<Document> loaded = session_.documents().findById(document_.id);
  if (loaded) {
    document_ = loaded.value();
  } else {
    document_.posted = true;
  }
  persisted_ = true;
  updatePostedUi();
  return Status::ok();
}

void IaWriteOffDialog::unpost()
{
  if (!document_.id) {
    return;
  }
  const Status status = session_.posting().unpost(document_.id);
  if (!status) {
    ui::showError(this, status);
    return;
  }
  Result<Document> loaded = session_.documents().findById(document_.id);
  if (loaded) {
    document_ = loaded.value();
  }
  document_.posted = false;
  persisted_ = true;
  updatePostedUi();
  refreshGate();
}

void IaWriteOffDialog::printForm()
{
  Status status = document_.id ? Status::ok() : save();
  if (!status) {
    ui::showError(this, status);
    return;
  }
  Result<Document> loaded = session_.documents().findById(document_.id);
  if (!loaded) {
    ui::showError(this, loaded.error());
    return;
  }
  const Result<QString> html = PrintForms(session_.handle()).documentHtml(loaded.value());
  if (!html) {
    ui::showError(this, html.error());
    return;
  }
  ui::exportReport(this, documentTypeLabelUk(loaded.value().type),
                   QStringLiteral("fa-write-off-%1.pdf").arg(loaded.value().number), html.value(),
                   QString(), QString());
}

void IaWriteOffDialog::refreshGate()
{
  ui::refreshDocumentButtons(session_, buttons(), document_.posted, date_->date(),
                             [this](Document & out) { return collect(out); });
}

void IaWriteOffDialog::updatePostedUi()
{
  const bool locked = document_.posted;
  number_->setReadOnly(locked);
  date_->setReadOnly(locked);
  comment_->setReadOnly(locked);
  table_->setEnabled(!locked);
}

void IaWriteOffDialog::openHelp()
{
  HelpBrowser::openTopic(this, QStringLiteral("intangibles.html"));
}

} // namespace fin
