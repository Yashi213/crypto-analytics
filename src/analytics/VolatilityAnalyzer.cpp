#include "analytics/VolatilityAnalyzer.h"
#include "utils/Logger.h"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <ctime>

VolatilityAnalyzer::VolatilityAnalyzer(DatabaseManager& db) : db_(db) {}

// ─── log-return std dev ───────────────────────────────────────────────────────
double VolatilityAnalyzer::stdDevReturns(const std::vector<double>& prices) {
    if (prices.size() < 2) return 0;
    std::vector<double> returns;
    returns.reserve(prices.size() - 1);
    for (size_t i = 1; i < prices.size(); ++i) {
        if (prices[i - 1] <= 0) continue;
        returns.push_back(std::log(prices[i] / prices[i - 1]));
    }
    if (returns.empty()) return 0;
    double mean = std::accumulate(returns.begin(), returns.end(), 0.0) /
                  returns.size();
    double var  = 0;
    for (double r : returns) var += (r - mean) * (r - mean);
    return std::sqrt(var / returns.size());
}

double VolatilityAnalyzer::annualisedVolatility(const std::vector<double>& prices,
                                                  int trading_days_per_year) {
    double sd = stdDevReturns(prices);
    return sd * std::sqrt(static_cast<double>(trading_days_per_year));
}

// ─── internal helper ──────────────────────────────────────────────────────────
VolatilityMetric VolatilityAnalyzer::computeMetric(
        int pair_id,
        const std::vector<Candlestick>& candles,
        const std::string& period_type) {
    std::vector<double> prices;
    prices.reserve(candles.size());
    for (const auto& c : candles) prices.push_back(c.close_price);

    VolatilityMetric m;
    m.pair_id       = pair_id;
    m.calculated_at = std::time(nullptr);
    m.period_type   = period_type;
    m.std_dev       = stdDevReturns(prices);
    // annualise with 365 trading days (crypto runs 24/7)
    m.volatility    = m.std_dev * std::sqrt(365.0);
    return m;
}

// ─── public: compute & persist ────────────────────────────────────────────────
// Uses all available candles to compute σ_daily (std dev of log-returns),
// then scales it to daily / weekly / monthly horizons:
//   daily   = σ × 1      (per day)
//   weekly  = σ × √7     (per week, 7 calendar days for 24/7 crypto)
//   monthly = σ × √30    (per month)
// std_dev field always stores σ_daily so consumers can re-scale if needed.
void VolatilityAnalyzer::computeAndSave(int pair_id) {
    auto candles = db_.getCandlesticks(pair_id);
    if (candles.size() < 2) return;

    std::vector<double> prices;
    prices.reserve(candles.size());
    for (const auto& c : candles) prices.push_back(c.close_price);

    double sigma = stdDevReturns(prices); // daily σ (log-return std dev)
    std::time_t now = std::time(nullptr);

    auto make = [&](const std::string& pt, double scale) {
        VolatilityMetric m;
        m.pair_id       = pair_id;
        m.calculated_at = now;
        m.period_type   = pt;
        m.std_dev       = sigma;
        m.volatility    = sigma * scale;
        return m;
    };

    std::vector<VolatilityMetric> metrics = {
        make("daily",   1.0),
        make("weekly",  std::sqrt(7.0)),
        make("monthly", std::sqrt(30.0)),
    };

    db_.saveVolatilityMetrics(metrics);
    LOG_INFO("Volatility computed for pair_id=" + std::to_string(pair_id));
}

std::vector<VolatilityMetric>
VolatilityAnalyzer::getMetrics(int pair_id, const std::string& period) {
    return db_.getVolatilityMetrics(pair_id, period);
}

// ─── rolling volatility series ────────────────────────────────────────────────
std::vector<std::pair<std::time_t, double>>
VolatilityAnalyzer::rollingVolatility(int pair_id, int window_days) {
    auto candles = db_.getCandlesticks(pair_id);
    std::vector<std::pair<std::time_t, double>> result;
    if (static_cast<int>(candles.size()) < window_days) return result;

    for (size_t i = window_days; i <= candles.size(); ++i) {
        std::vector<double> prices;
        for (size_t j = i - window_days; j < i; ++j)
            prices.push_back(candles[j].close_price);
        double vol = annualisedVolatility(prices);
        result.emplace_back(candles[i - 1].timestamp, vol);
    }
    return result;
}
