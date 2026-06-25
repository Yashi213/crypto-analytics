#include "analytics/AnomalyDetector.h"
#include "utils/Logger.h"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <sstream>
#include <ctime>
#include <iomanip>

AnomalyDetector::AnomalyDetector(DatabaseManager& db,
                                   double zscore_threshold,
                                   double iqr_multiplier,
                                   double ma_deviation_pct,
                                   double volume_spike_sigma)
    : db_(db),
      zscore_threshold_(zscore_threshold),
      iqr_multiplier_(iqr_multiplier),
      ma_deviation_pct_(ma_deviation_pct),
      volume_spike_sigma_(volume_spike_sigma) {}

double AnomalyDetector::mean(const std::vector<double>& v) {
    if (v.empty()) return 0;
    return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
}

double AnomalyDetector::stddev(const std::vector<double>& v) {
    if (v.size() < 2) return 0;
    double m   = mean(v);
    double var = 0;
    for (double x : v) var += (x - m) * (x - m);
    return std::sqrt(var / v.size());
}

std::vector<double>
AnomalyDetector::logReturns(const std::vector<Candlestick>& c) {
    std::vector<double> r;
    for (size_t i = 1; i < c.size(); ++i) {
        if (c[i - 1].close_price > 0)
            r.push_back(std::log(c[i].close_price / c[i - 1].close_price));
    }
    return r;
}

// ─── price anomalies (z-score on log-returns) ────────────────────────────────
std::vector<Anomaly>
AnomalyDetector::detectPriceAnomalies(int pair_id,
                                       const std::vector<Candlestick>& candles) {
    if (candles.size() < 5) return {};
    auto returns = logReturns(candles);
    double mu  = mean(returns);
    double sig = stddev(returns);
    if (sig == 0) return {};

    std::vector<Anomaly> out;
    for (size_t i = 0; i < returns.size(); ++i) {
        double z = (returns[i] - mu) / sig;
        if (std::abs(z) < zscore_threshold_) continue;

        Anomaly a;
        a.pair_id            = pair_id;
        a.detected_at        = candles[i + 1].timestamp;
        a.severity           = std::abs(z);
        a.price_at_detection = candles[i + 1].close_price;
        a.volume_at_detection = candles[i + 1].volume;

        if (returns[i] > 0) {
            a.anomaly_type = "price_spike";
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2);
            ss << "Скачок цены: +" << returns[i] * 100 << "% (z=" << z << ")";
            a.description = ss.str();
        } else {
            a.anomaly_type = "price_drop";
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2);
            ss << "Падение цены: " << returns[i] * 100 << "% (z=" << z << ")";
            a.description = ss.str();
        }
        out.push_back(a);
    }
    return out;
}

// ─── volume anomalies (z-score on volumes) ───────────────────────────────────
std::vector<Anomaly>
AnomalyDetector::detectVolumeAnomalies(int pair_id,
                                        const std::vector<Candlestick>& candles) {
    if (candles.size() < 5) return {};
    std::vector<double> vols;
    for (const auto& c : candles) vols.push_back(c.volume);

    double mu  = mean(vols);
    double sig = stddev(vols);
    if (sig == 0) return {};

    std::vector<Anomaly> out;
    for (size_t i = 0; i < candles.size(); ++i) {
        double z = (vols[i] - mu) / sig;
        if (z < volume_spike_sigma_) continue;

        Anomaly a;
        a.pair_id             = pair_id;
        a.detected_at         = candles[i].timestamp;
        a.anomaly_type        = "volume_spike";
        a.severity            = z;
        a.price_at_detection  = candles[i].close_price;
        a.volume_at_detection = candles[i].volume;
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "Всплеск объёма: " << vols[i] << " (z=" << z << ", среднее=" << mu << ")";
        a.description = ss.str();
        out.push_back(a);
    }
    return out;
}

// ─── moving-average crossovers (with hysteresis) ─────────────────────────────
// A pure price/MA crossover whipsaws — in a sideways market the daily price
// touches the MA almost every day, flooding the chart. We instead track a
// "regime" (price above vs below the MA) that flips only when the price closes
// more than `ma_deviation_pct_` past the MA. That deadband filters daily noise
// and, because the regime holds through a sustained trend, marks just ONE event
// per decisive break — no cluster, no whipsaw.
std::vector<Anomaly>
AnomalyDetector::detectMovingAverageBreaks(int pair_id,
                                            const std::vector<Candlestick>& candles,
                                            int ma_window) {
    if (static_cast<int>(candles.size()) <= ma_window + 1) return {};
    std::vector<Anomaly> out;

    int regime = 0; // +1 = price above MA, -1 = below, 0 = not yet established

    for (size_t i = ma_window; i < candles.size(); ++i) {
        double sum = 0;
        for (int k = 0; k < ma_window; ++k) sum += candles[i - 1 - k].close_price;
        double ma = sum / ma_window;
        if (ma == 0) continue;
        double dev = (candles[i].close_price - ma) / ma * 100.0;

        if (regime == 0) {                 // establish initial side, no event
            regime = (dev >= 0) ? 1 : -1;
            continue;
        }
        // Flip only on a decisive break through the deadband
        bool flip_down = (regime ==  1 && dev <= -ma_deviation_pct_);
        bool flip_up   = (regime == -1 && dev >=  ma_deviation_pct_);
        if (!flip_down && !flip_up) continue;

        regime = flip_up ? 1 : -1;

        Anomaly a;
        a.pair_id             = pair_id;
        a.detected_at         = candles[i].timestamp;
        a.severity            = std::abs(dev);
        a.price_at_detection  = candles[i].close_price;
        a.volume_at_detection = candles[i].volume;

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        if (flip_up) {
            a.anomaly_type = "ma_breakout_up";
            ss << "Пробой вверх: цена +" << dev << "% над " << ma_window
               << "-дн. СС (" << ma << ")";
        } else {
            a.anomaly_type = "ma_breakout_down";
            ss << "Пробой вниз: цена " << dev << "% под " << ma_window
               << "-дн. СС (" << ma << ")";
        }
        a.description = ss.str();
        out.push_back(a);
    }
    return out;
}

// ─── detect & save ────────────────────────────────────────────────────────────
std::vector<Anomaly>
AnomalyDetector::detectAndSave(int pair_id) {
    auto candles = db_.getCandlesticks(pair_id);
    std::vector<Anomaly> all;

    auto pa = detectPriceAnomalies(pair_id, candles);
    auto va = detectVolumeAnomalies(pair_id, candles);
    auto ma = detectMovingAverageBreaks(pair_id, candles);

    all.insert(all.end(), pa.begin(), pa.end());
    all.insert(all.end(), va.begin(), va.end());
    all.insert(all.end(), ma.begin(), ma.end());

    db_.saveAnomalies(all);
    LOG_INFO("Detected " + std::to_string(all.size()) +
             " anomalies for pair_id=" + std::to_string(pair_id));
    return all;
}

std::vector<Anomaly>
AnomalyDetector::getAnomalies(int pair_id, int limit) {
    return db_.getAnomalies(pair_id, limit);
}
