#include "database/DatabaseManager.h"
#include "utils/Logger.h"
#include "utils/TimeUtils.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iomanip>
#include <ctime>

// ─── constructor ──────────────────────────────────────────────────────────────
DatabaseManager::DatabaseManager(const std::string& conn_str)
    : conn_str_(conn_str) {
    conn_ = std::make_unique<pqxx::connection>(conn_str);
    LOG_INFO("Database connected: " + conn_str);
}

// ─── schema ───────────────────────────────────────────────────────────────────
void DatabaseManager::initializeSchema(const std::string& schema_path) {
    std::ifstream f(schema_path);
    if (!f.is_open())
        throw std::runtime_error("Cannot open schema: " + schema_path);
    std::ostringstream ss;
    ss << f.rdbuf();
    pqxx::work txn(*conn_);
    txn.exec(ss.str());
    txn.commit();
    LOG_INFO("Schema initialized");
}

// ─── time helpers ─────────────────────────────────────────────────────────────
std::string DatabaseManager::timeToSql(std::time_t t) {
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S+00");
    return ss.str();
}

std::time_t DatabaseManager::sqlToTime(const std::string& s) {
    std::tm tm{};
    std::istringstream ss(s);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    return timegm(&tm);
}

void DatabaseManager::ensureConnection() {
    if (!conn_ || !conn_->is_open()) {
        conn_ = std::make_unique<pqxx::connection>(conn_str_);
    }
}

// ─── exchanges ────────────────────────────────────────────────────────────────
int DatabaseManager::upsertExchange(const std::string& name,
                                     const std::string& url) {
    ensureConnection();
    pqxx::work txn(*conn_);
    auto r = txn.exec_params(
        "INSERT INTO exchanges(name,url) VALUES($1,$2) "
        "ON CONFLICT(name) DO UPDATE SET url=EXCLUDED.url "
        "RETURNING id",
        name, url);
    txn.commit();
    return r[0][0].as<int>();
}

std::vector<Exchange> DatabaseManager::getAllExchanges() {
    ensureConnection();
    pqxx::work txn(*conn_);
    auto r = txn.exec("SELECT id,name,url FROM exchanges ORDER BY id");
    txn.commit();
    std::vector<Exchange> out;
    for (const auto& row : r)
        out.push_back({row[0].as<int>(), row[1].as<std::string>(),
                       row[2].is_null() ? "" : row[2].as<std::string>()});
    return out;
}

// ─── assets ───────────────────────────────────────────────────────────────────
int DatabaseManager::upsertAsset(const std::string& symbol,
                                  const std::string& name) {
    ensureConnection();
    pqxx::work txn(*conn_);
    auto r = txn.exec_params(
        "INSERT INTO assets(symbol,name) VALUES($1,$2) "
        "ON CONFLICT(symbol) DO UPDATE SET name=EXCLUDED.name "
        "RETURNING id",
        symbol, name);
    txn.commit();
    return r[0][0].as<int>();
}

std::vector<Asset> DatabaseManager::getAllAssets() {
    ensureConnection();
    pqxx::work txn(*conn_);
    auto r = txn.exec("SELECT id,symbol,name FROM assets ORDER BY id");
    txn.commit();
    std::vector<Asset> out;
    for (const auto& row : r)
        out.push_back({row[0].as<int>(), row[1].as<std::string>(),
                       row[2].as<std::string>()});
    return out;
}

// ─── trading pairs ────────────────────────────────────────────────────────────
int DatabaseManager::upsertTradingPair(const std::string& base,
                                        const std::string& quote,
                                        int exchange_id,
                                        const std::string& symbol) {
    ensureConnection();
    pqxx::work txn(*conn_);
    auto r = txn.exec_params(
        "INSERT INTO trading_pairs(base_asset,quote_asset,exchange_id,pair_symbol) "
        "VALUES($1,$2,$3,$4) "
        "ON CONFLICT(pair_symbol,exchange_id) DO UPDATE "
        "SET base_asset=EXCLUDED.base_asset "
        "RETURNING id",
        base, quote, exchange_id, symbol);
    txn.commit();
    return r[0][0].as<int>();
}

