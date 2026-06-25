#pragma once
#include <vector>
#include <string>
#include "models/Models.h"
#include "database/DatabaseManager.h"

class VolatilityAnalyzer {
public:
    explicit VolatilityAnalyzer(DatabaseManager& db);

    // Annualised historical volatility from log-returns
    static double annualisedVolatility(const std::vector<double>& prices,
                                       int trading_days_per_year = 365);
    static double stdDevReturns(const std::vector<double>& prices);

    // Compute and persist metrics for all configured periods
    void computeAndSave(int pair_id);

    // Retrieve stored metrics
    std::vector<VolatilityMetric> getMetrics(int pair_id,
                                              const std::string& period = "");

    // Rolling daily volatility series (window = `window_days`)
    std::vector<std::pair<std::time_t, double>>
    rollingVolatility(int pair_id, int window_days = 30);

private:
    DatabaseManager& db_;

    // Compute for a specific window of candles
    VolatilityMetric computeMetric(int pair_id,
                                   const std::vector<Candlestick>& candles,
                                   const std::string& period_type);
};
