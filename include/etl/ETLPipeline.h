#pragma once
#include <string>
#include <vector>
#include <map>
#include "database/DatabaseManager.h"
#include "etl/GateioFetcher.h"
#include "etl/CoinGeckoFetcher.h"

class ETLPipeline {
public:
    ETLPipeline(DatabaseManager& db,
                GateioFetcher&    gateio,
                CoinGeckoFetcher& coingecko,
                const std::vector<std::string>&     pairs,
                const std::map<std::string, std::string>& coingecko_ids,
                int historical_days = 365);

    // Full ETL: historical candles + current tickers
    void runFull();

    // Only update current tickers (fast refresh)
    void runTickerUpdate();

    // Only fetch historical OHLCV for all configured pairs
    void runHistorical();

private:
    DatabaseManager&  db_;
    GateioFetcher&    gateio_;
    CoinGeckoFetcher& coingecko_;
    std::vector<std::string>            pairs_;
    std::map<std::string, std::string>  coingecko_ids_;
    int historical_days_;
    int gateio_exchange_id_     = 0;
    int coingecko_exchange_id_  = 0;

    void ensureExchanges();
    void ensurePairs();
    int  getPairId(const std::string& symbol, int exchange_id);

    // Transform helpers
    static std::vector<Candlestick> cleanCandles(std::vector<Candlestick> raw);
};
