#include "utils/Config.h"
#include <fstream>
#include <stdexcept>

Config& Config::instance() {
    static Config inst;
    return inst;
}

void Config::load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("Cannot open config: " + path);
    f >> data_;
}

std::string Config::dbHost()     const { return data_["database"]["host"]; }
int         Config::dbPort()     const { return data_["database"]["port"]; }
std::string Config::dbName()     const { return data_["database"]["dbname"]; }
std::string Config::dbUser()     const { return data_["database"]["user"]; }
std::string Config::dbPassword() const { return data_["database"]["password"]; }

std::string Config::dbConnStr() const {
    return "host="     + dbHost()     +
           " port="    + std::to_string(dbPort()) +
           " dbname="  + dbName()     +
           " user="    + dbUser()     +
           " password="+ dbPassword();
}

std::string Config::gateioBaseUrl()      const { return data_["gateio"]["base_url"]; }
int         Config::gateioRateLimitMs()  const { return data_["gateio"]["rate_limit_ms"]; }
std::string Config::coingeckoBaseUrl()   const { return data_["coingecko"]["base_url"]; }
int         Config::coingeckoRateLimitMs() const { return data_["coingecko"]["rate_limit_ms"]; }

std::vector<std::string> Config::tradingPairs() const {
    return data_["trading_pairs"].get<std::vector<std::string>>();
}

std::string Config::coingeckoId(const std::string& pair) const {
    auto it = data_["coingecko_ids"].find(pair);
    if (it == data_["coingecko_ids"].end()) return "";
    return it->get<std::string>();
}

int         Config::historicalDays()        const { return data_["historical_days"]; }
std::string Config::candlestickInterval()   const { return data_["candlestick_interval"]; }
std::string Config::reportsDir()            const { return data_["reports_dir"]; }

double Config::anomalyZscoreThreshold()  const { return data_["anomaly"]["zscore_threshold"]; }
double Config::anomalyIqrMultiplier()    const { return data_["anomaly"]["iqr_multiplier"]; }
double Config::anomalyMaDeviationPct()   const { return data_["anomaly"]["ma_deviation_pct"]; }
double Config::anomalyVolumeSpikeSigma() const { return data_["anomaly"]["volume_spike_sigma"]; }

int    Config::forecastSmaWindow()    const { return data_["forecast"]["sma_window"]; }
double Config::forecastEmaAlpha()     const { return data_["forecast"]["ema_alpha"]; }
int    Config::forecastLinRegWindow() const { return data_["forecast"]["linear_reg_window"]; }
int    Config::forecastDays()         const { return data_["forecast"]["forecast_days"]; }