int DatabaseManager::getTradingPairId(const std::string& symbol, int exchange_id) {
    ensureConnection();
    pqxx::work txn(*conn_);
    auto r = txn.exec_params(
        "SELECT id FROM trading_pairs WHERE pair_symbol=$1 AND exchange_id=$2",
        symbol, exchange_id);
    txn.commit();
    if (r.empty()) return 0;
    return r[0][0].as<int>();
}

std::vector<TradingPair> DatabaseManager::getAllTradingPairs() {
    ensureConnection();
    pqxx::work txn(*conn_);
    auto r = txn.exec(
        "SELECT tp.id,tp.base_asset,tp.quote_asset,tp.exchange_id,"
        "tp.pair_symbol,e.name "
        "FROM trading_pairs tp JOIN exchanges e ON e.id=tp.exchange_id "
        "ORDER BY tp.id");
    txn.commit();
    std::vector<TradingPair> out;
    for (const auto& row : r)
        out.push_back({row[0].as<int>(), row[1].as<std::string>(),
                       row[2].as<std::string>(), row[3].as<int>(),
                       row[4].as<std::string>(), row[5].as<std::string>()});
    return out;
}

TradingPair DatabaseManager::getTradingPairById(int id) {
    ensureConnection();
    pqxx::work txn(*conn_);
    auto r = txn.exec_params(
        "SELECT tp.id,tp.base_asset,tp.quote_asset,tp.exchange_id,"
        "tp.pair_symbol,e.name "
        "FROM trading_pairs tp JOIN exchanges e ON e.id=tp.exchange_id "
        "WHERE tp.id=$1", id);
    txn.commit();
    if (r.empty()) return {};
    const auto row = r[0];
    return {row[0].as<int>(), row[1].as<std::string>(),
            row[2].as<std::string>(), row[3].as<int>(),
            row[4].as<std::string>(), row[5].as<std::string>()};
}

// ─── candlesticks ─────────────────────────────────────────────────────────────
void DatabaseManager::insertCandlesticks(const std::vector<Candlestick>& candles) {
    if (candles.empty()) return;
    ensureConnection();
    pqxx::work txn(*conn_);
    for (const auto& c : candles) {
        txn.exec_params(
            "INSERT INTO candlesticks"
            "(pair_id,timestamp,open_price,close_price,high_price,low_price,volume) "
            "VALUES($1,$2,$3,$4,$5,$6,$7) "
            "ON CONFLICT(pair_id,timestamp) DO UPDATE SET "
            "open_price=EXCLUDED.open_price, close_price=EXCLUDED.close_price, "
            "high_price=EXCLUDED.high_price, low_price=EXCLUDED.low_price, "
            "volume=EXCLUDED.volume",
            c.pair_id, timeToSql(c.timestamp),
            c.open_price, c.close_price, c.high_price, c.low_price, c.volume);
    }
    txn.commit();
}

std::vector<Candlestick> DatabaseManager::getCandlesticks(int pair_id,
                                                            std::time_t from,
                                                            std::time_t to,
                                                            int limit) {
    ensureConnection();
    pqxx::work txn(*conn_);
    // Integer pair_id is safe to inline; timestamps use our controlled format
    std::string q =
        "SELECT id,pair_id,EXTRACT(EPOCH FROM timestamp)::bigint,"
        "open_price,close_price,high_price,low_price,volume "
        "FROM candlesticks WHERE pair_id=" + std::to_string(pair_id);
    if (from) q += " AND timestamp >= '" + timeToSql(from) + "'";
    if (to)   q += " AND timestamp <= '" + timeToSql(to)   + "'";
    q += " ORDER BY timestamp";
    if (limit > 0) q += " LIMIT " + std::to_string(limit);

    auto r = txn.exec(q);
    txn.commit();
    std::vector<Candlestick> out;
    for (const auto& row : r) {
        Candlestick c;
        c.id          = row[0].as<long long>();
        c.pair_id     = row[1].as<int>();
        c.timestamp   = row[2].as<long long>();
        c.open_price  = row[3].as<double>();
        c.close_price = row[4].as<double>();
        c.high_price  = row[5].as<double>();
        c.low_price   = row[6].as<double>();
        c.volume      = row[7].as<double>();
        out.push_back(c);
    }
    return out;
}

