# Crypto Analytics

Система сбора и анализа данных криптовалютного рынка на **C++17** и **PostgreSQL**.
Загружает исторические свечи и рыночные данные с публичных API бирж, сохраняет их в БД,
считает статистику, волатильность, корреляции, ищет аномалии, строит прогнозы и формирует отчёты в **PDF**.

> Учебный проект (практика), Университет ИТМО.

## Возможности

- **ETL**: загрузка дневных свечей (OHLCV) и тикеров с Gate.io, рыночной капитализации с CoinGecko;
  очистка данных, upsert в PostgreSQL, журнал запусков ETL в отдельной таблице.
- **Статистика**: базовые метрики цены и объёма, перцентили, изменение цены за период,
  топы по капитализации, объёму и доходности.
- **Волатильность**: годовая волатильность по лог-доходностям, скользящая волатильность.
- **Корреляции**: Пирсон и Спирмен между парами, матрица корреляций.
- **Аномалии**: выбросы цены (z-score, IQR), всплески объёма, пробои скользящей средней.
- **Прогноз**: SMA, метод Холта, линейная регрессия; бэктест моделей с оценкой точности.
- **Отчёты**: PDF с графиками — по активу, сравнительный, корреляционный, по аномалиям, по прогнозу, полный.
- Интерактивное меню: запуск с настройками из конфига или изменение параметров прогона без правки файла.

## Архитектура

```
Gate.io API ─┐
             ├─► ETL (GateioFetcher, CoinGeckoFetcher, ETLPipeline) ─► PostgreSQL
CoinGecko ───┘                                                          │
                                                                        ▼
                       Analytics (Statistics, Volatility, Correlation, Anomaly, Forecaster)
                                                                        │
                                                                        ▼
                                                     ReportGenerator ─► PDF-отчёты
```

```
include/ , src/
├── etl/         HTTP-клиенты бирж и ETL-пайплайн
├── database/    DatabaseManager — работа с PostgreSQL через libpqxx
├── analytics/   статистика, волатильность, корреляции, аномалии, прогнозы
├── reports/     генерация PDF (libharu), графики
├── models/      структуры данных
└── utils/       конфиг, логгер, HTTP-клиент (libcurl), работа со временем
sql/schema.sql   схема БД
```

**Схема БД**: `exchanges`, `assets`, `trading_pairs`, `candlesticks`, `market_tickers`,
`volatility_metrics`, `correlation_metrics`, `anomalies`, `forecasts`, `etl_logs`
с индексами по `(pair_id, timestamp)` для основных запросов.

## Стек

C++17 · CMake · PostgreSQL · libpqxx · libcurl · nlohmann/json · libharu · vcpkg

## Запуск

### 1. База данных

```bash
psql -U postgres -f setup_db.sql
```

Скрипт создаёт базу `crypto_analytics` и применяет `sql/schema.sql`.
Схема также применяется автоматически при запуске приложения.

### 2. Конфигурация

```bash
cp config/config.example.json config/config.json
```

Укажите параметры подключения к PostgreSQL. Файл `config/config.json` не коммитится.

### 3. Сборка

**Windows** (зависимости ставятся через vcpkg автоматически):

```bat
build.bat
```

**Linux / macOS** (через vcpkg):

```bash
vcpkg install curl nlohmann-json libpqxx libharu
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 4. Запуск

```bash
./build/crypto_analytics                  # конфиг по умолчанию: config/config.json
./build/crypto_analytics path/to/config.json
```

Отчёты сохраняются в каталог `reports/` (настраивается в конфиге).

## Автор

Поляк Яков, М3307, Университет ИТМО
