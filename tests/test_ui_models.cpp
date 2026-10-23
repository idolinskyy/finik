#include <catch2/catch_test_macros.hpp>

#include "ui/DocumentJournalModel.h"
#include "ui/models/AccountAnalysisModel.h"
#include "ui/models/AccountCardModel.h"
#include "ui/models/CounterpartyModel.h"
#include "ui/models/CounterpartyTurnoverModel.h"
#include "ui/models/NomenclatureModel.h"
#include "ui/models/SubcontoTrialBalanceModel.h"
#include "ui/models/TrialBalanceModel.h"

#include "fin/core/Money.h"
#include "fin/domain/Counterparty.h"
#include "fin/domain/Document.h"
#include "fin/domain/Nomenclature.h"
#include "fin/reports/Reports.h"

#include <QDate>
#include <QFont>

using namespace fin;

namespace {
QString modelDataString(const QAbstractItemModel & model, int row, int col)
{
  return model.data(model.index(row, col)).toString();
}
} // namespace

TEST_CASE("TrialBalanceModel renders balances and formats totals", "[ui][models]")
{
  TrialBalanceModel model;

  REQUIRE(model.columnCount() == TrialBalanceModel::kColumns);
  REQUIRE(model.headerData(0, Qt::Horizontal).toString() == QStringLiteral("Рахунок"));
  REQUIRE(model.headerData(1, Qt::Horizontal).toString() == QStringLiteral("Назва"));

  TrialBalance report;
  report.from = QDate(2026, 1, 1);
  report.to = QDate(2026, 1, 31);

  TrialBalanceRow r1;
  r1.accountId = AccountId{10};
  r1.code = QStringLiteral("311");
  r1.name = QStringLiteral("Поточний рахунок");
  r1.openingDebit = Money::fromCents(500000); // 5000.00
  r1.openingCredit = Money::zero();
  r1.turnoverDebit = Money::fromCents(200000);  // 2000.00
  r1.turnoverCredit = Money::fromCents(100000); // 1000.00
  r1.closingDebit = Money::fromCents(600000);   // 6000.00
  r1.closingCredit = Money::zero();
  report.rows.push_back(r1);

  TrialBalanceRow r2;
  r2.accountId = AccountId{20};
  r2.code = QStringLiteral("631");
  r2.name = QStringLiteral("Розрахунки з постачальниками");
  r2.openingDebit = Money::zero();
  r2.openingCredit = Money::fromCents(300000);
  r2.turnoverDebit = Money::fromCents(100000);
  r2.turnoverCredit = Money::fromCents(150000);
  r2.closingDebit = Money::zero();
  r2.closingCredit = Money::fromCents(350000);
  report.rows.push_back(r2);

  report.totals.name = QStringLiteral("Разом");
  report.totals.openingDebit = Money::fromCents(500000);
  report.totals.openingCredit = Money::fromCents(300000);
  report.totals.turnoverDebit = Money::fromCents(300000);
  report.totals.turnoverCredit = Money::fromCents(250000);
  report.totals.closingDebit = Money::fromCents(600000);
  report.totals.closingCredit = Money::fromCents(350000);

  model.setReport(report);

  // 2 рядки рахунків + 1 підсумковий рядок = 3
  REQUIRE(model.rowCount() == 3);

  // Перевірка нормального рядка
  REQUIRE(model.accountIdAt(0) == AccountId{10});
  REQUIRE(model.data(model.index(0, 0)).toString() == QStringLiteral("311"));
  REQUIRE(model.data(model.index(0, 1)).toString() == QStringLiteral("Поточний рахунок"));
  REQUIRE(model.data(model.index(0, 2)).toString() == r1.openingDebit.toDisplay());

  // Перевірка шрифту для звичайного рядка (не жирний)
  const QVariant normalFont = model.data(model.index(0, 0), Qt::FontRole);
  if (normalFont.isValid()) {
    REQUIRE_FALSE(normalFont.value<QFont>().bold());
  }

  // Перевірка підсумкового рядка (індекс 2)
  REQUIRE(model.accountIdAt(2).value == 0);
  REQUIRE(model.data(model.index(2, 1)).toString() == QStringLiteral("Разом"));
  REQUIRE(model.data(model.index(2, 4)).toString() == report.totals.turnoverDebit.toDisplay());
  const QVariant totalsFont = model.data(model.index(2, 0), Qt::FontRole);
  REQUIRE(totalsFont.isValid());
  REQUIRE(totalsFont.value<QFont>().bold());
}