// ─── market tickers ───────────────────────────────────────────────────────────
void DatabaseManager::insertMarketTicker(const MarketTicker& t) {
    ensureConnection();
    pqxx::work txn(*conn_);
    txn.exec_params(
        "INSERT INTO market_tickers"
        "(pair_id,timestamp,last_price,market_cap,volume_24h,price_change_24h_pct) "
        "VALUES($1,$2,$3,$4,$5,$6)",
        t.pair_id, timeToSql(t.timestamp),
        t.last_price, t.market_cap, t.volume_24h, t.price_change_24h_pct);
    txn.commit();
}

MarketTicker DatabaseManager::getLatestTicker(int pair_id) {
    ensureConnection();
    pqxx::work txn(*conn_);
    auto r = txn.exec_params(
        "SELECT id,pair_id,EXTRACT(EPOCH FROM timestamp)::bigint,"
        "last_price,market_cap,volume_24h,price_change_24h_pct "
        "FROM market_tickers WHERE pair_id=$1 "
        "ORDER BY timestamp DESC LIMIT 1",
        pair_id);
    txn.commit();
    if (r.empty()) return {};
    const auto row = r[0];
    MarketTicker mt;
    mt.id                   = row[0].as<long long>();
    mt.pair_id              = row[1].as<int>();
    mt.timestamp            = row[2].as<long long>();
    mt.last_price           = row[3].is_null() ? 0 : row[3].as<double>();
    mt.market_cap           = row[4].is_null() ? 0 : row[4].as<double>();
    mt.volume_24h           = row[5].is_null() ? 0 : row[5].as<double>();
    mt.price_change_24h_pct = row[6].is_null() ? 0 : row[6].as<double>();
    return mt;
}

// ─── volatility metrics ───────────────────────────────────────────────────────
void DatabaseManager::saveVolatilityMetrics(
        const std::vector<VolatilityMetric>& metrics) {
    if (metrics.empty()) return;
    ensureConnection();
    pqxx::work txn(*conn_);
    for (const auto& m : metrics) {
        txn.exec_params(
            "INSERT INTO volatility_metrics"
            "(pair_id,calculated_at,period_type,volatility,std_dev) "
            "VALUES($1,$2,$3,$4,$5) "
            "ON CONFLICT(pair_id,period_type) DO UPDATE SET "
            "calculated_at=EXCLUDED.calculated_at, "
            "volatility=EXCLUDED.volatility, std_dev=EXCLUDED.std_dev",
            m.pair_id, timeToSql(m.calculated_at),
            m.period_type, m.volatility, m.std_dev);
    }
    txn.commit();
}

std::vector<VolatilityMetric> DatabaseManager::getVolatilityMetrics(
        int pair_id, const std::string& period) {
    ensureConnection();
    pqxx::work txn(*conn_);
    std::string q =
        "SELECT id,pair_id,EXTRACT(EPOCH FROM calculated_at)::bigint,"
        "period_type,volatility,std_dev "
        "FROM volatility_metrics WHERE pair_id=" + std::to_string(pair_id);
    // period is always one of our controlled constants: "daily","weekly","monthly"
    if (!period.empty()) q += " AND period_type='" + period + "'";
    q += " ORDER BY calculated_at DESC";
    auto r = txn.exec(q);
    txn.commit();
    std::vector<VolatilityMetric> out;
    for (const auto& row : r) {
        VolatilityMetric m;
        m.id            = row[0].as<long long>();
        m.pair_id       = row[1].as<int>();
        m.calculated_at = row[2].as<long long>();
        m.period_type   = row[3].as<std::string>();
        m.volatility    = row[4].is_null() ? 0 : row[4].as<double>();
        m.std_dev       = row[5].is_null() ? 0 : row[5].as<double>();
        out.push_back(m);
    }
    return out;
}

