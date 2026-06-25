#include "etl/CoinGeckoFetcher.h"
#include "utils/HttpClient.h"
#include "utils/Logger.h"
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <ctime>

using json = nlohmann::json;

CoinGeckoFetcher::CoinGeckoFetcher(const std::string& base_url, int rate_limit_ms)
    : base_url_(base_url), rate_limit_ms_(rate_limit_ms) {}

void CoinGeckoFetcher::rateLimit() const {
    std::this_thread::sleep_for(std::chrono::milliseconds(rate_limit_ms_));
}

std::map<std::string, MarketTicker>
CoinGeckoFetcher::fetchMarketData(const std::vector<std::string>& coin_ids,
                                   const std::string& vs_currency) {
    if (coin_ids.empty()) return {};

    std::string ids;
    for (size_t i = 0; i < coin_ids.size(); ++i) {
        if (i) ids += "%2C";
        ids += coin_ids[i];
    }

    std::string url = base_url_ + "/coins/markets?vs_currency=" +
                      vs_currency + "&ids=" + ids +
                      "&order=market_cap_desc&per_page=100&page=1"
                      "&sparkline=false&price_change_percentage=24h";

    LOG_INFO("Fetching CoinGecko market data");
    auto resp = HttpClient::get(url, {"Accept: application/json"});
    rateLimit();

    if (!resp.success) {
        LOG_WARN("CoinGecko fetch failed [" +
                 std::to_string(resp.status_code) + "]");
        return {};
    }

    json body = json::parse(resp.body, nullptr, false);
    if (body.is_discarded() || !body.is_array()) return {};

    std::map<std::string, MarketTicker> out;
    for (const auto& item : body) {
        if (!item.contains("id")) continue;
        std::string coin_id = item["id"].get<std::string>();
        MarketTicker mt;
        mt.pair_id              = 0; // resolved by pipeline
        mt.timestamp            = std::time(nullptr);
        mt.last_price           = item.value("current_price",       0.0);
        mt.market_cap           = item.value("market_cap",          0.0);
        mt.volume_24h           = item.value("total_volume",        0.0);
        mt.price_change_24h_pct = item.value(
            "price_change_percentage_24h", 0.0);
        out[coin_id] = mt;
    }
    LOG_INFO("CoinGecko: got data for " + std::to_string(out.size()) + " coins");
    return out;
}
