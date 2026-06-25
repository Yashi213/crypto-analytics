#pragma once
#include <string>
#include <vector>
#include <memory>
#include <pqxx/pqxx>
#include "models/Models.h"

class DatabaseManager {
public:
    explicit DatabaseManager(const std::string& conn_str);

    void initializeSchema(const std::string& schema_path);

    // --- Exchanges ---
    int  upsertExchange(const std::string& name, const std::string& url);
    std::vector<Exchange> getAllExchanges();

    // --- Assets ---
    int  upsertAsset(const std::string& symbol, const std::string& name);
    std::vector<Asset> getAllAssets();

    // --- Trading pairs ---
    int  upsertTradingPair(const std::string& base, const std::string& quote,
                           int exchange_id, const std::string& symbol);
    int  getTradingPairId(const std::string& symbol, int exchange_id);
    std::vector<TradingPair> getAllTradingPairs();
    TradingPair getTradingPairById(int id);

    // --- Candlesticks ---
    void insertCandlesticks(const std::vector<Candlestick>& candles);
    std::vector<Candlestick> getCandlesticks(int pair_id,
                                              std::time_t from = 0,
                                              std::time_t to   = 0,
                                              int limit = 0);

    // --- Market tickers ---
    void         insertMarketTicker(const MarketTicker& ticker);
    MarketTicker getLatestTicker(int pair_id);

    // --- Volatility ---
    void saveVolatilityMetrics(const std::vector<VolatilityMetric>& metrics);
    std::vector<VolatilityMetric> getVolatilityMetrics(int pair_id,
                                                        const std::string& period = "");

    // --- Correlation ---
    void saveCorrelationMetric(const CorrelationMetric& m);
    std::vector<CorrelationMetric> getCorrelationMetrics(int period_days);

    // --- Anomalies ---
    void saveAnomalies(const std::vector<Anomaly>& anomalies);
    std::vector<Anomaly> getAnomalies(int pair_id = 0, int limit = 100);

    // --- Forecasts ---
    void saveForecasts(const std::vector<ForecastResult>& forecasts);
    std::vector<ForecastResult> getForecasts(int pair_id, int limit = 30);

    // --- ETL log ---
    long long startEtlLog(const std::string& source, const std::string& op);
    void      completeEtlLog(long long log_id, int records,
                              const std::string& status  = "success",
                              const std::string& err_msg = "");

private:
    std::unique_ptr<pqxx::connection> conn_;
    void ensureConnection();
    std::string conn_str_;

    static std::string timeToSql(std::time_t t);
    static std::time_t sqlToTime(const std::string& s);
};
