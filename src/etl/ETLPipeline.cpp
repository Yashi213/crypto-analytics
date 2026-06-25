#include "etl/ETLPipeline.h"
#include "utils/Logger.h"
#include <algorithm>
#include <stdexcept>
#include <ctime>

ETLPipeline::ETLPipeline(DatabaseManager&  db,
                          GateioFetcher&    gateio,
                          CoinGeckoFetcher& coingecko,
                          const std::vector<std::string>&    pairs,
                          const std::map<std::string,std::string>& ids,
                          int historical_days)
    : db_(db), gateio_(gateio), coingecko_(coingecko),
      pairs_(pairs), coingecko_ids_(ids),
      historical_days_(historical_days) {
    ensureExchanges();
    ensurePairs();
}

void ETLPipeline::ensureExchanges() {
    gateio_exchange_id_    = db_.upsertExchange("gate.io",   "https://www.gate.io");
    coingecko_exchange_id_ = db_.upsertExchange("coingecko", "https://www.coingecko.com");
    LOG_INFO("Exchanges: gate.io=" + std::to_string(gateio_exchange_id_) +
             " coingecko=" + std::to_string(coingecko_exchange_id_));
}

void ETLPipeline::ensurePairs() {
    for (const auto& sym : pairs_) {
        // Parse "BTC_USDT" -> base="BTC", quote="USDT"
        auto pos = sym.find('_');
        std::string base  = (pos != std::string::npos) ? sym.substr(0, pos)  : sym;
        std::string quote = (pos != std::string::npos) ? sym.substr(pos + 1) : "USDT";
        db_.upsertTradingPair(base, quote, gateio_exchange_id_, sym);
    }
}

int ETLPipeline::getPairId(const std::string& symbol, int exchange_id) {
    return db_.getTradingPairId(symbol, exchange_id);
}

// ─── ETL: historical candles ──────────────────────────────────────────────────
void ETLPipeline::runHistorical() {
    LOG_INFO("=== ETL: historical candles ===");
    long long log_id = db_.startEtlLog("gate.io", "historical_candles");
    int total = 0;
    try {
        for (const auto& sym : pairs_) {
            int pair_id = getPairId(sym, gateio_exchange_id_);
            if (pair_id <= 0) {
                LOG_WARN("No pair_id for " + sym);
                continue;
            }
            auto candles = gateio_.fetchCandlesticks(sym, "1d", historical_days_);
            candles = cleanCandles(std::move(candles));
            for (auto& c : candles) c.pair_id = pair_id;
            db_.insertCandlesticks(candles);
            total += static_cast<int>(candles.size());
            LOG_INFO(sym + ": inserted " + std::to_string(candles.size()) + " candles");
        }
        db_.completeEtlLog(log_id, total);
    } catch (const std::exception& e) {
        db_.completeEtlLog(log_id, total, "failed", e.what());
        throw;
    }
}

// ─── ETL: ticker update ───────────────────────────────────────────────────────
void ETLPipeline::runTickerUpdate() {
    LOG_INFO("=== ETL: ticker update ===");
    long long log_id = db_.startEtlLog("gate.io+coingecko", "ticker_update");
    int total = 0;
    try {
        // 1. Gate.io tickers
        for (const auto& sym : pairs_) {
            int pair_id = getPairId(sym, gateio_exchange_id_);
            if (pair_id <= 0) continue;
            auto mt = gateio_.fetchTicker(sym);
            mt.pair_id = pair_id;
            db_.insertMarketTicker(mt);
            ++total;
        }

        // 2. CoinGecko market caps
        std::vector<std::string> cgids;
        for (const auto& sym : pairs_) {
            auto it = coingecko_ids_.find(sym);
            if (it != coingecko_ids_.end()) cgids.push_back(it->second);
        }
        auto cg_data = coingecko_.fetchMarketData(cgids);
        for (const auto& sym : pairs_) {
            auto it = coingecko_ids_.find(sym);
            if (it == coingecko_ids_.end()) continue;
            auto cg_it = cg_data.find(it->second);
            if (cg_it == cg_data.end()) continue;
            int pair_id = getPairId(sym, gateio_exchange_id_);
            if (pair_id <= 0) continue;
            MarketTicker mt = cg_it->second;
            mt.pair_id = pair_id;
            // Update market cap in the existing ticker record via upsert-style
            db_.insertMarketTicker(mt);
            ++total;
        }
        db_.completeEtlLog(log_id, total);
    } catch (const std::exception& e) {
        db_.completeEtlLog(log_id, total, "failed", e.what());
        throw;
    }
}

void ETLPipeline::runFull() {
    runHistorical();
    runTickerUpdate();
}

// ─── data cleaning ────────────────────────────────────────────────────────────
std::vector<Candlestick>
ETLPipeline::cleanCandles(std::vector<Candlestick> raw) {
    // Remove entries with zero/negative prices
    raw.erase(std::remove_if(raw.begin(), raw.end(), [](const Candlestick& c) {
        return c.close_price <= 0 || c.open_price <= 0 ||
               c.high_price  <= 0 || c.low_price  <= 0;
    }), raw.end());

    // Remove duplicates by timestamp (keep last)
    std::stable_sort(raw.begin(), raw.end(),
        [](const Candlestick& a, const Candlestick& b){
            return a.timestamp < b.timestamp;
        });
    raw.erase(std::unique(raw.begin(), raw.end(),
        [](const Candlestick& a, const Candlestick& b){
            return a.timestamp == b.timestamp;
        }), raw.end());

    // Clamp: high >= close >= low
    for (auto& c : raw) {
        c.high_price = std::max({c.high_price, c.open_price, c.close_price});
        c.low_price  = std::min({c.low_price,  c.open_price, c.close_price});
    }
    return raw;
}