TEST_CASE("AccountCardModel handles section rows and document mapping", "[ui][models]")
{
  AccountCardModel model;

  REQUIRE(model.columnCount() == AccountCardModel::kColumns);
  REQUIRE(model.headerData(0, Qt::Horizontal).toString() == QStringLiteral("Дата"));
  REQUIRE(model.headerData(1, Qt::Horizontal).toString() == QStringLiteral("Документ"));
  REQUIRE(model.headerData(2, Qt::Horizontal).toString() == QStringLiteral("Зміст"));

  AccountCard card;
  card.from = QDate(2026, 2, 1);
  card.to = QDate(2026, 2, 28);
  card.openingDebit = Money::fromCents(100000);
  card.openingCredit = Money::zero();

  AccountCardRow entry;
  entry.documentId = DocumentId{42};
  entry.date = QDate(2026, 2, 10);
  entry.documentNumber = QStringLiteral("ПКО-000001");
  entry.corrAccountCode = QStringLiteral("361");
  entry.debit = Money::fromCents(25000);
  entry.credit = Money::zero();
  entry.balanceDebit = Money::fromCents(125000);
  entry.balanceCredit = Money::zero();
  entry.description = QStringLiteral("Оплата від покупця");
  entry.debitSubconto.s1.kindId = QStringLiteral("counterparty");
  entry.debitSubconto.s1.entityId = 7;
  entry.debitSubconto.s1.name = QStringLiteral("ТОВ Покупець");
  card.rows.push_back(entry);

  card.closingDebit = Money::fromCents(125000);
  card.closingCredit = Money::zero();

  model.setReport(card);

  // 1 Opening + 1 Normal + 1 Closing = 3 рядки
  REQUIRE(model.rowCount() == 3);

  // Рядок 0: Сальдо на початок (колонка 2 = назва секції)
  REQUIRE(model.rowKindAt(0) == AccountCardModel::RowKind::Opening);
  REQUIRE(model.documentIdAt(0).value == 0);
  REQUIRE(model.data(model.index(0, 2)).toString() == QStringLiteral("Сальдо на початок"));
  REQUIRE(model.data(model.index(0, 5)).toString() == card.openingDebit.toDisplay());
  REQUIRE(model.data(model.index(0, 0), Qt::FontRole).value<QFont>().bold());

  // Рядок 1: Звичайна проводка
  REQUIRE(model.rowKindAt(1) == AccountCardModel::RowKind::Normal);
  REQUIRE(model.documentIdAt(1) == DocumentId{42});
  REQUIRE(model.data(model.index(1, 0)).toString() == QStringLiteral("10.02.2026"));
  REQUIRE(model.data(model.index(1, 1)).toString() == QStringLiteral("ПКО-000001"));
  REQUIRE(model.data(model.index(1, 2)).toString() == QStringLiteral("Оплата від покупця"));
  REQUIRE(model.data(model.index(1, 3)).toString() == entry.debit.toDisplay());
  REQUIRE(model.headerData(10, Qt::Horizontal).toString() == QStringLiteral("Субконто"));
  REQUIRE(model.data(model.index(1, 10)).toString() == QStringLiteral("ТОВ Покупець"));

  // Рядок 2: Сальдо на кінець (колонка 2 = назва секції)
  REQUIRE(model.rowKindAt(2) == AccountCardModel::RowKind::Closing);
  REQUIRE(model.documentIdAt(2).value == 0);
  REQUIRE(model.data(model.index(2, 2)).toString().contains(QStringLiteral("Сальдо на кінець")));
  REQUIRE(model.data(model.index(2, 5)).toString() == card.closingDebit.toDisplay());
  REQUIRE(model.data(model.index(2, 0), Qt::FontRole).value<QFont>().bold());
}

