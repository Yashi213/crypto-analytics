#pragma once
#include <vector>
#include <string>
#include "models/Models.h"
#include "database/DatabaseManager.h"

class StatisticsAnalyzer {
public:
    explicit StatisticsAnalyzer(DatabaseManager& db);

    static BasicStats calculate(std::vector<double> values);

    BasicStats priceStats(int pair_id,
                          std::time_t from = 0,
                          std::time_t to   = 0);
    BasicStats volumeStats(int pair_id,
                           std::time_t from = 0,
                           std::time_t to   = 0);

    // Percentage price change over last `days` calendar days
    double priceChange(int pair_id, int days);

    // Returns (pair, value) ranked list
    struct Ranked { TradingPair pair; double value; };
    std::vector<Ranked> topByMarketCap(int limit = 10);
    std::vector<Ranked> topByVolume(int limit = 10);
    std::vector<Ranked> topByReturn(int limit = 10, int days = 30);

    // Print summary to stdout
    void printSummary(int pair_id);

private:
    DatabaseManager& db_;
    static double percentile(std::vector<double> sorted, double p);
};
