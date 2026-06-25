#pragma once
#include <string>
#include <vector>
#include <map>
#include "models/Models.h"

class CoinGeckoFetcher {
public:
    explicit CoinGeckoFetcher(const std::string& base_url,
                               int rate_limit_ms = 1200);

    // Returns map coin_id -> MarketTicker (pair_id left 0, filled by pipeline)
    std::map<std::string, MarketTicker>
    fetchMarketData(const std::vector<std::string>& coin_ids,
                    const std::string& vs_currency = "usd");

private:
    std::string base_url_;
    int         rate_limit_ms_;
    void rateLimit() const;
};