TEST_CASE("SubcontoTrialBalanceModel renders analytics rows and totals", "[ui][models]")
{
  SubcontoTrialBalanceModel model;
  REQUIRE(model.columnCount() == SubcontoTrialBalanceModel::kColumns);

  SubcontoTrialBalance report;
  report.subcontoKindName = QStringLiteral("Номенклатура");
  report.totals.closingDebit = Money::fromCents(1000000);

  SubcontoTrialBalanceRow row;
  row.subconto.kindId = QStringLiteral("nomenclature");
  row.subconto.entityId = 5;
  row.subconto.name = QStringLiteral("Ноутбук Dell");
  row.turnoverDebit = Money::fromCents(1000000);
  row.closingDebit = Money::fromCents(1000000);
  report.rows.push_back(row);
  report.totals.turnoverDebit = Money::fromCents(1000000);

  model.setReport(report);
  REQUIRE(model.rowCount() == 2);
  REQUIRE(model.headerData(0, Qt::Horizontal).toString() == QStringLiteral("Номенклатура"));
  REQUIRE(model.data(model.index(0, 0)).toString() == QStringLiteral("Ноутбук Dell"));
  REQUIRE(model.data(model.index(0, 3)).toString() == row.turnoverDebit.toDisplay());
  REQUIRE(model.data(model.index(1, 0)).toString() == QStringLiteral("Разом"));
  REQUIRE(model.data(model.index(1, 0), Qt::FontRole).value<QFont>().bold());
}

TEST_CASE("SubcontoTrialBalanceModel indents hierarchical rows", "[ui][models]")
{
  SubcontoTrialBalanceModel model;
  SubcontoTrialBalance report;
  report.hierarchical = true;
  report.subcontoKindName = QStringLiteral("Контрагенти / Договори");

  SubcontoTrialBalanceRow parent;
  parent.level = 0;
  parent.subconto.name = QStringLiteral("ТОВ Альфа");
  parent.closingDebit = Money::fromCents(15000);
  report.rows.push_back(parent);

  SubcontoTrialBalanceRow child;
  child.level = 1;
  child.subconto.name = QStringLiteral("Договір A-1");
  child.closingDebit = Money::fromCents(10000);
  report.rows.push_back(child);
  report.totals.closingDebit = Money::fromCents(15000);

  model.setReport(report);
  REQUIRE(model.data(model.index(0, 0)).toString() == QStringLiteral("ТОВ Альфа"));
  REQUIRE(model.data(model.index(0, 0), Qt::FontRole).value<QFont>().bold());
  REQUIRE(model.data(model.index(1, 0)).toString() == QStringLiteral("    Договір A-1"));
}

