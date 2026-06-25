#pragma once
#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

class Config {
public:
    static Config& instance();

    void load(const std::string& path);

    // Database
    std::string dbHost()     const;
    int         dbPort()     const;
    std::string dbName()     const;
    std::string dbUser()     const;
    std::string dbPassword() const;
    std::string dbConnStr()  const;

    // API
    std::string gateioBaseUrl()      const;
    int         gateioRateLimitMs()  const;
    std::string coingeckoBaseUrl()   const;
    int         coingeckoRateLimitMs() const;

    // Assets
    std::vector<std::string>     tradingPairs()   const;
    std::string                  coingeckoId(const std::string& pair) const;

    // ETL
    int         historicalDays()        const;
    std::string candlestickInterval()   const;

    // Reports
    std::string reportsDir() const;

    // Anomaly
    double anomalyZscoreThreshold()  const;
    double anomalyIqrMultiplier()    const;
    double anomalyMaDeviationPct()   const;
    double anomalyVolumeSpikeSigma() const;

    // Forecast
    int    forecastSmaWindow()     const;
    double forecastEmaAlpha()      const;
    int    forecastLinRegWindow()  const;
    int    forecastDays()          const;

private:
    Config() = default;
    nlohmann::json data_;
};