// ─── correlation metrics ──────────────────────────────────────────────────────
void DatabaseManager::saveCorrelationMetric(const CorrelationMetric& m) {
    ensureConnection();
    pqxx::work txn(*conn_);
    txn.exec_params(
        "INSERT INTO correlation_metrics"
        "(pair1_id,pair2_id,calculated_at,pearson_correlation,"
        "spearman_correlation,period_days) "
        "VALUES($1,$2,$3,$4,$5,$6) "
        "ON CONFLICT(pair1_id,pair2_id,period_days) DO UPDATE SET "
        "calculated_at=EXCLUDED.calculated_at, "
        "pearson_correlation=EXCLUDED.pearson_correlation, "
        "spearman_correlation=EXCLUDED.spearman_correlation",
        m.pair1_id, m.pair2_id, timeToSql(m.calculated_at),
        m.pearson_correlation, m.spearman_correlation, m.period_days);
    txn.commit();
}

std::vector<CorrelationMetric>
DatabaseManager::getCorrelationMetrics(int period_days) {
    ensureConnection();
    pqxx::work txn(*conn_);
    auto r = txn.exec_params(
        "SELECT id,pair1_id,pair2_id,"
        "EXTRACT(EPOCH FROM calculated_at)::bigint,"
        "pearson_correlation,spearman_correlation,period_days "
        "FROM correlation_metrics WHERE period_days=$1 "
        "ORDER BY calculated_at DESC",
        period_days);
    txn.commit();
    std::vector<CorrelationMetric> out;
    for (const auto& row : r) {
        CorrelationMetric cm;
        cm.id                   = row[0].as<long long>();
        cm.pair1_id             = row[1].as<int>();
        cm.pair2_id             = row[2].as<int>();
        cm.calculated_at        = row[3].as<long long>();
        cm.pearson_correlation  = row[4].is_null() ? 0 : row[4].as<double>();
        cm.spearman_correlation = row[5].is_null() ? 0 : row[5].as<double>();
        cm.period_days          = row[6].as<int>();
        out.push_back(cm);
    }
    return out;
}

// ─── anomalies ────────────────────────────────────────────────────────────────
void DatabaseManager::saveAnomalies(const std::vector<Anomaly>& anomalies) {
    if (anomalies.empty()) return;
    ensureConnection();
    pqxx::work txn(*conn_);
    // Clear this pair's existing anomalies first so re-runs (or a changed
    // detection method) don't leave stale rows from previous runs behind.
    txn.exec_params("DELETE FROM anomalies WHERE pair_id=$1", anomalies[0].pair_id);
    for (const auto& a : anomalies) {
        txn.exec_params(
            "INSERT INTO anomalies"
            "(pair_id,detected_at,anomaly_type,description,"
            "severity,price_at_detection,volume_at_detection) "
            "VALUES($1,$2,$3,$4,$5,$6,$7)",
            a.pair_id, timeToSql(a.detected_at),
            a.anomaly_type, a.description,
            a.severity, a.price_at_detection, a.volume_at_detection);
    }
    txn.commit();
}

std::vector<Anomaly> DatabaseManager::getAnomalies(int pair_id, int limit) {
    ensureConnection();
    pqxx::work txn(*conn_);
    std::string q =
        "SELECT id,pair_id,EXTRACT(EPOCH FROM detected_at)::bigint,"
        "anomaly_type,description,severity,"
        "price_at_detection,volume_at_detection "
        "FROM anomalies";
    if (pair_id > 0) q += " WHERE pair_id=" + std::to_string(pair_id);
    q += " ORDER BY detected_at DESC";
    if (limit > 0) q += " LIMIT " + std::to_string(limit);
    auto r = txn.exec(q);
    txn.commit();
    std::vector<Anomaly> out;
    for (const auto& row : r) {
        Anomaly a;
        a.id                  = row[0].as<long long>();
        a.pair_id             = row[1].as<int>();
        a.detected_at         = row[2].as<long long>();
        a.anomaly_type        = row[3].as<std::string>();
        a.description         = row[4].is_null() ? "" : row[4].as<std::string>();
        a.severity            = row[5].is_null() ? 0  : row[5].as<double>();
        a.price_at_detection  = row[6].is_null() ? 0  : row[6].as<double>();
        a.volume_at_detection = row[7].is_null() ? 0  : row[7].as<double>();
        out.push_back(a);
    }
    return out;
}