TEST_CASE("AccountAnalysisModel displays correspondences and turnover totals", "[ui][models]")
{
  AccountAnalysisModel model;

  REQUIRE(model.columnCount() == AccountAnalysisModel::kColumns);
  REQUIRE(model.headerData(0, Qt::Horizontal).toString() ==
          QStringLiteral("Кореспондуючий рахунок"));

  AccountAnalysis analysis;
  analysis.from = QDate(2026, 3, 1);
  analysis.to = QDate(2026, 3, 31);
  analysis.openingDebit = Money::fromCents(10000);
  analysis.openingCredit = Money::zero();

  AccountAnalysisCorrespondence corr;
  corr.corrAccountId = AccountId{5};
  corr.corrAccountCode = QStringLiteral("631");
  corr.corrAccountName = QStringLiteral("Постачальники");
  corr.debitTurnover = Money::zero();
  corr.creditTurnover = Money::fromCents(50000);
  analysis.correspondences.push_back(corr);

  analysis.turnoverDebit = Money::zero();
  analysis.turnoverCredit = Money::fromCents(50000);
  analysis.closingDebit = Money::zero();
  analysis.closingCredit = Money::fromCents(40000);

  model.setReport(analysis);

  // 1 SectionOpening + 1 Normal corr + 1 SectionTurnover + 1 SectionClosing = 4
  // рядки
  REQUIRE(model.rowCount() == 4);

  REQUIRE(model.rowKindAt(0) == AccountAnalysisModel::RowKind::SectionOpening);
  REQUIRE(model.data(model.index(0, 0)).toString().contains(QStringLiteral("Сальдо на початок")));
  REQUIRE(model.data(model.index(0, 0), Qt::FontRole).value<QFont>().bold());

  REQUIRE(model.rowKindAt(1) == AccountAnalysisModel::RowKind::Normal);
  REQUIRE(model.data(model.index(1, 0)).toString() == QStringLiteral("631"));
  REQUIRE(model.data(model.index(1, 1)).toString() == QStringLiteral("Постачальники"));
  REQUIRE(model.data(model.index(1, 3)).toString() == corr.creditTurnover.toDisplay());

  REQUIRE(model.rowKindAt(2) == AccountAnalysisModel::RowKind::SectionTurnover);
  REQUIRE(model.data(model.index(2, 0)).toString().contains(QStringLiteral("Обороти за період")));
  REQUIRE(model.data(model.index(2, 0), Qt::FontRole).value<QFont>().bold());

  REQUIRE(model.rowKindAt(3) == AccountAnalysisModel::RowKind::SectionClosing);
  REQUIRE(model.data(model.index(3, 0)).toString().contains(QStringLiteral("Сальдо на кінець")));
  REQUIRE(model.data(model.index(3, 0), Qt::FontRole).value<QFont>().bold());
}

TEST_CASE("CounterpartyTurnoverModel handles turnover rows and totals", "[ui][models]")
{
  CounterpartyTurnoverModel model;

  REQUIRE(model.columnCount() == CounterpartyTurnoverModel::kColumns);
  REQUIRE(model.headerData(0, Qt::Horizontal).toString() == QStringLiteral("Контрагент"));

  CounterpartyTurnover turnover;
  turnover.from = QDate(2026, 4, 1);
  turnover.to = QDate(2026, 4, 30);

  CounterpartyTurnoverRow row;
  row.counterpartyId = CounterpartyId{101};
  row.name = QStringLiteral("ТОВ Епіцентр");
  row.openingDebit = Money::fromCents(10000);
  row.openingCredit = Money::zero();
  row.turnoverDebit = Money::fromCents(5000);
  row.turnoverCredit = Money::fromCents(3000);
  row.closingDebit = Money::fromCents(12000);
  row.closingCredit = Money::zero();
  turnover.rows.push_back(row);

  turnover.totals.name = QStringLiteral("Разом");
  turnover.totals.openingDebit = row.openingDebit;
  turnover.totals.openingCredit = row.openingCredit;
  turnover.totals.turnoverDebit = row.turnoverDebit;
  turnover.totals.turnoverCredit = row.turnoverCredit;
  turnover.totals.closingDebit = row.closingDebit;
  turnover.totals.closingCredit = row.closingCredit;

  model.setReport(turnover);

  // 1 рядок контрагента + 1 totals = 2 рядки
  REQUIRE(model.rowCount() == 2);

  REQUIRE(model.counterpartyIdAt(0) == CounterpartyId{101});
  REQUIRE(model.data(model.index(0, 0)).toString() == QStringLiteral("ТОВ Епіцентр"));

  // Підсумковий рядок
  REQUIRE(model.counterpartyIdAt(1).value == 0);
  REQUIRE(model.data(model.index(1, 0)).toString() == QStringLiteral("Разом"));
  REQUIRE(model.data(model.index(1, 0), Qt::FontRole).value<QFont>().bold());
}

