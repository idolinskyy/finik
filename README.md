# ![Фінік](./packaging/icons/hicolor/32x32/apps/finik.png) Фінік

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![Qt6](https://img.shields.io/badge/Qt-6.10%2B-green.svg)](https://www.qt.io/)
[![SQLite3](https://img.shields.io/badge/Database-SQLite%203-lightgrey.svg)](https://www.sqlite.org/)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Tests](https://img.shields.io/badge/Catch2-228%20cases-success.svg)](tests/)

**Фінік** — десктопна бухгалтерська система для малого і середнього бізнесу в Україні (C++17, Qt 6, SQLite). Суми в копійках (`int64`), подвійний запис у `ledger_entries`, проведення через `PostingEngine` з перевіркою цілісності.

## Принципи

1. **Гроші та кількість** — лише `int64` (копійки; міліодиниці з 3 знаками). Без `float`/`double` для сум.
2. **Шари** — `fin_core` без UI; `Finik` — Qt Widgets.
3. **Проведення** — незмінні проведені документи (зміна лише через `unpost`); блокування `closed_through`.
4. **Цілісність** — `IntegrityChecker`: баланс Дт=Кт, склад ↔ 281, ОЗ/НМА/МШП ↔ субрахунки.

## Облік

- Торгівля та послуги, повернення, переміщення, замовлення.
- Склад: середньозважена собівартість, інвентаризація, лишки/нестачі.
- Каса (КО-1/КО-2/КО-4) і банк (виписки CSV/XML/Monobank).
- ПДВ (перша подія, 643/644→641), податкові накладні, XML ЄРПН/M.E.Doc.
- Зарплата і кадри: штат, табель П-5, ПДФО/ВЗ/ЄСВ, відпускні, лікарняні.
- ОЗ, НМА, МШП (прямолінійна амортизація), виробництво (BOM).
- Мультивалюта (курси НБУ), звіти (ОСВ, баланс 1-м/2-м, книга доходів ФОП тощо), PDF/CSV.

## Для кого

Цільовий режим v0.1 — **одне робоче місце** (локальний SQLite), не клієнт–сервер.

- **Можна:** щоденні книги — торгівля, каса, банк, склад, ПДВ-реєстр, зарплата, ОЗ, закриття місяця, ОСВ.
- **Податкові накладні XML** — для підпису й реєстрації в M.E.Doc / Кабінеті ДПС (так і задумано; КЕП у Фініку немає).
- **XML декларацій** (ПДВ, 4ДФ, ЄП) — каркас без XSD ДПС, **не файл для подачі**.
- Кілька ролей (`admin` / `accountant` / `cashier` / `viewer`) працюють **по черзі на тому ж файлі**, не одночасно з двох ПК.

## Доступ (RBAC)

- `admin` — повний доступ, користувачі, компанія, backup.
- `accountant` — облік, проведення, зарплата, звіти.
- `cashier` — лише `CashIn` / `CashOut`.
- `viewer` — лише читання.

## Збірка

Потрібні: C++17, CMake 3.16+, Qt 6 (`Widgets`, `Sql`, `Network`, `PrintSupport`), SQLite 3, Catch2 v3 (для тестів).

```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
./Finik
./tests/fin_tests
```

Локальні перевірки (формат): `./scripts/check.sh` — див. [CONTRIBUTING.md](CONTRIBUTING.md).

## Пакування

Щоб поставити Фінік без збірки з сирців (Linux):

```bash
./scripts/package.sh
```

У `dist/` з’являться `.deb` і `.tar.gz` (CPack). AppImage — лише якщо в `PATH` є `linuxdeploy`. CI / GitHub Actions для цього **не** запускаємо (на холді); реліз на GitHub — вручну: тег `v0.1.x` і вкладені файли з `dist/`.

Пакет ставить бінарник `Finik`, ярлик `.desktop` і іконки hicolor. База лишається в `~/.local/share/Finik/Finik/` — встановлення пакета її не чіпає.

## Дані

SQLite (WAL). Qt кладе дані в `organizationName/applicationName` (обидва
`Finik`), тому каталог подвійний:

- Linux: `~/.local/share/Finik/Finik/finik.sqlite`
- Windows: `%APPDATA%\Finik\Finik\finik.sqlite`
- macOS: `~/Library/Application Support/Finik/Finik/finik.sqlite`

Резервне копіювання та поетапне відновлення — `applyStagedRestore`.

## Документація

- [docs/DEVELOPER_GUIDE.md](docs/DEVELOPER_GUIDE.md) — гайд для розробника без бухгалтерії: дебет/кредит, рахунки, карта C++ файлів.
- [PROJECT_DOCS.md](PROJECT_DOCS.md) — підсистеми, схема БД (v27), проведення.
- [docs/ROADMAP_1C.md](docs/ROADMAP_1C.md) — що є / частково / план.
- [CONTRIBUTING.md](CONTRIBUTING.md) — стандарти коду. Вбудована довідка бухгалтера — F1 (`help/*.html`).

## Статус і відповідальність

Фінік поширюється **як є (as is)**, без жодних гарантій придатності чи безпомилковості обліку.
Це вільне ПЗ на стадії активної розробки, а не комерційний продукт із договором підтримки.

- Перед роботою з реальними даними зробіть резервну копію і перевірте проводки самостійно.
- Автор не несе відповідальності за фінансові, податкові чи інші наслідки використання.
- Питання в Issues вітаються, але відповіді та виправлення **не гарантуються**.

Повні умови — у [LICENSE](LICENSE) (GNU GPL v3, розділи про відсутність гарантії).

## Ліцензія

GNU GPL v3 — див. [LICENSE](LICENSE).

Copyright (C) 2026 Ihor Dolinskyi

Фінік використовує [Qt](https://www.qt.io/). Qt is a trademark of The Qt Company
Ltd. and is available under LGPLv3, GPLv3, and commercial licenses. See
[Qt licensing](https://www.qt.io/licensing/).
Сторонні матеріали (Inter, Material Icons) — [NOTICE](NOTICE).