// ─── forecasts ────────────────────────────────────────────────────────────────
void DatabaseManager::saveForecasts(const std::vector<ForecastResult>& forecasts) {
    if (forecasts.empty()) return;
    ensureConnection();
    pqxx::work txn(*conn_);
    // Delete all existing forecasts for these pairs/models before re-inserting
    // so stale rows from previous runs (with different timestamps) never accumulate.
    if (!forecasts.empty()) {
        int pid = forecasts[0].pair_id;
        txn.exec_params("DELETE FROM forecasts WHERE pair_id=$1", pid);
    }
    for (const auto& f : forecasts) {
        txn.exec_params(
            "INSERT INTO forecasts"
            "(pair_id,created_at,model_type,forecast_for,"
            "predicted_price,predicted_volume,confidence_lower,confidence_upper) "
            "VALUES($1,$2,$3,$4,$5,$6,$7,$8)",
            f.pair_id, timeToSql(f.created_at), f.model_type,
            timeToSql(f.forecast_for),
            f.predicted_price, f.predicted_volume,
            f.confidence_lower, f.confidence_upper);
    }
    txn.commit();
}

std::vector<ForecastResult>
DatabaseManager::getForecasts(int pair_id, int limit) {
    ensureConnection();
    pqxx::work txn(*conn_);
    auto r = txn.exec_params(
        "SELECT id,pair_id,EXTRACT(EPOCH FROM created_at)::bigint,"
        "model_type,EXTRACT(EPOCH FROM forecast_for)::bigint,"
        "predicted_price,predicted_volume,confidence_lower,confidence_upper "
        "FROM forecasts WHERE pair_id=$1 "
        "ORDER BY forecast_for, model_type LIMIT $2",
        pair_id, limit);
    txn.commit();
    std::vector<ForecastResult> out;
    for (const auto& row : r) {
        ForecastResult f;
        f.id               = row[0].as<long long>();
        f.pair_id          = row[1].as<int>();
        f.created_at       = row[2].as<long long>();
        f.model_type       = row[3].as<std::string>();
        f.forecast_for     = row[4].as<long long>();
        f.predicted_price  = row[5].is_null() ? 0 : row[5].as<double>();
        f.predicted_volume = row[6].is_null() ? 0 : row[6].as<double>();
        f.confidence_lower = row[7].is_null() ? 0 : row[7].as<double>();
        f.confidence_upper = row[8].is_null() ? 0 : row[8].as<double>();
        out.push_back(f);
    }
    return out;
}

// ─── ETL log ──────────────────────────────────────────────────────────────────
long long DatabaseManager::startEtlLog(const std::string& source,
                                        const std::string& op) {
    ensureConnection();
    pqxx::work txn(*conn_);
    auto r = txn.exec_params(
        "INSERT INTO etl_logs(source,operation,status) "
        "VALUES($1,$2,'running') RETURNING id",
        source, op);
    txn.commit();
    return r[0][0].as<long long>();
}

void DatabaseManager::completeEtlLog(long long log_id, int records,
                                      const std::string& status,
                                      const std::string& err_msg) {
    ensureConnection();
    pqxx::work txn(*conn_);
    if (err_msg.empty()) {
        txn.exec_params(
            "UPDATE etl_logs SET completed_at=NOW(), status=$1, "
            "records_processed=$2 WHERE id=$3",
            status, records, log_id);
    } else {
        txn.exec_params(
            "UPDATE etl_logs SET completed_at=NOW(), status=$1, "
            "records_processed=$2, error_message=$3 WHERE id=$4",
            status, records, err_msg, log_id);
    }
    txn.commit();
}
