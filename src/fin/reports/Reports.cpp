#include "fin/reports/Reports.h"

#include "fin/core/Checked.h"
#include "fin/core/Sql.h"
#include "fin/db/AccountRepository.h"
#include "fin/db/CompanyRepository.h"
#include "fin/db/CounterpartyRepository.h"
#include "fin/db/SubcontoRepository.h"
#include "fin/domain/ChartCodes.h"
#include "fin/domain/Document.h"
#include "fin/domain/FixedAsset.h"
#include "fin/domain/IntangibleAsset.h"

#include <algorithm>
#include <QHash>
#include <QLatin1String>
#include <QList>

namespace fin {
namespace {

struct Agg
{
  std::int64_t openDt = 0;
  std::int64_t openCt = 0;
  std::int64_t turnDt = 0;
  std::int64_t turnCt = 0;
};

struct FxAgg
{
  QString currencyCode;
  bool mixed = false;
  std::int64_t openDt = 0;
  std::int64_t openCt = 0;
  std::int64_t turnDt = 0;
  std::int64_t turnCt = 0;

  void noteCurrency(const QString & code)
  {
    if (code.isEmpty() || code.compare(QLatin1String("UAH"), Qt::CaseInsensitive) == 0) {
      return;
    }
    if (currencyCode.isEmpty()) {
      currencyCode = code;
    } else if (currencyCode.compare(code, Qt::CaseInsensitive) != 0) {
      mixed = true;
    }
  }
};

} // namespace

TrialBalanceReport::TrialBalanceReport(QSqlDatabase db) : db_(std::move(db)) {}

Result<TrialBalance> TrialBalanceReport::build(QDate from, QDate to) const
{
  if (!from.isValid() || !to.isValid() || from > to) {
    return Result<TrialBalance>::fail(Error::invalidArgument(QStringLiteral("некоректний період")));
  }

  AccountRepository accounts(db_);
  Result<std::vector<Account>> list = accounts.listPostable();
  if (!list) {
    return Result<TrialBalance>::fail(list.error());
  }

  sql::Query query(db_);
  Status status =
      query.exec(QStringLiteral("SELECT debit_account_id, credit_account_id, amount_cents, date, "
                                "currency_code, currency_amount_cents FROM ledger_entries"));
  if (!status) {
    return Result<TrialBalance>::fail(status.error());
  }

  const QString fromIso = from.toString(Qt::ISODate);
  const QString toIso = to.toString(Qt::ISODate);

  QHash<std::int64_t, Agg> byAccount;
  QHash<std::int64_t, FxAgg> byAccountFx;
  while (query.next()) {
    const std::int64_t debitId = query.value(0).toLongLong();
    const std::int64_t creditId = query.value(1).toLongLong();
    const std::int64_t amount = query.value(2).toLongLong();
    const QString date = query.value(3).toString();
    const QString curCode = query.value(4).toString();
    const std::int64_t curAmt = query.value(5).toLongLong();
    if (date < fromIso) {
      Status st = addTo(byAccount[debitId].openDt, amount, QStringLiteral("ОСВ"));
      if (!st) {
        return Result<TrialBalance>::fail(st.error());
      }
      st = addTo(byAccount[creditId].openCt, amount, QStringLiteral("ОСВ"));
      if (!st) {
        return Result<TrialBalance>::fail(st.error());
      }
      if (curAmt != 0) {
        byAccountFx[debitId].noteCurrency(curCode);
        byAccountFx[creditId].noteCurrency(curCode);
        st = addTo(byAccountFx[debitId].openDt, curAmt, QStringLiteral("ОСВ валюта"));
        if (!st) {
          return Result<TrialBalance>::fail(st.error());
        }
        st = addTo(byAccountFx[creditId].openCt, curAmt, QStringLiteral("ОСВ валюта"));
        if (!st) {
          return Result<TrialBalance>::fail(st.error());
        }
      }
    } else if (date <= toIso) {
      Status st = addTo(byAccount[debitId].turnDt, amount, QStringLiteral("ОСВ"));
      if (!st) {
        return Result<TrialBalance>::fail(st.error());
      }
      st = addTo(byAccount[creditId].turnCt, amount, QStringLiteral("ОСВ"));
      if (!st) {
        return Result<TrialBalance>::fail(st.error());
      }
      if (curAmt != 0) {
        byAccountFx[debitId].noteCurrency(curCode);
        byAccountFx[creditId].noteCurrency(curCode);
        st = addTo(byAccountFx[debitId].turnDt, curAmt, QStringLiteral("ОСВ валюта"));
        if (!st) {
          return Result<TrialBalance>::fail(st.error());
        }
        st = addTo(byAccountFx[creditId].turnCt, curAmt, QStringLiteral("ОСВ валюта"));
        if (!st) {
          return Result<TrialBalance>::fail(st.error());
        }
      }
    }
  }

  TrialBalance report;
  report.from = from;
  report.to = to;
  std::int64_t totOpenDt = 0;
  std::int64_t totOpenCt = 0;
  std::int64_t totTurnDt = 0;
  std::int64_t totTurnCt = 0;
  std::int64_t totCloseDt = 0;
  std::int64_t totCloseCt = 0;

  for (const Account & account : list.value()) {
    const Agg agg = byAccount.value(account.id.value);
    TrialBalanceRow row;
    row.accountId = account.id;
    row.code = account.code;
    row.name = account.name;
    Status split = splitSigned(agg.openDt - agg.openCt, row.openingDebit, row.openingCredit);
    if (!split) {
      return Result<TrialBalance>::fail(split.error());
    }
    row.turnoverDebit = Money::fromCents(agg.turnDt);
    row.turnoverCredit = Money::fromCents(agg.turnCt);
    const std::int64_t closeNet = (agg.openDt - agg.openCt) + agg.turnDt - agg.turnCt;
    split = splitSigned(closeNet, row.closingDebit, row.closingCredit);
    if (!split) {
      return Result<TrialBalance>::fail(split.error());
    }
    const FxAgg fx = byAccountFx.value(account.id.value);
    if (!fx.mixed && !fx.currencyCode.isEmpty()) {
      row.currencyCode = fx.currencyCode;
      split = splitSigned(fx.openDt - fx.openCt, row.openingForeignDebit, row.openingForeignCredit);
      if (!split) {
        return Result<TrialBalance>::fail(split.error());
      }
      const std::int64_t fxClose = (fx.openDt - fx.openCt) + fx.turnDt - fx.turnCt;
      split = splitSigned(fxClose, row.closingForeignDebit, row.closingForeignCredit);
      if (!split) {
        return Result<TrialBalance>::fail(split.error());
      }
    }
    report.rows.push_back(row);

    Status add = addTo(totOpenDt, row.openingDebit.cents(), QStringLiteral("ОСВ"));
    if (!add) {
      return Result<TrialBalance>::fail(add.error());
    }
    add = addTo(totOpenCt, row.openingCredit.cents(), QStringLiteral("ОСВ"));
    if (!add) {
      return Result<TrialBalance>::fail(add.error());
    }
    add = addTo(totTurnDt, row.turnoverDebit.cents(), QStringLiteral("ОСВ"));
    if (!add) {
      return Result<TrialBalance>::fail(add.error());
    }
    add = addTo(totTurnCt, row.turnoverCredit.cents(), QStringLiteral("ОСВ"));
    if (!add) {
      return Result<TrialBalance>::fail(add.error());
    }
    add = addTo(totCloseDt, row.closingDebit.cents(), QStringLiteral("ОСВ"));
    if (!add) {
      return Result<TrialBalance>::fail(add.error());
    }
    add = addTo(totCloseCt, row.closingCredit.cents(), QStringLiteral("ОСВ"));
    if (!add) {
      return Result<TrialBalance>::fail(add.error());
    }
  }

  report.totals.name = QStringLiteral("Разом");
  report.totals.openingDebit = Money::fromCents(totOpenDt);
  report.totals.openingCredit = Money::fromCents(totOpenCt);
  report.totals.turnoverDebit = Money::fromCents(totTurnDt);
  report.totals.turnoverCredit = Money::fromCents(totTurnCt);
  report.totals.closingDebit = Money::fromCents(totCloseDt);
  report.totals.closingCredit = Money::fromCents(totCloseCt);
  return Result<TrialBalance>::ok(std::move(report));
}

AccountCardReport::AccountCardReport(QSqlDatabase db) : db_(std::move(db)) {}

Result<AccountCard> AccountCardReport::build(AccountId accountId, QDate from, QDate to,
                                             CounterpartyId counterparty) const
{
  if (!from.isValid() || !to.isValid() || from > to) {
    return Result<AccountCard>::fail(Error::invalidArgument(QStringLiteral("некоректний період")));
  }

  AccountRepository accounts(db_);
  Result<Account> account = accounts.findById(accountId);
  if (!account) {
    return Result<AccountCard>::fail(account.error());
  }
  if (account.value().isFolder) {
    return Result<AccountCard>::fail(
        Error::validation(QStringLiteral("картка доступна лише для субрахунків")));
  }

  const QString cpFilter = QStringLiteral(" AND (:cp = 0 OR counterparty_id = :cp2)");
  sql::Query openQuery(db_);
  Status status = openQuery.prepare(
      QStringLiteral(
          "SELECT "
          "COALESCE(SUM(CASE WHEN debit_account_id = :id THEN amount_cents ELSE 0 END), 0), "
          "COALESCE(SUM(CASE WHEN credit_account_id = :id2 THEN amount_cents ELSE 0 END), 0) "
          "FROM ledger_entries WHERE date < :from AND "
          "(debit_account_id = :id3 OR credit_account_id = :id4)") +
      cpFilter);
  if (!status) {
    return Result<AccountCard>::fail(status.error());
  }
  const qlonglong id = static_cast<qlonglong>(accountId.value);
  const qlonglong cp = static_cast<qlonglong>(counterparty.value);
  openQuery.bind(QStringLiteral(":id"), id);
  openQuery.bind(QStringLiteral(":id2"), id);
  openQuery.bind(QStringLiteral(":id3"), id);
  openQuery.bind(QStringLiteral(":id4"), id);
  openQuery.bind(QStringLiteral(":from"), from.toString(Qt::ISODate));
  openQuery.bind(QStringLiteral(":cp"), cp);
  openQuery.bind(QStringLiteral(":cp2"), cp);
  status = openQuery.exec();
  if (!status) {
    return Result<AccountCard>::fail(status.error());
  }

  std::int64_t openDt = 0;
  std::int64_t openCt = 0;
  if (openQuery.next()) {
    openDt = openQuery.value(0).toLongLong();
    openCt = openQuery.value(1).toLongLong();
  }

  sql::Query query(db_);
  status = query.prepare(
      QStringLiteral("SELECT e.date, e.amount_cents, e.debit_account_id, e.credit_account_id, "
                     "e.description, e.document_id, d.number, d.type, e.id, "
                     "e.dt_subconto1_kind, e.dt_subconto1_id, e.dt_subconto2_kind, "
                     "e.dt_subconto2_id, e.dt_subconto3_kind, e.dt_subconto3_id, "
                     "e.ct_subconto1_kind, e.ct_subconto1_id, e.ct_subconto2_kind, "
                     "e.ct_subconto2_id, e.ct_subconto3_kind, e.ct_subconto3_id, "
                     "e.currency_code, e.currency_amount_cents, e.currency_rate_micros "
                     "FROM ledger_entries e "
                     "JOIN documents d ON d.id = e.document_id "
                     "WHERE (e.debit_account_id = :id OR e.credit_account_id = :id2) "
                     "AND e.date >= :from AND e.date <= :to") +
      QStringLiteral(" AND (:cp = 0 OR e.counterparty_id = :cp2) "
                     "ORDER BY e.date, e.id"));
  if (!status) {
    return Result<AccountCard>::fail(status.error());
  }
  query.bind(QStringLiteral(":id"), id);
  query.bind(QStringLiteral(":id2"), id);
  query.bind(QStringLiteral(":from"), from.toString(Qt::ISODate));
  query.bind(QStringLiteral(":to"), to.toString(Qt::ISODate));
  query.bind(QStringLiteral(":cp"), cp);
  query.bind(QStringLiteral(":cp2"), cp);
  status = query.exec();
  if (!status) {
    return Result<AccountCard>::fail(status.error());
  }

  AccountCard card;
  card.accountId = accountId;
  card.code = account.value().code;
  card.name = account.value().name;
  card.from = from;
  card.to = to;
  Status split = splitSigned(openDt - openCt, card.openingDebit, card.openingCredit);
  if (!split) {
    return Result<AccountCard>::fail(split.error());
  }

  std::int64_t signedBalance = openDt - openCt;
  std::int64_t turnDt = 0;
  std::int64_t turnCt = 0;
  SubcontoRepository subcontoRepo(db_);

  while (query.next()) {
    AccountCardRow row;
    row.date = QDate::fromString(query.value(0).toString(), Qt::ISODate);
    const std::int64_t amount = query.value(1).toLongLong();
    const std::int64_t debitId = query.value(2).toLongLong();
    const std::int64_t creditId = query.value(3).toLongLong();
    row.description = query.value(4).toString();
    row.documentId = DocumentId{query.value(5).toLongLong()};
    row.documentNumber = query.value(6).toString();
    const auto type = documentTypeFromString(query.value(7).toString());
    row.documentTypeLabel = type ? documentTypeLabelUk(*type) : query.value(7).toString();

    row.debitSubconto.s1.kindId = query.value(9).toString();
    row.debitSubconto.s1.entityId = query.value(10).toLongLong();
    row.debitSubconto.s2.kindId = query.value(11).toString();
    row.debitSubconto.s2.entityId = query.value(12).toLongLong();
    row.debitSubconto.s3.kindId = query.value(13).toString();
    row.debitSubconto.s3.entityId = query.value(14).toLongLong();

    row.creditSubconto.s1.kindId = query.value(15).toString();
    row.creditSubconto.s1.entityId = query.value(16).toLongLong();
    row.creditSubconto.s2.kindId = query.value(17).toString();
    row.creditSubconto.s2.entityId = query.value(18).toLongLong();
    row.creditSubconto.s3.kindId = query.value(19).toString();
    row.creditSubconto.s3.entityId = query.value(20).toLongLong();

    row.currencyCode = query.value(21).toString();
    const std::int64_t curAmt = query.value(22).toLongLong();
    row.currencyRateMicros = query.value(23).toLongLong();

    static_cast<void>(subcontoRepo.resolveNames(row.debitSubconto));
    static_cast<void>(subcontoRepo.resolveNames(row.creditSubconto));

    const auto corrRes =
        accounts.findById(AccountId{debitId == accountId.value ? creditId : debitId});
    if (corrRes) {
      row.corrAccountCode = corrRes.value().code;
    }

    if (debitId == accountId.value) {
      row.debit = Money::fromCents(amount);
      if (!row.currencyCode.isEmpty()) {
        row.currencyDebit = Money::fromCents(curAmt);
      }
      Status add = addTo(signedBalance, amount, QStringLiteral("картка"));
      if (!add) {
        return Result<AccountCard>::fail(add.error());
      }
      add = addTo(turnDt, amount, QStringLiteral("картка"));
      if (!add) {
        return Result<AccountCard>::fail(add.error());
      }
    } else {
      row.credit = Money::fromCents(amount);
      if (!row.currencyCode.isEmpty()) {
        row.currencyCredit = Money::fromCents(curAmt);
      }
      Status add = addTo(signedBalance, -amount, QStringLiteral("картка"));
      if (!add) {
        return Result<AccountCard>::fail(add.error());
      }
      add = addTo(turnCt, amount, QStringLiteral("картка"));
      if (!add) {
        return Result<AccountCard>::fail(add.error());
      }
    }
    split = splitSigned(signedBalance, row.balanceDebit, row.balanceCredit);
    if (!split) {
      return Result<AccountCard>::fail(split.error());
    }
    card.rows.push_back(row);
  }

  card.turnoverDebit = Money::fromCents(turnDt);
  card.turnoverCredit = Money::fromCents(turnCt);
  split = splitSigned(signedBalance, card.closingDebit, card.closingCredit);
  if (!split) {
    return Result<AccountCard>::fail(split.error());
  }
  return Result<AccountCard>::ok(std::move(card));
}

Result<AccountCard> AccountCardReport::buildWithSubconto(AccountId accountId, QDate from, QDate to,
                                                         const QString & subcontoKindId,
                                                         std::int64_t entityId) const
{
  if (!from.isValid() || !to.isValid() || from > to) {
    return Result<AccountCard>::fail(Error::invalidArgument(QStringLiteral("некоректний період")));
  }
  AccountRepository accounts(db_);
  Result<Account> account = accounts.findById(accountId);
  if (!account) {
    return Result<AccountCard>::fail(account.error());
  }
  if (account.value().isFolder) {
    return Result<AccountCard>::fail(
        Error::validation(QStringLiteral("картка доступна лише для субрахунків")));
  }

  const qlonglong id = static_cast<qlonglong>(accountId.value);
  const qlonglong eid = static_cast<qlonglong>(entityId);

  sql::Query openQuery(db_);
  Status status = openQuery.prepare(QStringLiteral(
      "SELECT "
      "COALESCE(SUM(CASE WHEN debit_account_id = :id THEN amount_cents ELSE 0 END), 0), "
      "COALESCE(SUM(CASE WHEN credit_account_id = :id2 THEN amount_cents ELSE 0 END), 0) "
      "FROM ledger_entries WHERE date < :from AND "
      "((debit_account_id = :id3 AND ((dt_subconto1_kind = :k1 AND dt_subconto1_id = :eid1) OR "
      "(dt_subconto2_kind = :k2 AND dt_subconto2_id = :eid2) OR (dt_subconto3_kind = :k3 AND "
      "dt_subconto3_id = :eid3))) OR "
      " (credit_account_id = :id4 AND ((ct_subconto1_kind = :k4 AND ct_subconto1_id = :eid4) OR "
      "(ct_subconto2_kind = :k5 AND ct_subconto2_id = :eid5) OR (ct_subconto3_kind = :k6 AND "
      "ct_subconto3_id = :eid6))))"));
  if (!status) {
    return Result<AccountCard>::fail(status.error());
  }
  openQuery.bind(QStringLiteral(":id"), id);
  openQuery.bind(QStringLiteral(":id2"), id);
  openQuery.bind(QStringLiteral(":id3"), id);
  openQuery.bind(QStringLiteral(":id4"), id);
  openQuery.bind(QStringLiteral(":from"), from.toString(Qt::ISODate));
  openQuery.bind(QStringLiteral(":k1"), subcontoKindId);
  openQuery.bind(QStringLiteral(":k2"), subcontoKindId);
  openQuery.bind(QStringLiteral(":k3"), subcontoKindId);
  openQuery.bind(QStringLiteral(":k4"), subcontoKindId);
  openQuery.bind(QStringLiteral(":k5"), subcontoKindId);
  openQuery.bind(QStringLiteral(":k6"), subcontoKindId);
  openQuery.bind(QStringLiteral(":eid1"), eid);
  openQuery.bind(QStringLiteral(":eid2"), eid);
  openQuery.bind(QStringLiteral(":eid3"), eid);
  openQuery.bind(QStringLiteral(":eid4"), eid);
  openQuery.bind(QStringLiteral(":eid5"), eid);
  openQuery.bind(QStringLiteral(":eid6"), eid);

  status = openQuery.exec();
  if (!status) {
    return Result<AccountCard>::fail(status.error());
  }
  std::int64_t openDt = 0;
  std::int64_t openCt = 0;
  if (openQuery.next()) {
    openDt = openQuery.value(0).toLongLong();
    openCt = openQuery.value(1).toLongLong();
  }

  sql::Query query(db_);
  status = query.prepare(QStringLiteral(
      "SELECT e.date, e.amount_cents, e.debit_account_id, e.credit_account_id, "
      "e.description, e.document_id, d.number, d.type, e.id, "
      "e.dt_subconto1_kind, e.dt_subconto1_id, e.dt_subconto2_kind, e.dt_subconto2_id, "
      "e.dt_subconto3_kind, e.dt_subconto3_id, "
      "e.ct_subconto1_kind, e.ct_subconto1_id, e.ct_subconto2_kind, e.ct_subconto2_id, "
      "e.ct_subconto3_kind, e.ct_subconto3_id "
      "FROM ledger_entries e "
      "JOIN documents d ON d.id = e.document_id "
      "WHERE e.date >= :from AND e.date <= :to AND "
      "((e.debit_account_id = :id AND ((e.dt_subconto1_kind = :k1 AND e.dt_subconto1_id = :eid1) "
      "OR (e.dt_subconto2_kind = :k2 AND e.dt_subconto2_id = :eid2) OR (e.dt_subconto3_kind = :k3 "
      "AND e.dt_subconto3_id = :eid3))) OR "
      " (e.credit_account_id = :id2 AND ((e.ct_subconto1_kind = :k4 AND e.ct_subconto1_id = :eid4) "
      "OR (e.ct_subconto2_kind = :k5 AND e.ct_subconto2_id = :eid5) OR (e.ct_subconto3_kind = :k6 "
      "AND e.ct_subconto3_id = :eid6)))) "
      "ORDER BY e.date, e.id"));
  if (!status) {
    return Result<AccountCard>::fail(status.error());
  }
  query.bind(QStringLiteral(":from"), from.toString(Qt::ISODate));
  query.bind(QStringLiteral(":to"), to.toString(Qt::ISODate));
  query.bind(QStringLiteral(":id"), id);
  query.bind(QStringLiteral(":id2"), id);
  query.bind(QStringLiteral(":k1"), subcontoKindId);
  query.bind(QStringLiteral(":k2"), subcontoKindId);
  query.bind(QStringLiteral(":k3"), subcontoKindId);
  query.bind(QStringLiteral(":k4"), subcontoKindId);
  query.bind(QStringLiteral(":k5"), subcontoKindId);
  query.bind(QStringLiteral(":k6"), subcontoKindId);
  query.bind(QStringLiteral(":eid1"), eid);
  query.bind(QStringLiteral(":eid2"), eid);
  query.bind(QStringLiteral(":eid3"), eid);
  query.bind(QStringLiteral(":eid4"), eid);
  query.bind(QStringLiteral(":eid5"), eid);
  query.bind(QStringLiteral(":eid6"), eid);

  status = query.exec();
  if (!status) {
    return Result<AccountCard>::fail(status.error());
  }

  AccountCard card;
  card.accountId = accountId;
  card.code = account.value().code;
  card.name = account.value().name;
  card.from = from;
  card.to = to;
  Status split = splitSigned(openDt - openCt, card.openingDebit, card.openingCredit);
  if (!split) {
    return Result<AccountCard>::fail(split.error());
  }

  std::int64_t signedBalance = openDt - openCt;
  std::int64_t turnDt = 0;
  std::int64_t turnCt = 0;
  SubcontoRepository subcontoRepo(db_);

  while (query.next()) {
    AccountCardRow row;
    row.date = QDate::fromString(query.value(0).toString(), Qt::ISODate);
    const std::int64_t amount = query.value(1).toLongLong();
    const std::int64_t debitId = query.value(2).toLongLong();
    const std::int64_t creditId = query.value(3).toLongLong();
    row.description = query.value(4).toString();
    row.documentId = DocumentId{query.value(5).toLongLong()};
    row.documentNumber = query.value(6).toString();
    const auto type = documentTypeFromString(query.value(7).toString());
    row.documentTypeLabel = type ? documentTypeLabelUk(*type) : query.value(7).toString();

    row.debitSubconto.s1.kindId = query.value(9).toString();
    row.debitSubconto.s1.entityId = query.value(10).toLongLong();
    row.debitSubconto.s2.kindId = query.value(11).toString();
    row.debitSubconto.s2.entityId = query.value(12).toLongLong();
    row.debitSubconto.s3.kindId = query.value(13).toString();
    row.debitSubconto.s3.entityId = query.value(14).toLongLong();

    row.creditSubconto.s1.kindId = query.value(15).toString();
    row.creditSubconto.s1.entityId = query.value(16).toLongLong();
    row.creditSubconto.s2.kindId = query.value(17).toString();
    row.creditSubconto.s2.entityId = query.value(18).toLongLong();
    row.creditSubconto.s3.kindId = query.value(19).toString();
    row.creditSubconto.s3.entityId = query.value(20).toLongLong();

    static_cast<void>(subcontoRepo.resolveNames(row.debitSubconto));
    static_cast<void>(subcontoRepo.resolveNames(row.creditSubconto));

    const auto corrRes =
        accounts.findById(AccountId{debitId == accountId.value ? creditId : debitId});
    if (corrRes) {
      row.corrAccountCode = corrRes.value().code;
    }

    if (debitId == accountId.value) {
      row.debit = Money::fromCents(amount);
      Status add = addTo(signedBalance, amount, QStringLiteral("картка"));
      if (!add) {
        return Result<AccountCard>::fail(add.error());
      }
      add = addTo(turnDt, amount, QStringLiteral("картка"));
      if (!add) {
        return Result<AccountCard>::fail(add.error());
      }
    } else {
      row.credit = Money::fromCents(amount);
      Status add = addTo(signedBalance, -amount, QStringLiteral("картка"));
      if (!add) {
        return Result<AccountCard>::fail(add.error());
      }
      add = addTo(turnCt, amount, QStringLiteral("картка"));
      if (!add) {
        return Result<AccountCard>::fail(add.error());
      }
    }
    split = splitSigned(signedBalance, row.balanceDebit, row.balanceCredit);
    if (!split) {
      return Result<AccountCard>::fail(split.error());
    }
    card.rows.push_back(row);
  }

  card.turnoverDebit = Money::fromCents(turnDt);
  card.turnoverCredit = Money::fromCents(turnCt);
  split = splitSigned(signedBalance, card.closingDebit, card.closingCredit);
  if (!split) {
    return Result<AccountCard>::fail(split.error());
  }
  return Result<AccountCard>::ok(std::move(card));
}

CounterpartyTurnoverReport::CounterpartyTurnoverReport(QSqlDatabase db) : db_(std::move(db)) {}

Result<CounterpartyTurnover> CounterpartyTurnoverReport::build(AccountId accountId, QDate from,
                                                               QDate to) const
{
  if (!from.isValid() || !to.isValid() || from > to) {
    return Result<CounterpartyTurnover>::fail(
        Error::invalidArgument(QStringLiteral("некоректний період")));
  }
  AccountRepository accounts(db_);
  Result<Account> account = accounts.findById(accountId);
  if (!account) {
    return Result<CounterpartyTurnover>::fail(account.error());
  }
  if (account.value().isFolder) {
    return Result<CounterpartyTurnover>::fail(
        Error::validation(QStringLiteral("звіт доступний лише для субрахунків")));
  }

  sql::Query query(db_);
  Status status = query.prepare(
      QStringLiteral("SELECT COALESCE(e.counterparty_id, 0), "
                     "COALESCE(c.name, ''), e.date, e.amount_cents, e.debit_account_id, "
                     "e.credit_account_id "
                     "FROM ledger_entries e "
                     "LEFT JOIN counterparties c ON c.id = e.counterparty_id "
                     "WHERE e.debit_account_id = :id OR e.credit_account_id = :id2"));
  if (!status) {
    return Result<CounterpartyTurnover>::fail(status.error());
  }
  const qlonglong id = static_cast<qlonglong>(accountId.value);
  query.bind(QStringLiteral(":id"), id);
  query.bind(QStringLiteral(":id2"), id);
  status = query.exec();
  if (!status) {
    return Result<CounterpartyTurnover>::fail(status.error());
  }

  const QString fromIso = from.toString(Qt::ISODate);
  const QString toIso = to.toString(Qt::ISODate);
  struct Agg
  {
    QString name;
    std::int64_t openDt = 0;
    std::int64_t openCt = 0;
    std::int64_t turnDt = 0;
    std::int64_t turnCt = 0;
  };
  QHash<std::int64_t, Agg> byCp;
  while (query.next()) {
    const std::int64_t cp = query.value(0).toLongLong();
    Agg & agg = byCp[cp];
    if (agg.name.isEmpty()) {
      agg.name = query.value(1).toString();
    }
    const QString date = query.value(2).toString();
    const std::int64_t amount = query.value(3).toLongLong();
    const bool isDebit = query.value(4).toLongLong() == accountId.value;
    if (date < fromIso) {
      if (isDebit) {
        agg.openDt += amount;
      } else {
        agg.openCt += amount;
      }
    } else if (date <= toIso) {
      if (isDebit) {
        agg.turnDt += amount;
      } else {
        agg.turnCt += amount;
      }
    }
  }

  CounterpartyTurnover report;
  report.accountId = accountId;
  report.accountCode = account.value().code;
  report.accountName = account.value().name;
  report.from = from;
  report.to = to;

  QList<std::int64_t> keys = byCp.keys();
  std::sort(keys.begin(), keys.end(), [&](std::int64_t a, std::int64_t b) {
    return byCp.value(a).name.localeAwareCompare(byCp.value(b).name) < 0;
  });

  std::int64_t totOpenDt = 0;
  std::int64_t totOpenCt = 0;
  std::int64_t totTurnDt = 0;
  std::int64_t totTurnCt = 0;
  std::int64_t totCloseDt = 0;
  std::int64_t totCloseCt = 0;
  for (std::int64_t key : keys) {
    const Agg agg = byCp.value(key);
    if (agg.openDt == 0 && agg.openCt == 0 && agg.turnDt == 0 && agg.turnCt == 0) {
      continue;
    }
    CounterpartyTurnoverRow row;
    if (key > 0) {
      row.counterpartyId = CounterpartyId{key};
    }
    row.name = agg.name.isEmpty() ? QStringLiteral("—") : agg.name;
    Status split = splitSigned(agg.openDt - agg.openCt, row.openingDebit, row.openingCredit);
    if (!split) {
      return Result<CounterpartyTurnover>::fail(split.error());
    }
    row.turnoverDebit = Money::fromCents(agg.turnDt);
    row.turnoverCredit = Money::fromCents(agg.turnCt);
    split = splitSigned((agg.openDt - agg.openCt) + agg.turnDt - agg.turnCt, row.closingDebit,
                        row.closingCredit);
    if (!split) {
      return Result<CounterpartyTurnover>::fail(split.error());
    }
    report.rows.push_back(row);
    totOpenDt += row.openingDebit.cents();
    totOpenCt += row.openingCredit.cents();
    totTurnDt += row.turnoverDebit.cents();
    totTurnCt += row.turnoverCredit.cents();
    totCloseDt += row.closingDebit.cents();
    totCloseCt += row.closingCredit.cents();
  }
  report.totals.name = QStringLiteral("Разом");
  report.totals.openingDebit = Money::fromCents(totOpenDt);
  report.totals.openingCredit = Money::fromCents(totOpenCt);
  report.totals.turnoverDebit = Money::fromCents(totTurnDt);
  report.totals.turnoverCredit = Money::fromCents(totTurnCt);
  report.totals.closingDebit = Money::fromCents(totCloseDt);
  report.totals.closingCredit = Money::fromCents(totCloseCt);
  return Result<CounterpartyTurnover>::ok(std::move(report));
}

StockReport::StockReport(QSqlDatabase db) : db_(std::move(db)) {}

Result<StockReportData> StockReport::build(QDate from, QDate to,
                                           std::optional<WarehouseId> warehouseId) const
{
  if (!from.isValid() || !to.isValid() || from > to) {
    return Result<StockReportData>::fail(
        Error::invalidArgument(QStringLiteral("некоректний період")));
  }

  StockReportData report;
  report.from = from;
  report.to = to;
  if (warehouseId.has_value() && warehouseId->value > 0) {
    report.warehouseId = *warehouseId;
    sql::Query whQuery(db_);
    if (whQuery.prepare(QStringLiteral("SELECT name FROM warehouses WHERE id = :id"))) {
      whQuery.bind(QStringLiteral(":id"), static_cast<qlonglong>(warehouseId->value));
      if (whQuery.exec() && whQuery.next()) {
        report.warehouseName = whQuery.value(0).toString();
      }
    }
  }

  sql::Query query(db_);
  QString sql = QStringLiteral("SELECT m.nomenclature_id, n.name, COALESCE(n.sku, ''), n.unit, "
                               "m.date, m.quantity_milli, m.amount_cents "
                               "FROM inventory_movements m "
                               "JOIN nomenclature n ON n.id = m.nomenclature_id ");
  if (warehouseId.has_value() && warehouseId->value > 0) {
    sql += QStringLiteral("WHERE m.warehouse_id = :wh ");
  }
  Status status = query.prepare(sql);
  if (!status) {
    return Result<StockReportData>::fail(status.error());
  }
  if (warehouseId.has_value() && warehouseId->value > 0) {
    query.bind(QStringLiteral(":wh"), static_cast<qlonglong>(warehouseId->value));
  }
  status = query.exec();
  if (!status) {
    return Result<StockReportData>::fail(status.error());
  }

  const QString fromIso = from.toString(Qt::ISODate);
  const QString toIso = to.toString(Qt::ISODate);
  struct Agg
  {
    QString name;
    QString sku;
    QString unit;
    std::int64_t openQty = 0;
    std::int64_t openAmt = 0;
    std::int64_t inQty = 0;
    std::int64_t inAmt = 0;
    std::int64_t outQty = 0;
    std::int64_t outAmt = 0;
    std::int64_t reservedQty = 0;
    std::int64_t orderedQty = 0;
  };
  QHash<std::int64_t, Agg> byItem;
  while (query.next()) {
    const std::int64_t id = query.value(0).toLongLong();
    Agg & agg = byItem[id];
    agg.name = query.value(1).toString();
    agg.sku = query.value(2).toString();
    agg.unit = query.value(3).toString();
    const QString date = query.value(4).toString();
    const std::int64_t qty = query.value(5).toLongLong();
    const std::int64_t amount = query.value(6).toLongLong();
    if (date < fromIso) {
      agg.openQty += qty;
      agg.openAmt += amount;
    } else if (date <= toIso) {
      if (qty >= 0) {
        agg.inQty += qty;
        agg.inAmt += amount;
      } else {
        agg.outQty += -qty;
        agg.outAmt += -amount;
      }
    }
  }

  // Облік резервів за замовленнями покупців та очікуваних надходжень за замовленнями постачальникам
  sql::Query orderQuery(db_);
  QString orderSql = QStringLiteral(
      "SELECT d.type, l.nomenclature_id, COALESCE(SUM(l.quantity_milli), 0), "
      "n.name, COALESCE(n.sku, ''), n.unit "
      "FROM documents d "
      "JOIN document_lines l ON l.document_id = d.id "
      "JOIN nomenclature n ON n.id = l.nomenclature_id "
      "WHERE d.type IN ('customer_order', 'supplier_order') "
      "AND (d.order_status IS NULL OR d.order_status NOT IN ('closed', 'cancelled')) ");
  if (warehouseId.has_value() && warehouseId->value > 0) {
    orderSql += QStringLiteral("AND (d.warehouse_id = :wh OR l.warehouse_id = :wh) ");
  }
  orderSql += QStringLiteral("GROUP BY d.type, l.nomenclature_id");
  if (orderQuery.prepare(orderSql)) {
    if (warehouseId.has_value() && warehouseId->value > 0) {
      orderQuery.bind(QStringLiteral(":wh"), static_cast<qlonglong>(warehouseId->value));
    }
    if (orderQuery.exec()) {
      while (orderQuery.next()) {
        const QString typeStr = orderQuery.value(0).toString();
        const std::int64_t nomId = orderQuery.value(1).toLongLong();
        const std::int64_t qty = orderQuery.value(2).toLongLong();
        Agg & agg = byItem[nomId];
        if (agg.name.isEmpty()) {
          agg.name = orderQuery.value(3).toString();
          agg.sku = orderQuery.value(4).toString();
          agg.unit = orderQuery.value(5).toString();
        }
        if (typeStr == QLatin1String("customer_order")) {
          agg.reservedQty += qty;
        } else if (typeStr == QLatin1String("supplier_order")) {
          agg.orderedQty += qty;
        }
      }
    }
  }

  QList<std::int64_t> keys = byItem.keys();
  std::sort(keys.begin(), keys.end(), [&](std::int64_t a, std::int64_t b) {
    return byItem.value(a).name.localeAwareCompare(byItem.value(b).name) < 0;
  });

  std::int64_t totOpenQty = 0;
  std::int64_t totOpenAmt = 0;
  std::int64_t totInQty = 0;
  std::int64_t totInAmt = 0;
  std::int64_t totOutQty = 0;
  std::int64_t totOutAmt = 0;
  std::int64_t totCloseQty = 0;
  std::int64_t totCloseAmt = 0;
  std::int64_t totResQty = 0;
  std::int64_t totOrdQty = 0;
  for (std::int64_t key : keys) {
    const Agg agg = byItem.value(key);
    StockRow row;
    row.nomenclatureId = NomenclatureId{key};
    row.name = agg.name;
    row.sku = agg.sku;
    row.unit = agg.unit;
    row.openingQty = Quantity::fromMilli(agg.openQty);
    row.openingAmount = Money::fromCents(agg.openAmt);
    row.inQty = Quantity::fromMilli(agg.inQty);
    row.inAmount = Money::fromCents(agg.inAmt);
    row.outQty = Quantity::fromMilli(agg.outQty);
    row.outAmount = Money::fromCents(agg.outAmt);
    row.closingQty = Quantity::fromMilli(agg.openQty + agg.inQty - agg.outQty);
    row.closingAmount = Money::fromCents(agg.openAmt + agg.inAmt - agg.outAmt);
    row.reservedQty = Quantity::fromMilli(agg.reservedQty);
    row.orderedQty = Quantity::fromMilli(agg.orderedQty);
    report.rows.push_back(row);
    totOpenQty += agg.openQty;
    totOpenAmt += agg.openAmt;
    totInQty += agg.inQty;
    totInAmt += agg.inAmt;
    totOutQty += agg.outQty;
    totOutAmt += agg.outAmt;
    totCloseQty += agg.openQty + agg.inQty - agg.outQty;
    totCloseAmt += agg.openAmt + agg.inAmt - agg.outAmt;
    totResQty += agg.reservedQty;
    totOrdQty += agg.orderedQty;
  }
  report.totals.name = QStringLiteral("Разом");
  report.totals.openingQty = Quantity::fromMilli(totOpenQty);
  report.totals.openingAmount = Money::fromCents(totOpenAmt);
  report.totals.inQty = Quantity::fromMilli(totInQty);
  report.totals.inAmount = Money::fromCents(totInAmt);
  report.totals.outQty = Quantity::fromMilli(totOutQty);
  report.totals.outAmount = Money::fromCents(totOutAmt);
  report.totals.closingQty = Quantity::fromMilli(totCloseQty);
  report.totals.closingAmount = Money::fromCents(totCloseAmt);
  report.totals.reservedQty = Quantity::fromMilli(totResQty);
  report.totals.orderedQty = Quantity::fromMilli(totOrdQty);
  return Result<StockReportData>::ok(std::move(report));
}

CashBookReport::CashBookReport(QSqlDatabase db) : db_(std::move(db)) {}

Result<CashBook> CashBookReport::build(QDate from, QDate to) const
{
  return buildFor(chart::kCash, from, to);
}

Result<CashBook> CashBookReport::buildFor(const char * accountCode, QDate from, QDate to) const
{
  if (!from.isValid() || !to.isValid() || from > to) {
    return Result<CashBook>::fail(Error::invalidArgument(QStringLiteral("некоректний період")));
  }
  if (accountCode == nullptr || QLatin1String(accountCode).isEmpty()) {
    return Result<CashBook>::fail(
        Error::invalidArgument(QStringLiteral("не вказано рахунок виписки")));
  }
  const Result<Account> cash = AccountRepository(db_).findByCode(QString::fromLatin1(accountCode));
  if (!cash) {
    return Result<CashBook>::fail(cash.error());
  }
  const qlonglong cashId = static_cast<qlonglong>(cash.value().id.value);
  const QString fromIso = from.toString(Qt::ISODate);
  const QString toIso = to.toString(Qt::ISODate);

  sql::Query open(db_);
  Status status = open.prepare(QStringLiteral(
      "SELECT "
      "COALESCE(SUM(CASE WHEN debit_account_id = :c1 THEN amount_cents ELSE 0 END), 0) - "
      "COALESCE(SUM(CASE WHEN credit_account_id = :c2 THEN amount_cents ELSE 0 END), 0) "
      "FROM ledger_entries WHERE date < :from AND "
      "(debit_account_id = :c3 OR credit_account_id = :c4)"));
  if (!status) {
    return Result<CashBook>::fail(status.error());
  }
  open.bind(QStringLiteral(":c1"), cashId);
  open.bind(QStringLiteral(":c2"), cashId);
  open.bind(QStringLiteral(":c3"), cashId);
  open.bind(QStringLiteral(":c4"), cashId);
  open.bind(QStringLiteral(":from"), fromIso);
  status = open.exec();
  if (!status) {
    return Result<CashBook>::fail(status.error());
  }
  std::int64_t balance = 0;
  if (open.next()) {
    balance = open.value(0).toLongLong();
  }

  sql::Query query(db_);
  status = query.prepare(
      QStringLiteral("SELECT e.date, e.document_id, d.number, d.type, "
                     "COALESCE(e.description, ''), e.amount_cents, e.debit_account_id, "
                     "e.credit_account_id "
                     "FROM ledger_entries e "
                     "JOIN documents d ON d.id = e.document_id "
                     "WHERE e.date >= :from AND e.date <= :to "
                     "AND (e.debit_account_id = :c1 OR e.credit_account_id = :c2) "
                     "ORDER BY e.date, d.number, e.id"));
  if (!status) {
    return Result<CashBook>::fail(status.error());
  }
  query.bind(QStringLiteral(":from"), fromIso);
  query.bind(QStringLiteral(":to"), toIso);
  query.bind(QStringLiteral(":c1"), cashId);
  query.bind(QStringLiteral(":c2"), cashId);
  status = query.exec();
  if (!status) {
    return Result<CashBook>::fail(status.error());
  }

  CashBook book;
  book.from = from;
  book.to = to;
  book.opening = Money::fromCents(balance);
  std::int64_t incoming = 0;
  std::int64_t outgoing = 0;
  const bool withDayTotals = QLatin1String(accountCode) == QLatin1String(chart::kCash);
  QDate day;
  std::int64_t dayIn = 0;
  std::int64_t dayOut = 0;
  auto flushDay = [&](std::int64_t running) {
    if (!withDayTotals || !day.isValid()) {
      return Status::ok();
    }
    CashBookRow total;
    total.date = day;
    total.typeLabel = QStringLiteral("Разом за день");
    total.incoming = Money::fromCents(dayIn);
    total.outgoing = Money::fromCents(dayOut);
    total.balance = Money::fromCents(running);
    total.dayTotal = true;
    book.rows.push_back(total);
    dayIn = 0;
    dayOut = 0;
    return Status::ok();
  };
  while (query.next()) {
    CashBookRow row;
    row.date = QDate::fromString(query.value(0).toString(), Qt::ISODate);
    row.documentId = DocumentId{query.value(1).toLongLong()};
    row.number = query.value(2).toString();
    const auto type = documentTypeFromString(query.value(3).toString());
    row.typeLabel = type ? documentTypeLabelUk(*type) : query.value(3).toString();
    row.description = query.value(4).toString();
    const std::int64_t amount = query.value(5).toLongLong();
    const qlonglong debit = query.value(6).toLongLong();
    if (withDayTotals && day.isValid() && row.date != day) {
      const Status flushed = flushDay(balance);
      if (!flushed) {
        return Result<CashBook>::fail(flushed.error());
      }
    }
    day = row.date;
    if (debit == cashId) {
      row.incoming = Money::fromCents(amount);
      Status add = addTo(incoming, amount, QStringLiteral("каса"));
      if (!add) {
        return Result<CashBook>::fail(add.error());
      }
      add = addTo(balance, amount, QStringLiteral("каса"));
      if (!add) {
        return Result<CashBook>::fail(add.error());
      }
      add = addTo(dayIn, amount, QStringLiteral("каса"));
      if (!add) {
        return Result<CashBook>::fail(add.error());
      }
    } else {
      row.outgoing = Money::fromCents(amount);
      Status add = addTo(outgoing, amount, QStringLiteral("каса"));
      if (!add) {
        return Result<CashBook>::fail(add.error());
      }
      if (addOverflows(balance, -amount)) {
        return Result<CashBook>::fail(Error::overflow(QStringLiteral("переповнення каси")));
      }
      balance -= amount;
      add = addTo(dayOut, amount, QStringLiteral("каса"));
      if (!add) {
        return Result<CashBook>::fail(add.error());
      }
    }
    row.balance = Money::fromCents(balance);
    book.rows.push_back(row);
  }
  {
    const Status flushed = flushDay(balance);
    if (!flushed) {
      return Result<CashBook>::fail(flushed.error());
    }
  }
  book.incoming = Money::fromCents(incoming);
  book.outgoing = Money::fromCents(outgoing);
  book.closing = Money::fromCents(balance);
  return Result<CashBook>::ok(std::move(book));
}

CashJournalReport::CashJournalReport(QSqlDatabase db) : db_(std::move(db)) {}

Result<CashJournal> CashJournalReport::build(QDate from, QDate to) const
{
  if (!from.isValid() || !to.isValid() || from > to) {
    return Result<CashJournal>::fail(Error::invalidArgument(QStringLiteral("некоректний період")));
  }
  sql::Query query(db_);
  Status status = query.prepare(QStringLiteral(
      "SELECT d.id, d.date, d.number, d.type, COALESCE(c.name, ''), "
      "COALESCE(d.comment, ''), "
      "COALESCE(SUM(CASE WHEN d.type = 'cash_in' THEN l.amount_cents ELSE 0 END), 0), "
      "COALESCE(SUM(CASE WHEN d.type = 'cash_out' THEN l.amount_cents ELSE 0 END), 0) "
      "FROM documents d "
      "LEFT JOIN counterparties c ON c.id = d.counterparty_id "
      "LEFT JOIN document_lines l ON l.document_id = d.id "
      "WHERE d.date >= :from AND d.date <= :to AND d.posted = 1 "
      "AND d.type IN ('cash_in', 'cash_out') "
      "GROUP BY d.id ORDER BY d.date, d.number, d.id"));
  if (!status) {
    return Result<CashJournal>::fail(status.error());
  }
  query.bind(QStringLiteral(":from"), from.toString(Qt::ISODate));
  query.bind(QStringLiteral(":to"), to.toString(Qt::ISODate));
  status = query.exec();
  if (!status) {
    return Result<CashJournal>::fail(status.error());
  }
  CashJournal journal;
  journal.from = from;
  journal.to = to;
  std::int64_t incoming = 0;
  std::int64_t outgoing = 0;
  while (query.next()) {
    CashJournalRow row;
    row.documentId = DocumentId{query.value(0).toLongLong()};
    row.date = QDate::fromString(query.value(1).toString(), Qt::ISODate);
    row.number = query.value(2).toString();
    const auto type = documentTypeFromString(query.value(3).toString());
    row.typeLabel = type ? documentTypeLabelUk(*type) : query.value(3).toString();
    row.party = query.value(4).toString();
    row.purpose = query.value(5).toString();
    row.incoming = Money::fromCents(query.value(6).toLongLong());
    row.outgoing = Money::fromCents(query.value(7).toLongLong());
    Status add = addTo(incoming, row.incoming.cents(), QStringLiteral("каса"));
    if (!add) {
      return Result<CashJournal>::fail(add.error());
    }
    add = addTo(outgoing, row.outgoing.cents(), QStringLiteral("каса"));
    if (!add) {
      return Result<CashJournal>::fail(add.error());
    }
    journal.rows.push_back(row);
  }
  journal.incoming = Money::fromCents(incoming);
  journal.outgoing = Money::fromCents(outgoing);
  return Result<CashJournal>::ok(std::move(journal));
}

FinancialStatementReport::FinancialStatementReport(QSqlDatabase db) : db_(std::move(db)) {}

Result<IncomeStatement> FinancialStatementReport::income(QDate from, QDate to) const
{
  if (!from.isValid() || !to.isValid() || from > to) {
    return Result<IncomeStatement>::fail(
        Error::invalidArgument(QStringLiteral("некоректний період")));
  }
  sql::Query query(db_);
  Status status = query.prepare(QStringLiteral(
      "SELECT a.code, "
      "COALESCE(SUM(CASE WHEN l.credit_account_id = a.id THEN l.amount_cents ELSE 0 END), 0), "
      "COALESCE(SUM(CASE WHEN l.debit_account_id = a.id THEN l.amount_cents ELSE 0 END), 0) "
      "FROM ledger_entries l "
      "JOIN documents d ON d.id = l.document_id "
      "JOIN accounts a ON a.id IN (l.debit_account_id, l.credit_account_id) "
      "WHERE l.date BETWEEN :from AND :to AND d.type <> 'period_result' "
      "AND a.code IN ('701','703','71','714','902','92','944') "
      "GROUP BY a.code"));
  if (!status) {
    return Result<IncomeStatement>::fail(status.error());
  }
  query.bind(QStringLiteral(":from"), from.toString(Qt::ISODate));
  query.bind(QStringLiteral(":to"), to.toString(Qt::ISODate));
  status = query.exec();
  if (!status) {
    return Result<IncomeStatement>::fail(status.error());
  }
  IncomeStatement report;
  report.from = from;
  report.to = to;
  std::int64_t goods = 0;
  std::int64_t services = 0;
  std::int64_t other = 0;
  std::int64_t cogs = 0;
  std::int64_t admin = 0;
  while (query.next()) {
    const QString code = query.value(0).toString();
    const std::int64_t credit = query.value(1).toLongLong();
    const std::int64_t debit = query.value(2).toLongLong();
    // JOIN can double-count if both sides match the same account (never for one line).
    if (code == QLatin1String(chart::kRevenue)) {
      goods = credit - debit;
    } else if (code == QLatin1String(chart::kServiceRevenue)) {
      services = credit - debit;
    } else if (code == QLatin1String(chart::kOtherIncome) ||
               code == QLatin1String(chart::kExchangeGain)) {
      Status st = addTo(other, credit, QStringLiteral("інший дохід"));
      if (!st) {
        return Result<IncomeStatement>::fail(st.error());
      }
      st = addTo(other, -debit, QStringLiteral("інший дохід"));
      if (!st) {
        return Result<IncomeStatement>::fail(st.error());
      }
    } else if (code == QLatin1String(chart::kCogs)) {
      cogs = debit - credit;
    } else if (code == QLatin1String(chart::kAdminExpense) ||
               code == QLatin1String(chart::kExchangeLoss)) {
      Status st = addTo(admin, debit, QStringLiteral("адмінвитрати"));
      if (!st) {
        return Result<IncomeStatement>::fail(st.error());
      }
      st = addTo(admin, -credit, QStringLiteral("адмінвитрати"));
      if (!st) {
        return Result<IncomeStatement>::fail(st.error());
      }
    }
  }
  report.goodsRevenue = Money::fromCents(goods);
  report.serviceRevenue = Money::fromCents(services);
  report.otherIncome = Money::fromCents(other);
  report.cogs = Money::fromCents(cogs);
  report.admin = Money::fromCents(admin);
  std::int64_t profit = 0;
  Status add = addTo(profit, goods, QStringLiteral("фінрезультат"));
  if (!add) {
    return Result<IncomeStatement>::fail(add.error());
  }
  add = addTo(profit, services, QStringLiteral("фінрезультат"));
  if (!add) {
    return Result<IncomeStatement>::fail(add.error());
  }
  add = addTo(profit, other, QStringLiteral("фінрезультат"));
  if (!add) {
    return Result<IncomeStatement>::fail(add.error());
  }
  add = addTo(profit, -cogs, QStringLiteral("фінрезультат"));
  if (!add) {
    return Result<IncomeStatement>::fail(add.error());
  }
  add = addTo(profit, -admin, QStringLiteral("фінрезультат"));
  if (!add) {
    return Result<IncomeStatement>::fail(add.error());
  }
  report.profit = Money::fromCents(profit);
  return Result<IncomeStatement>::ok(report);
}

Result<BalanceSheet> FinancialStatementReport::balance(QDate asOf) const
{
  if (!asOf.isValid()) {
    return Result<BalanceSheet>::fail(
        Error::invalidArgument(QStringLiteral("некоректна дата балансу")));
  }
  const Result<TrialBalance> tb = TrialBalanceReport(db_).build(asOf, asOf);
  if (!tb) {
    return Result<BalanceSheet>::fail(tb.error());
  }
  BalanceSheet sheet;
  sheet.asOf = asOf;
  std::int64_t assets = 0;
  std::int64_t equity = 0;
  std::int64_t liabilities = 0;
  std::int64_t openResult = 0;
  for (const TrialBalanceRow & row : tb.value().rows) {
    if (row.code.compare(QLatin1String(chart::kAuxiliary), Qt::CaseInsensitive) == 0) {
      continue;
    }
    const bool incomeOrExpense =
        row.code.compare(QLatin1String(chart::kRevenue), Qt::CaseInsensitive) == 0 ||
        row.code.compare(QLatin1String(chart::kServiceRevenue), Qt::CaseInsensitive) == 0 ||
        row.code.compare(QLatin1String(chart::kOtherIncome), Qt::CaseInsensitive) == 0 ||
        row.code.compare(QLatin1String(chart::kExchangeGain), Qt::CaseInsensitive) == 0 ||
        row.code.compare(QLatin1String(chart::kCogs), Qt::CaseInsensitive) == 0 ||
        row.code.compare(QLatin1String(chart::kAdminExpense), Qt::CaseInsensitive) == 0 ||
        row.code.compare(QLatin1String(chart::kExchangeLoss), Qt::CaseInsensitive) == 0 ||
        row.code.compare(QLatin1String(chart::kFinancialResult), Qt::CaseInsensitive) == 0;
    if (incomeOrExpense) {
      const std::int64_t net = row.closingCredit.cents() - row.closingDebit.cents();
      Status st = addTo(openResult, net, QStringLiteral("баланс"));
      if (!st) {
        return Result<BalanceSheet>::fail(st.error());
      }
      continue;
    }
    if (isFaDepreciationAccountCode(row.code) || isIaAmortizationAccountCode(row.code)) {
      if (!row.closingCredit.isZero()) {
        const Result<Money> neg = row.closingCredit.negated();
        if (!neg) {
          return Result<BalanceSheet>::fail(neg.error());
        }
        sheet.assets.push_back({row.code, row.name, neg.value()});
        Status st = addTo(assets, neg.value().cents(), QStringLiteral("актив"));
        if (!st) {
          return Result<BalanceSheet>::fail(st.error());
        }
      }
      continue;
    }
    if (!row.closingDebit.isZero()) {
      sheet.assets.push_back({row.code, row.name, row.closingDebit});
      Status st = addTo(assets, row.closingDebit.cents(), QStringLiteral("актив"));
      if (!st) {
        return Result<BalanceSheet>::fail(st.error());
      }
    }
    if (!row.closingCredit.isZero()) {
      const bool isEquity =
          row.code.compare(QLatin1String(chart::kCapital), Qt::CaseInsensitive) == 0 ||
          row.code.compare(QLatin1String(chart::kRetainedEarnings), Qt::CaseInsensitive) == 0;
      if (isEquity) {
        sheet.equity.push_back({row.code, row.name, row.closingCredit});
        Status st = addTo(equity, row.closingCredit.cents(), QStringLiteral("капітал"));
        if (!st) {
          return Result<BalanceSheet>::fail(st.error());
        }
      } else {
        sheet.liabilities.push_back({row.code, row.name, row.closingCredit});
        Status st = addTo(liabilities, row.closingCredit.cents(), QStringLiteral("зобов'язання"));
        if (!st) {
          return Result<BalanceSheet>::fail(st.error());
        }
      }
    }
  }
  if (openResult != 0) {
    const bool profit = openResult > 0;
    StatementLine line;
    line.code = QString::fromLatin1(chart::kFinancialResult);
    line.name = QStringLiteral("Незакритий фінрезультат");
    line.amount = Money::fromCents(profit ? openResult : -openResult);
    if (profit) {
      sheet.equity.push_back(line);
      Status st = addTo(equity, openResult, QStringLiteral("капітал"));
      if (!st) {
        return Result<BalanceSheet>::fail(st.error());
      }
    } else {
      sheet.assets.push_back(line);
      Status st = addTo(assets, -openResult, QStringLiteral("актив"));
      if (!st) {
        return Result<BalanceSheet>::fail(st.error());
      }
    }
  }
  sheet.assetsTotal = Money::fromCents(assets);
  sheet.equityTotal = Money::fromCents(equity);
  sheet.liabilitiesTotal = Money::fromCents(liabilities);
  return Result<BalanceSheet>::ok(std::move(sheet));
}

IncomeBookReport::IncomeBookReport(QSqlDatabase db) : db_(std::move(db)) {}

Result<IncomeBook> IncomeBookReport::build(QDate from, QDate to) const
{
  if (!from.isValid() || !to.isValid() || from > to) {
    return Result<IncomeBook>::fail(Error::invalidArgument(QStringLiteral("некоректний період")));
  }
  const Result<CompanyProfile> company = CompanyRepository(db_).load();
  if (!company) {
    return Result<IncomeBook>::fail(company.error());
  }
  sql::Query query(db_);
  Status status = query.prepare(QStringLiteral(
      "SELECT l.date, d.number, d.type, COALESCE(c.name, ''), a.code, "
      "CASE WHEN l.credit_account_id = a.id THEN l.amount_cents ELSE -l.amount_cents END "
      "FROM ledger_entries l "
      "JOIN documents d ON d.id = l.document_id "
      "JOIN accounts a ON a.id IN (l.credit_account_id, l.debit_account_id) "
      "LEFT JOIN counterparties c ON c.id = l.counterparty_id "
      "WHERE l.date BETWEEN :from AND :to AND d.type <> 'period_result' "
      "AND a.code IN ('701','703') "
      "ORDER BY l.date, d.number, l.id"));
  if (!status) {
    return Result<IncomeBook>::fail(status.error());
  }
  query.bind(QStringLiteral(":from"), from.toString(Qt::ISODate));
  query.bind(QStringLiteral(":to"), to.toString(Qt::ISODate));
  status = query.exec();
  if (!status) {
    return Result<IncomeBook>::fail(status.error());
  }
  IncomeBook book;
  book.from = from;
  book.to = to;
  book.taxSystem = company.value().taxSystem;
  book.rateBp = company.value().singleTaxRateBp;
  std::int64_t income = 0;
  while (query.next()) {
    IncomeBookRow row;
    row.date = QDate::fromString(query.value(0).toString(), Qt::ISODate);
    row.number = query.value(1).toString();
    const auto type = documentTypeFromString(query.value(2).toString());
    row.typeLabel = type ? documentTypeLabelUk(*type) : query.value(2).toString();
    row.party = query.value(3).toString();
    row.account = query.value(4).toString();
    row.amount = Money::fromCents(query.value(5).toLongLong());
    Status add = addTo(income, row.amount.cents(), QStringLiteral("книга доходів"));
    if (!add) {
      return Result<IncomeBook>::fail(add.error());
    }
    book.rows.push_back(row);
  }
  book.income = Money::fromCents(income);
  if (book.taxSystem == TaxSystem::Simplified && income > 0) {
    const Result<std::int64_t> tax = mulDivRound(income, book.rateBp, 10000);
    if (!tax) {
      return Result<IncomeBook>::fail(tax.error());
    }
    book.tax = Money::fromCents(tax.value());
  }
  return Result<IncomeBook>::ok(std::move(book));
}

ReconciliationReport::ReconciliationReport(QSqlDatabase db) : db_(std::move(db)) {}

Result<ReconciliationAct> ReconciliationReport::build(AccountId accountId, CounterpartyId party,
                                                      QDate from, QDate to) const
{
  if (!party) {
    return Result<ReconciliationAct>::fail(
        Error::validation(QStringLiteral("оберіть контрагента для акта звірки")));
  }
  const Result<Counterparty> loaded = CounterpartyRepository(db_).findById(party);
  if (!loaded) {
    return Result<ReconciliationAct>::fail(loaded.error());
  }
  const Result<AccountCard> card = AccountCardReport(db_).build(accountId, from, to, party);
  if (!card) {
    return Result<ReconciliationAct>::fail(card.error());
  }
  ReconciliationAct act;
  act.party = loaded.value();
  act.accountId = card.value().accountId;
  act.accountCode = card.value().code;
  act.accountName = card.value().name;
  act.from = from;
  act.to = to;
  act.openingDebit = card.value().openingDebit;
  act.openingCredit = card.value().openingCredit;
  act.rows = card.value().rows;
  act.turnoverDebit = card.value().turnoverDebit;
  act.turnoverCredit = card.value().turnoverCredit;
  act.closingDebit = card.value().closingDebit;
  act.closingCredit = card.value().closingCredit;
  return Result<ReconciliationAct>::ok(std::move(act));
}

VatStatementReport::VatStatementReport(QSqlDatabase db) : db_(std::move(db)) {}

Result<VatStatement> VatStatementReport::build(QDate from, QDate to) const
{
  const Result<TrialBalance> balance = TrialBalanceReport(db_).build(from, to);
  if (!balance) {
    return Result<VatStatement>::fail(balance.error());
  }
  VatStatement report;
  report.from = from;
  report.to = to;
  auto pick = [&](const char * code, TrialBalanceRow & into) {
    for (const TrialBalanceRow & row : balance.value().rows) {
      if (row.code.compare(QLatin1String(code), Qt::CaseInsensitive) == 0) {
        into = row;
        return;
      }
    }
  };
  pick(chart::kVatLiability, report.liability);
  pick(chart::kVatCredit, report.credit);
  pick(chart::kVatBudget, report.budget);
  if (report.liability.code.isEmpty()) {
    report.liability.code = QString::fromLatin1(chart::kVatLiability);
    report.liability.name = QStringLiteral("Податкові зобов'язання");
  }
  if (report.credit.code.isEmpty()) {
    report.credit.code = QString::fromLatin1(chart::kVatCredit);
    report.credit.name = QStringLiteral("Податковий кредит");
  }
  if (report.budget.code.isEmpty()) {
    report.budget.code = QString::fromLatin1(chart::kVatBudget);
    report.budget.name = QStringLiteral("Розрахунки за податком на додану вартість");
  }
  return Result<VatStatement>::ok(std::move(report));
}

SubcontoTrialBalanceReport::SubcontoTrialBalanceReport(QSqlDatabase db) : db_(std::move(db)) {}

Result<SubcontoTrialBalance> SubcontoTrialBalanceReport::build(AccountId accountId, int lineNo,
                                                               QDate from, QDate to) const
{
  if (!from.isValid() || !to.isValid() || from > to) {
    return Result<SubcontoTrialBalance>::fail(
        Error::invalidArgument(QStringLiteral("некоректний період")));
  }
  if (lineNo < 1 || lineNo > 3) {
    return Result<SubcontoTrialBalance>::fail(
        Error::invalidArgument(QStringLiteral("номер розрізу субконто має бути від 1 до 3")));
  }

  AccountRepository accounts(db_);
  Result<Account> account = accounts.findById(accountId);
  if (!account) {
    return Result<SubcontoTrialBalance>::fail(account.error());
  }

  SubcontoRepository subcontoRepo(db_);
  const auto listRes = subcontoRepo.listForAccount(accountId);
  if (!listRes) {
    return Result<SubcontoTrialBalance>::fail(listRes.error());
  }

  QString kindId;
  for (const auto & s : listRes.value()) {
    if (s.lineNo == lineNo) {
      kindId = s.subcontoKindId;
      break;
    }
  }
  if (kindId.isEmpty()) {
    return Result<SubcontoTrialBalance>::fail(
        Error::notFound(QStringLiteral("для рахунку %1 не налаштовано Субконто %2")
                            .arg(account.value().code, QString::number(lineNo))));
  }

  const auto kindRes = subcontoRepo.findKind(kindId);
  const QString kindName = kindRes ? kindRes.value().nameUa : kindId;

  const QString dtKindCol = QStringLiteral("dt_subconto%1_kind").arg(lineNo);
  const QString dtIdCol = QStringLiteral("dt_subconto%1_id").arg(lineNo);
  const QString ctKindCol = QStringLiteral("ct_subconto%1_kind").arg(lineNo);
  const QString ctIdCol = QStringLiteral("ct_subconto%1_id").arg(lineNo);

  const QString sqlStr =
      QStringLiteral("SELECT debit_account_id, credit_account_id, amount_cents, date, "
                     "%1, %2, %3, %4 "
                     "FROM ledger_entries "
                     "WHERE debit_account_id = :id OR credit_account_id = :id2")
          .arg(dtKindCol, dtIdCol, ctKindCol, ctIdCol);

  sql::Query query(db_);
  Status status = query.prepare(sqlStr);
  if (!status) {
    return Result<SubcontoTrialBalance>::fail(status.error());
  }
  const qlonglong id = static_cast<qlonglong>(accountId.value);
  query.bind(QStringLiteral(":id"), id);
  query.bind(QStringLiteral(":id2"), id);
  status = query.exec();
  if (!status) {
    return Result<SubcontoTrialBalance>::fail(status.error());
  }

  const QString fromIso = from.toString(Qt::ISODate);
  const QString toIso = to.toString(Qt::ISODate);

  QHash<std::int64_t, Agg> byEntity;
  while (query.next()) {
    const std::int64_t debitId = query.value(0).toLongLong();
    const std::int64_t creditId = query.value(1).toLongLong();
    const std::int64_t amount = query.value(2).toLongLong();
    const QString date = query.value(3).toString();
    const std::int64_t dtEntityId = query.value(5).toLongLong();
    const std::int64_t ctEntityId = query.value(7).toLongLong();

    if (debitId == accountId.value && dtEntityId > 0) {
      if (date < fromIso) {
        Status st = addTo(byEntity[dtEntityId].openDt, amount, QStringLiteral("ОСВ субконто"));
        if (!st)
          return Result<SubcontoTrialBalance>::fail(st.error());
      } else if (date <= toIso) {
        Status st = addTo(byEntity[dtEntityId].turnDt, amount, QStringLiteral("ОСВ субконто"));
        if (!st)
          return Result<SubcontoTrialBalance>::fail(st.error());
      }
    }
    if (creditId == accountId.value && ctEntityId > 0) {
      if (date < fromIso) {
        Status st = addTo(byEntity[ctEntityId].openCt, amount, QStringLiteral("ОСВ субконто"));
        if (!st)
          return Result<SubcontoTrialBalance>::fail(st.error());
      } else if (date <= toIso) {
        Status st = addTo(byEntity[ctEntityId].turnCt, amount, QStringLiteral("ОСВ субконто"));
        if (!st)
          return Result<SubcontoTrialBalance>::fail(st.error());
      }
    }
  }

  SubcontoTrialBalance report;
  report.accountId = accountId;
  report.accountCode = account.value().code;
  report.accountName = account.value().name;
  report.subcontoKindId = kindId;
  report.subcontoKindName = kindName;
  report.from = from;
  report.to = to;

  std::int64_t totOpenDt = 0;
  std::int64_t totOpenCt = 0;
  std::int64_t totTurnDt = 0;
  std::int64_t totTurnCt = 0;
  std::int64_t totCloseDt = 0;
  std::int64_t totCloseCt = 0;

  QList<std::int64_t> entityIds = byEntity.keys();
  std::sort(entityIds.begin(), entityIds.end());

  for (std::int64_t eid : entityIds) {
    const Agg & agg = byEntity.value(eid);
    SubcontoTrialBalanceRow row;
    row.subconto.kindId = kindId;
    row.subconto.entityId = eid;
    auto nameRes = subcontoRepo.resolveName(kindId, eid);
    row.subconto.name = nameRes ? nameRes.value() : QString();

    Status split = splitSigned(agg.openDt - agg.openCt, row.openingDebit, row.openingCredit);
    if (!split)
      return Result<SubcontoTrialBalance>::fail(split.error());
    row.turnoverDebit = Money::fromCents(agg.turnDt);
    row.turnoverCredit = Money::fromCents(agg.turnCt);
    const std::int64_t closeNet = (agg.openDt - agg.openCt) + agg.turnDt - agg.turnCt;
    split = splitSigned(closeNet, row.closingDebit, row.closingCredit);
    if (!split)
      return Result<SubcontoTrialBalance>::fail(split.error());

    report.rows.push_back(row);

    Status add = addTo(totOpenDt, row.openingDebit.cents(), QStringLiteral("ОСВ субконто"));
    if (!add)
      return Result<SubcontoTrialBalance>::fail(add.error());
    add = addTo(totOpenCt, row.openingCredit.cents(), QStringLiteral("ОСВ субконто"));
    if (!add)
      return Result<SubcontoTrialBalance>::fail(add.error());
    add = addTo(totTurnDt, row.turnoverDebit.cents(), QStringLiteral("ОСВ субконто"));
    if (!add)
      return Result<SubcontoTrialBalance>::fail(add.error());
    add = addTo(totTurnCt, row.turnoverCredit.cents(), QStringLiteral("ОСВ субконто"));
    if (!add)
      return Result<SubcontoTrialBalance>::fail(add.error());
    add = addTo(totCloseDt, row.closingDebit.cents(), QStringLiteral("ОСВ субконто"));
    if (!add)
      return Result<SubcontoTrialBalance>::fail(add.error());
    add = addTo(totCloseCt, row.closingCredit.cents(), QStringLiteral("ОСВ субконто"));
    if (!add)
      return Result<SubcontoTrialBalance>::fail(add.error());
  }

  report.totals.subconto.name = QStringLiteral("Разом");
  report.totals.openingDebit = Money::fromCents(totOpenDt);
  report.totals.openingCredit = Money::fromCents(totOpenCt);
  report.totals.turnoverDebit = Money::fromCents(totTurnDt);
  report.totals.turnoverCredit = Money::fromCents(totTurnCt);
  report.totals.closingDebit = Money::fromCents(totCloseDt);
  report.totals.closingCredit = Money::fromCents(totCloseCt);

  return Result<SubcontoTrialBalance>::ok(std::move(report));
}

AccountAnalysisReport::AccountAnalysisReport(QSqlDatabase db) : db_(std::move(db)) {}

Result<AccountAnalysis> AccountAnalysisReport::build(AccountId accountId, QDate from,
                                                     QDate to) const
{
  if (!from.isValid() || !to.isValid() || from > to) {
    return Result<AccountAnalysis>::fail(
        Error::invalidArgument(QStringLiteral("некоректний період")));
  }
  AccountRepository accounts(db_);
  Result<Account> account = accounts.findById(accountId);
  if (!account) {
    return Result<AccountAnalysis>::fail(account.error());
  }
  if (account.value().isFolder) {
    return Result<AccountAnalysis>::fail(
        Error::validation(QStringLiteral("аналіз рахунку доступний лише для субрахунків")));
  }

  const qlonglong id = static_cast<qlonglong>(accountId.value);

  // Початкове сальдо
  sql::Query openQuery(db_);
  Status status = openQuery.prepare(QStringLiteral(
      "SELECT "
      "COALESCE(SUM(CASE WHEN debit_account_id = :id THEN amount_cents ELSE 0 END), 0), "
      "COALESCE(SUM(CASE WHEN credit_account_id = :id2 THEN amount_cents ELSE 0 END), 0) "
      "FROM ledger_entries WHERE date < :from AND "
      "(debit_account_id = :id3 OR credit_account_id = :id4)"));
  if (!status) {
    return Result<AccountAnalysis>::fail(status.error());
  }
  openQuery.bind(QStringLiteral(":id"), id);
  openQuery.bind(QStringLiteral(":id2"), id);
  openQuery.bind(QStringLiteral(":id3"), id);
  openQuery.bind(QStringLiteral(":id4"), id);
  openQuery.bind(QStringLiteral(":from"), from.toString(Qt::ISODate));
  status = openQuery.exec();
  if (!status) {
    return Result<AccountAnalysis>::fail(status.error());
  }

  std::int64_t openDt = 0;
  std::int64_t openCt = 0;
  if (openQuery.next()) {
    openDt = openQuery.value(0).toLongLong();
    openCt = openQuery.value(1).toLongLong();
  }

  // Обороти в розрізі кореспондуючих рахунків за період
  sql::Query turnQuery(db_);
  status = turnQuery.prepare(
      QStringLiteral("SELECT debit_account_id, credit_account_id, SUM(amount_cents) "
                     "FROM ledger_entries "
                     "WHERE date >= :from AND date <= :to AND "
                     "(debit_account_id = :id OR credit_account_id = :id2) "
                     "GROUP BY debit_account_id, credit_account_id"));
  if (!status) {
    return Result<AccountAnalysis>::fail(status.error());
  }
  turnQuery.bind(QStringLiteral(":from"), from.toString(Qt::ISODate));
  turnQuery.bind(QStringLiteral(":to"), to.toString(Qt::ISODate));
  turnQuery.bind(QStringLiteral(":id"), id);
  turnQuery.bind(QStringLiteral(":id2"), id);
  status = turnQuery.exec();
  if (!status) {
    return Result<AccountAnalysis>::fail(status.error());
  }

  struct CorrTurnover
  {
    std::int64_t dtTurn = 0; // Дт нашого рахунку / Кт кореспондуючого
    std::int64_t ctTurn = 0; // Кт нашого рахунку / Дт кореспондуючого
  };

  QHash<std::int64_t, CorrTurnover> corrMap;
  std::int64_t totalTurnDt = 0;
  std::int64_t totalTurnCt = 0;

  while (turnQuery.next()) {
    const std::int64_t dtAcc = turnQuery.value(0).toLongLong();
    const std::int64_t ctAcc = turnQuery.value(1).toLongLong();
    const std::int64_t amt = turnQuery.value(2).toLongLong();

    if (dtAcc == accountId.value) {
      corrMap[ctAcc].dtTurn += amt;
      totalTurnDt += amt;
    } else {
      corrMap[dtAcc].ctTurn += amt;
      totalTurnCt += amt;
    }
  }

  AccountAnalysis report;
  report.accountId = accountId;
  report.accountCode = account.value().code;
  report.accountName = account.value().name;
  report.from = from;
  report.to = to;

  Status split = splitSigned(openDt - openCt, report.openingDebit, report.openingCredit);
  if (!split)
    return Result<AccountAnalysis>::fail(split.error());

  QList<std::int64_t> corrIds = corrMap.keys();
  std::sort(corrIds.begin(), corrIds.end());

  for (std::int64_t cid : corrIds) {
    const CorrTurnover & ct = corrMap.value(cid);
    AccountAnalysisCorrespondence corr;
    corr.corrAccountId = AccountId{cid};
    auto cAccRes = accounts.findById(corr.corrAccountId);
    if (cAccRes) {
      corr.corrAccountCode = cAccRes.value().code;
      corr.corrAccountName = cAccRes.value().name;
    }
    corr.debitTurnover = Money::fromCents(ct.dtTurn);
    corr.creditTurnover = Money::fromCents(ct.ctTurn);
    report.correspondences.push_back(corr);
  }

  report.turnoverDebit = Money::fromCents(totalTurnDt);
  report.turnoverCredit = Money::fromCents(totalTurnCt);
  const std::int64_t closeNet = (openDt - openCt) + totalTurnDt - totalTurnCt;
  split = splitSigned(closeNet, report.closingDebit, report.closingCredit);
  if (!split)
    return Result<AccountAnalysis>::fail(split.error());

  return Result<AccountAnalysis>::ok(std::move(report));
}

CashFlowReport::CashFlowReport(QSqlDatabase db) : db_(std::move(db)) {}

Result<CashFlowStatement> CashFlowReport::build(QDate from, QDate to) const
{
  if (!from.isValid() || !to.isValid() || from > to) {
    return Result<CashFlowStatement>::fail(
        Error::invalidArgument(QStringLiteral("Некоректний період")));
  }

  AccountRepository accounts(db_);
  auto loadMoneyId = [&](const char * code) -> Result<qlonglong> {
    const Result<Account> acc = accounts.findByCode(QString::fromLatin1(code));
    if (!acc)
      return Result<qlonglong>::fail(acc.error());
    return Result<qlonglong>::ok(static_cast<qlonglong>(acc.value().id.value));
  };
  const Result<qlonglong> cashIdRes = loadMoneyId(chart::kCash);
  if (!cashIdRes)
    return Result<CashFlowStatement>::fail(cashIdRes.error());
  const Result<qlonglong> bankIdRes = loadMoneyId(chart::kBank);
  if (!bankIdRes)
    return Result<CashFlowStatement>::fail(bankIdRes.error());
  const Result<qlonglong> fxCashIdRes = loadMoneyId(chart::kForeignCash);
  if (!fxCashIdRes)
    return Result<CashFlowStatement>::fail(fxCashIdRes.error());
  const Result<qlonglong> fxBankIdRes = loadMoneyId(chart::kForeignBank);
  if (!fxBankIdRes)
    return Result<CashFlowStatement>::fail(fxBankIdRes.error());

  const qlonglong cashId = cashIdRes.value();
  const qlonglong bankId = bankIdRes.value();
  const qlonglong fxCashId = fxCashIdRes.value();
  const qlonglong fxBankId = fxBankIdRes.value();
  const QString fromIso = from.toString(Qt::ISODate);
  const QString toIso = to.toString(Qt::ISODate);

  auto signedBalance = [&](qlonglong accountId, const QString & beforeIso) -> Result<std::int64_t> {
    sql::Query q(db_);
    Status st = q.prepare(QStringLiteral(
        "SELECT "
        "COALESCE(SUM(CASE WHEN debit_account_id = :a1 THEN amount_cents ELSE 0 END), 0) - "
        "COALESCE(SUM(CASE WHEN credit_account_id = :a2 THEN amount_cents ELSE 0 END), 0) "
        "FROM ledger_entries WHERE date < :before AND "
        "(debit_account_id = :a3 OR credit_account_id = :a4)"));
    if (!st)
      return Result<std::int64_t>::fail(st.error());
    q.bind(QStringLiteral(":a1"), accountId);
    q.bind(QStringLiteral(":a2"), accountId);
    q.bind(QStringLiteral(":a3"), accountId);
    q.bind(QStringLiteral(":a4"), accountId);
    q.bind(QStringLiteral(":before"), beforeIso);
    st = q.exec();
    if (!st)
      return Result<std::int64_t>::fail(st.error());
    std::int64_t cents = 0;
    if (q.next())
      cents = q.value(0).toLongLong();
    return Result<std::int64_t>::ok(cents);
  };

  const Result<std::int64_t> openCashUah = signedBalance(cashId, fromIso);
  if (!openCashUah)
    return Result<CashFlowStatement>::fail(openCashUah.error());
  const Result<std::int64_t> openCashFx = signedBalance(fxCashId, fromIso);
  if (!openCashFx)
    return Result<CashFlowStatement>::fail(openCashFx.error());
  const Result<std::int64_t> openBankUah = signedBalance(bankId, fromIso);
  if (!openBankUah)
    return Result<CashFlowStatement>::fail(openBankUah.error());
  const Result<std::int64_t> openBankFx = signedBalance(fxBankId, fromIso);
  if (!openBankFx)
    return Result<CashFlowStatement>::fail(openBankFx.error());
  const QString afterTo = to.addDays(1).toString(Qt::ISODate);
  const Result<std::int64_t> closeCashUah = signedBalance(cashId, afterTo);
  if (!closeCashUah)
    return Result<CashFlowStatement>::fail(closeCashUah.error());
  const Result<std::int64_t> closeCashFx = signedBalance(fxCashId, afterTo);
  if (!closeCashFx)
    return Result<CashFlowStatement>::fail(closeCashFx.error());
  const Result<std::int64_t> closeBankUah = signedBalance(bankId, afterTo);
  if (!closeBankUah)
    return Result<CashFlowStatement>::fail(closeBankUah.error());
  const Result<std::int64_t> closeBankFx = signedBalance(fxBankId, afterTo);
  if (!closeBankFx)
    return Result<CashFlowStatement>::fail(closeBankFx.error());

  std::int64_t openCashSum = openCashUah.value();
  Status addOpen = addTo(openCashSum, openCashFx.value(), QStringLiteral("рух коштів"));
  if (!addOpen)
    return Result<CashFlowStatement>::fail(addOpen.error());
  std::int64_t openBankSum = openBankUah.value();
  addOpen = addTo(openBankSum, openBankFx.value(), QStringLiteral("рух коштів"));
  if (!addOpen)
    return Result<CashFlowStatement>::fail(addOpen.error());
  std::int64_t closeCashSum = closeCashUah.value();
  addOpen = addTo(closeCashSum, closeCashFx.value(), QStringLiteral("рух коштів"));
  if (!addOpen)
    return Result<CashFlowStatement>::fail(addOpen.error());
  std::int64_t closeBankSum = closeBankUah.value();
  addOpen = addTo(closeBankSum, closeBankFx.value(), QStringLiteral("рух коштів"));
  if (!addOpen)
    return Result<CashFlowStatement>::fail(addOpen.error());

  CashFlowStatement st;
  st.from = from;
  st.to = to;
  st.openingCash = Money::fromCents(openCashSum);
  st.openingBank = Money::fromCents(openBankSum);
  st.closingCash = Money::fromCents(closeCashSum);
  st.closingBank = Money::fromCents(closeBankSum);
  const Result<Money> openTotal = st.openingCash.plus(st.openingBank);
  if (!openTotal)
    return Result<CashFlowStatement>::fail(openTotal.error());
  st.openingTotal = openTotal.value();
  const Result<Money> closeTotal = st.closingCash.plus(st.closingBank);
  if (!closeTotal)
    return Result<CashFlowStatement>::fail(closeTotal.error());
  st.closingTotal = closeTotal.value();

  sql::Query q(db_);
  Status status =
      q.prepare(QStringLiteral("SELECT e.amount_cents, e.debit_account_id, e.credit_account_id, "
                               "da.code, da.name, ca.code, ca.name "
                               "FROM ledger_entries e "
                               "JOIN accounts da ON da.id = e.debit_account_id "
                               "JOIN accounts ca ON ca.id = e.credit_account_id "
                               "WHERE e.date >= :from AND e.date <= :to "
                               "AND (e.debit_account_id IN (:m1, :m2, :m3, :m4) "
                               "  OR e.credit_account_id IN (:n1, :n2, :n3, :n4)) "
                               "ORDER BY e.date, e.id"));
  if (!status)
    return Result<CashFlowStatement>::fail(status.error());
  q.bind(QStringLiteral(":from"), fromIso);
  q.bind(QStringLiteral(":to"), toIso);
  q.bind(QStringLiteral(":m1"), cashId);
  q.bind(QStringLiteral(":m2"), bankId);
  q.bind(QStringLiteral(":m3"), fxCashId);
  q.bind(QStringLiteral(":m4"), fxBankId);
  q.bind(QStringLiteral(":n1"), cashId);
  q.bind(QStringLiteral(":n2"), bankId);
  q.bind(QStringLiteral(":n3"), fxCashId);
  q.bind(QStringLiteral(":n4"), fxBankId);
  status = q.exec();
  if (!status)
    return Result<CashFlowStatement>::fail(status.error());

  struct AccAgg
  {
    QString code;
    QString name;
    std::int64_t incoming = 0;
    std::int64_t outgoing = 0;
  };
  QHash<qlonglong, AccAgg> byCorr;
  std::int64_t inCents = 0;
  std::int64_t outCents = 0;

  auto isMoney = [&](qlonglong id) {
    return id == cashId || id == bankId || id == fxCashId || id == fxBankId;
  };

  while (q.next()) {
    const std::int64_t amount = q.value(0).toLongLong();
    const qlonglong debitId = q.value(1).toLongLong();
    const qlonglong creditId = q.value(2).toLongLong();
    const bool debitMoney = isMoney(debitId);
    const bool creditMoney = isMoney(creditId);
    if (debitMoney && creditMoney) {
      // Внутрішнє переміщення між грошовими рахунками — не в чистий рух.
      continue;
    }
    if (debitMoney) {
      Status add = addTo(inCents, amount, QStringLiteral("рух коштів"));
      if (!add)
        return Result<CashFlowStatement>::fail(add.error());
      AccAgg & row = byCorr[creditId];
      if (row.code.isEmpty()) {
        row.code = q.value(5).toString();
        row.name = q.value(6).toString();
      }
      add = addTo(row.incoming, amount, QStringLiteral("рух коштів"));
      if (!add)
        return Result<CashFlowStatement>::fail(add.error());
    } else if (creditMoney) {
      Status add = addTo(outCents, amount, QStringLiteral("рух коштів"));
      if (!add)
        return Result<CashFlowStatement>::fail(add.error());
      AccAgg & row = byCorr[debitId];
      if (row.code.isEmpty()) {
        row.code = q.value(3).toString();
        row.name = q.value(4).toString();
      }
      add = addTo(row.outgoing, amount, QStringLiteral("рух коштів"));
      if (!add)
        return Result<CashFlowStatement>::fail(add.error());
    }
  }

  st.incoming = Money::fromCents(inCents);
  st.outgoing = Money::fromCents(outCents);

  QList<qlonglong> keys = byCorr.keys();
  std::sort(keys.begin(), keys.end(),
            [&](qlonglong a, qlonglong b) { return byCorr.value(a).code < byCorr.value(b).code; });
  for (qlonglong id : keys) {
    const AccAgg & agg = byCorr.value(id);
    CashFlowCorrRow row;
    row.accountCode = agg.code;
    row.accountName = agg.name;
    row.incoming = Money::fromCents(agg.incoming);
    row.outgoing = Money::fromCents(agg.outgoing);
    st.byCorrespondence.push_back(std::move(row));
  }
  return Result<CashFlowStatement>::ok(std::move(st));
}

Result<Profitability> CashFlowReport::profitability(QDate from, QDate to) const
{
  FinancialStatementReport fs(db_);
  const Result<IncomeStatement> income = fs.income(from, to);
  if (!income)
    return Result<Profitability>::fail(income.error());
  const IncomeStatement & pl = income.value();
  Profitability p;
  p.from = from;
  p.to = to;
  const Result<Money> rev1 = pl.goodsRevenue.plus(pl.serviceRevenue);
  if (!rev1)
    return Result<Profitability>::fail(rev1.error());
  const Result<Money> revenue = rev1.value().plus(pl.otherIncome);
  if (!revenue)
    return Result<Profitability>::fail(revenue.error());
  const Result<Money> costs = pl.cogs.plus(pl.admin);
  if (!costs)
    return Result<Profitability>::fail(costs.error());
  p.revenue = revenue.value();
  p.costs = costs.value();
  p.profit = pl.profit;
  if (p.revenue.cents() != 0) {
    const double bp =
        (static_cast<double>(p.profit.cents()) * 10000.0) / static_cast<double>(p.revenue.cents());
    p.marginBp = static_cast<int>(bp >= 0.0 ? bp + 0.5 : bp - 0.5);
  }
  return Result<Profitability>::ok(std::move(p));
}

} // namespace fin
