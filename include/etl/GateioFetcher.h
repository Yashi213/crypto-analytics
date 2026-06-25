#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "models/Models.h"

class GateioFetcher {
public:
    explicit GateioFetcher(const std::string& base_url, int rate_limit_ms = 300);

    // Returns up to `limit` daily candles for the given pair (newest last)
    std::vector<Candlestick> fetchCandlesticks(const std::string& currency_pair,
                                                const std::string& interval = "1d",
                                                int limit = 365);

    // Fetches current ticker snapshot
    MarketTicker fetchTicker(const std::string& currency_pair);

    // Returns all available spot pairs from Gate.io
    std::vector<std::string> fetchAvailablePairs();

private:
    std::string base_url_;
    int         rate_limit_ms_;

    void rateLimit() const;
};
