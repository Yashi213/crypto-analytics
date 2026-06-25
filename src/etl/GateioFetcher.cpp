#include "etl/GateioFetcher.h"
#include "utils/HttpClient.h"
#include "utils/Logger.h"
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <ctime>

using json = nlohmann::json;

GateioFetcher::GateioFetcher(const std::string& base_url, int rate_limit_ms)
    : base_url_(base_url), rate_limit_ms_(rate_limit_ms) {}

void GateioFetcher::rateLimit() const {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(rate_limit_ms_));
}

// ─── candlesticks ─────────────────────────────────────────────────────────────
// Gate.io v4 /spot/candlesticks returns an array of arrays:
// [timestamp_s, quote_volume, close, high, low, open, is_closed, base_volume]
std::vector<Candlestick>
GateioFetcher::fetchCandlesticks(const std::string& currency_pair,
                                   const std::string& interval,
                                   int limit) {
    std::string url = base_url_ + "/spot/candlesticks?currency_pair=" +
                      currency_pair + "&interval=" + interval +
                      "&limit=" + std::to_string(limit);

    LOG_INFO("Fetching candlesticks: " + url);
    auto resp = HttpClient::get(url, {"Accept: application/json"});

    if (!resp.success) {
        LOG_ERROR("Candlestick fetch failed [" +
                  std::to_string(resp.status_code) + "]: " + resp.body);
        return {};
    }

    rateLimit();

    json body = json::parse(resp.body, nullptr, false);
    if (body.is_discarded() || !body.is_array()) {
        LOG_ERROR("Invalid JSON from Gate.io candlesticks");
        return {};
    }

    std::vector<Candlestick> out;
    out.reserve(body.size());
    for (const auto& elem : body) {
        if (!elem.is_array() || elem.size() < 6) continue;
        try {
            Candlestick c;
            c.pair_id     = 0; // filled by ETLPipeline
            c.timestamp   = std::stoll(elem[0].get<std::string>());
            c.volume      = std::stod(elem[1].get<std::string>());
            c.close_price = std::stod(elem[2].get<std::string>());
            c.high_price  = std::stod(elem[3].get<std::string>());
            c.low_price   = std::stod(elem[4].get<std::string>());
            c.open_price  = std::stod(elem[5].get<std::string>());
            out.push_back(c);
        } catch (...) {
            LOG_WARN("Skipping malformed candlestick element");
        }
    }
    LOG_INFO("Fetched " + std::to_string(out.size()) +
             " candles for " + currency_pair);
    return out;
}

// ─── ticker ───────────────────────────────────────────────────────────────────
// GET /spot/tickers/{currency_pair}
MarketTicker GateioFetcher::fetchTicker(const std::string& currency_pair) {
    std::string url = base_url_ + "/spot/tickers?currency_pair=" + currency_pair;
    LOG_INFO("Fetching ticker: " + url);
    auto resp = HttpClient::get(url, {"Accept: application/json"});

    rateLimit();

    if (!resp.success) {
        LOG_ERROR("Ticker fetch failed: " + resp.body);
        return {};
    }

    json body = json::parse(resp.body, nullptr, false);
    if (body.is_discarded() || !body.is_array() || body.empty()) return {};

    auto& t = body[0];
    MarketTicker mt;
    mt.timestamp = std::time(nullptr);
    try {
        mt.last_price           = std::stod(t.value("last",       "0"));
        mt.volume_24h           = std::stod(t.value("quote_volume","0"));
        mt.price_change_24h_pct = std::stod(t.value("change_percentage","0"));
    } catch (...) {}
    return mt;
}

// ─── available pairs ──────────────────────────────────────────────────────────
std::vector<std::string> GateioFetcher::fetchAvailablePairs() {
    std::string url = base_url_ + "/spot/currency_pairs";
    auto resp = HttpClient::get(url, {"Accept: application/json"});
    rateLimit();

    if (!resp.success) return {};
    json body = json::parse(resp.body, nullptr, false);
    if (!body.is_array()) return {};

    std::vector<std::string> pairs;
    for (const auto& p : body) {
        if (p.contains("id"))
            pairs.push_back(p["id"].get<std::string>());
    }
    return pairs;
}
