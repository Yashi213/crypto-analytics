#pragma once
#include <string>
#include <ctime>
#include <vector>

struct Asset {
    int         id       = 0;
    std::string symbol;
    std::string name;
    std::time_t added_at = 0;
};

struct Exchange {
    int         id  = 0;
    std::string name;
    std::string url;
};

struct TradingPair {
    int         id            = 0;
    std::string base_asset;
    std::string quote_asset;
    int         exchange_id   = 0;
    std::string pair_symbol;
    std::string exchange_name;
};

struct Candlestick {
    long long   id          = 0;
    int         pair_id     = 0;
    std::time_t timestamp   = 0;
    double      open_price  = 0;
    double      close_price = 0;
    double      high_price  = 0;
    double      low_price   = 0;
    double      volume      = 0;
};

struct MarketTicker {
    long long   id                    = 0;
    int         pair_id               = 0;
    std::time_t timestamp             = 0;
    double      last_price            = 0;
    double      market_cap            = 0;
    double      volume_24h            = 0;
    double      price_change_24h_pct  = 0;
};

struct VolatilityMetric {
    long long   id            = 0;
    int         pair_id       = 0;
    std::time_t calculated_at = 0;
    std::string period_type;   // "daily" | "weekly" | "monthly"
    double      volatility    = 0;
    double      std_dev       = 0;
};

struct CorrelationMetric {
    long long   id                   = 0;
    int         pair1_id             = 0;
    int         pair2_id             = 0;
    std::time_t calculated_at        = 0;
    double      pearson_correlation  = 0;
    double      spearman_correlation = 0;
    int         period_days          = 0;
};

struct Anomaly {
    long long   id                  = 0;
    int         pair_id             = 0;
    std::time_t detected_at         = 0;
    std::string anomaly_type;
    std::string description;
    double      severity            = 0;
    double      price_at_detection  = 0;
    double      volume_at_detection = 0;
};

struct ForecastResult {
    long long   id               = 0;
    int         pair_id          = 0;
    std::time_t created_at       = 0;
    std::string model_type;
    std::time_t forecast_for     = 0;
    double      predicted_price  = 0;
    double      predicted_volume = 0;
    double      confidence_lower = 0;
    double      confidence_upper = 0;
};

struct BasicStats {
    double min_val = 0;
    double max_val = 0;
    double mean    = 0;
    double median  = 0;
    double std_dev = 0;
    double q1      = 0;
    double q3      = 0;
    size_t count   = 0;
};
