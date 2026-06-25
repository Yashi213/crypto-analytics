#include "analytics/StatisticsAnalyzer.h"
#include "utils/Logger.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>

StatisticsAnalyzer::StatisticsAnalyzer(DatabaseManager& db) : db_(db) {}

double StatisticsAnalyzer::percentile(std::vector<double> data, double p) {
    if (data.empty()) return 0;
    std::sort(data.begin(), data.end());
    double idx = p / 100.0 * (data.size() - 1);
    size_t lo  = static_cast<size_t>(idx);
    size_t hi  = lo + 1;
    if (hi >= data.size()) return data.back();
    double frac = idx - lo;
    return data[lo] * (1 - frac) + data[hi] * frac;
}

BasicStats StatisticsAnalyzer::calculate(std::vector<double> values) {
    if (values.empty()) return {};
    BasicStats s;
    s.count   = values.size();
    s.min_val = *std::min_element(values.begin(), values.end());
    s.max_val = *std::max_element(values.begin(), values.end());
    s.mean    = std::accumulate(values.begin(), values.end(), 0.0) / s.count;

    std::sort(values.begin(), values.end());
    size_t mid = s.count / 2;
    s.median   = (s.count % 2 == 0)
                 ? (values[mid - 1] + values[mid]) / 2.0
                 : values[mid];

    double var = 0;
    for (double v : values) var += (v - s.mean) * (v - s.mean);
    s.std_dev = std::sqrt(var / s.count);

    s.q1 = percentile(values, 25);
    s.q3 = percentile(values, 75);
    return s;
}

BasicStats StatisticsAnalyzer::priceStats(int pair_id,
                                           std::time_t from,
                                           std::time_t to) {
    auto candles = db_.getCandlesticks(pair_id, from, to);
    std::vector<double> prices;
    prices.reserve(candles.size());
    for (const auto& c : candles) prices.push_back(c.close_price);
    return calculate(prices);
}

BasicStats StatisticsAnalyzer::volumeStats(int pair_id,
                                            std::time_t from,
                                            std::time_t to) {
    auto candles = db_.getCandlesticks(pair_id, from, to);
    std::vector<double> vols;
    vols.reserve(candles.size());
    for (const auto& c : candles) vols.push_back(c.volume);
    return calculate(vols);
}

double StatisticsAnalyzer::priceChange(int pair_id, int days) {
    std::time_t now  = std::time(nullptr);
    std::time_t from = now - static_cast<std::time_t>(days) * 86400;
    auto candles = db_.getCandlesticks(pair_id, from, now);
    if (candles.size() < 2) return 0;
    double first = candles.front().close_price;
    double last  = candles.back().close_price;
    if (first == 0) return 0;
    return (last - first) / first * 100.0;
}

std::vector<StatisticsAnalyzer::Ranked>
StatisticsAnalyzer::topByMarketCap(int limit) {
    auto pairs = db_.getAllTradingPairs();
    std::vector<Ranked> result;
    for (const auto& p : pairs) {
        auto ticker = db_.getLatestTicker(p.id);
        if (ticker.market_cap <= 0) continue;
        result.push_back({p, ticker.market_cap});
    }
    std::sort(result.begin(), result.end(),
              [](const Ranked& a, const Ranked& b){ return a.value > b.value; });
    if (static_cast<int>(result.size()) > limit) result.resize(limit);
    return result;
}

std::vector<StatisticsAnalyzer::Ranked>
StatisticsAnalyzer::topByVolume(int limit) {
    auto pairs = db_.getAllTradingPairs();
    std::vector<Ranked> result;
    for (const auto& p : pairs) {
        auto ticker = db_.getLatestTicker(p.id);
        if (ticker.volume_24h <= 0) continue;
        result.push_back({p, ticker.volume_24h});
    }
    std::sort(result.begin(), result.end(),
              [](const Ranked& a, const Ranked& b){ return a.value > b.value; });
    if (static_cast<int>(result.size()) > limit) result.resize(limit);
    return result;
}

std::vector<StatisticsAnalyzer::Ranked>
StatisticsAnalyzer::topByReturn(int limit, int days) {
    auto pairs = db_.getAllTradingPairs();
    std::vector<Ranked> result;
    for (const auto& p : pairs) {
        double chg = priceChange(p.id, days);
        result.push_back({p, chg});
    }
    std::sort(result.begin(), result.end(),
              [](const Ranked& a, const Ranked& b){ return a.value > b.value; });
    if (static_cast<int>(result.size()) > limit) result.resize(limit);
    return result;
}

void StatisticsAnalyzer::printSummary(int pair_id) {
    auto pair = db_.getTradingPairById(pair_id);
    auto ps   = priceStats(pair_id);
    auto vs   = volumeStats(pair_id);

    std::cout << "\n=== " << pair.pair_symbol << " Summary ===\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Price  — min: " << ps.min_val
              << "  max: " << ps.max_val
              << "  mean: " << ps.mean
              << "  median: " << ps.median
              << "  std_dev: " << ps.std_dev << "\n";
    std::cout << "Volume — min: " << vs.min_val
              << "  max: " << vs.max_val
              << "  mean: " << vs.mean << "\n";
    std::cout << "30d return: " << priceChange(pair_id, 30) << "%\n";
}