TEST_CASE("DocumentJournalModel maps document headers and roles", "[ui][models]")
{
  DocumentJournalModel model;

  REQUIRE(model.columnCount() == 6);
  REQUIRE(model.headerData(0, Qt::Horizontal).toString() == QStringLiteral("Дата"));
  REQUIRE(model.headerData(1, Qt::Horizontal).toString() == QStringLiteral("Номер"));
  REQUIRE(model.headerData(2, Qt::Horizontal).toString() == QStringLiteral("Тип"));

  std::vector<DocumentHeader> docs;
  DocumentHeader doc1;
  doc1.id = DocumentId{1};
  doc1.number = QStringLiteral("РТ-000001");
  doc1.date = QDate(2026, 5, 1);
  doc1.type = DocumentType::Sale;
  doc1.counterpartyName = QStringLiteral("ТОВ Покупець");
  doc1.total = Money::fromCents(125000);
  doc1.posted = true;
  doc1.comment = QStringLiteral("Тестова накладна");
  docs.push_back(doc1);

  DocumentHeader doc2;
  doc2.id = DocumentId{2};
  doc2.number = QStringLiteral("ПКО-000002");
  doc2.date = QDate(2026, 5, 2);
  doc2.type = DocumentType::CashIn;
  doc2.total = Money::fromCents(50000);
  doc2.posted = false;
  docs.push_back(doc2);

  model.setDocuments(docs);

  REQUIRE(model.rowCount() == 2);

  // Перевірка першого документа
  REQUIRE(model.documentAt(0).id == DocumentId{1});
  REQUIRE(model.data(model.index(0, 0)).toString() == QStringLiteral("01.05.2026"));
  REQUIRE(model.data(model.index(0, 1)).toString() == QStringLiteral("РТ-000001"));
  REQUIRE(model.data(model.index(0, 3)).toString() == QStringLiteral("ТОВ Покупець"));
  REQUIRE(model.data(model.index(0, 4)).toString() == doc1.total.toDisplay());
  REQUIRE(model.data(model.index(0, 5)).toString() == QStringLiteral("Так"));
  REQUIRE(model.data(model.index(1, 5)).toString() == QStringLiteral("Ні"));
}

TEST_CASE("CounterpartyModel and NomenclatureModel provide directory data", "[ui][models]")
{
  SECTION("CounterpartyModel")
  {
    CounterpartyModel cpModel;
    REQUIRE(cpModel.columnCount() == CounterpartyModel::kColumns);

    Counterparty cp;
    cp.id = CounterpartyId{15};
    cp.name = QStringLiteral("АТ Монобанк");
    cp.edrpou = QStringLiteral("99887766");
    cp.iban = QStringLiteral("UA112233445566778899001122334");

    cpModel.setCounterparties({cp});
    REQUIRE(cpModel.rowCount() == 1);
    REQUIRE(cpModel.idAt(0) == CounterpartyId{15});
    // Колонка 0 - ЄДРПОУ, колонка 1 - Назва
    REQUIRE(modelDataString(cpModel, 0, 0) == QStringLiteral("99887766"));
    REQUIRE(modelDataString(cpModel, 0, 1) == QStringLiteral("АТ Монобанк"));
    REQUIRE(cpModel.data(cpModel.index(0, 0), Qt::UserRole).toLongLong() == 15);
  }

  SECTION("NomenclatureModel")
  {
    NomenclatureModel nomModel;
    REQUIRE(nomModel.columnCount() == NomenclatureModel::kColumns);

    Nomenclature nom;
    nom.id = NomenclatureId{77};
    nom.name = QStringLiteral("Кава зернова 1кг");
    nom.sku = QStringLiteral("COF-01");
    nom.unit = QStringLiteral("кг");

    nomModel.setNomenclature({nom});
    REQUIRE(nomModel.rowCount() == 1);
    REQUIRE(nomModel.idAt(0) == NomenclatureId{77});
    // Колонка 0 - Артикул, колонка 1 - Назва
    REQUIRE(modelDataString(nomModel, 0, 0) == QStringLiteral("COF-01"));
    REQUIRE(modelDataString(nomModel, 0, 1) == QStringLiteral("Кава зернова 1кг"));
    REQUIRE(nomModel.data(nomModel.index(0, 0), Qt::UserRole).toLongLong() == 77);
  }
}
