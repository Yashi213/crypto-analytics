#pragma once
#include <vector>
#include <string>
#include "models/Models.h"
#include "database/DatabaseManager.h"

class AnomalyDetector {
public:
    explicit AnomalyDetector(DatabaseManager& db,
                              double zscore_threshold    = 3.0,
                              double iqr_multiplier      = 1.5,
                              double ma_deviation_pct    = 10.0,
                              double volume_spike_sigma  = 3.0);

    // Run all detectors and persist anomalies; returns detected events
    std::vector<Anomaly> detectAndSave(int pair_id);

    // Individual detectors
    std::vector<Anomaly> detectPriceAnomalies(int pair_id,
                                               const std::vector<Candlestick>& candles);
    std::vector<Anomaly> detectVolumeAnomalies(int pair_id,
                                                const std::vector<Candlestick>& candles);
    std::vector<Anomaly> detectMovingAverageBreaks(int pair_id,
                                                    const std::vector<Candlestick>& candles,
                                                    int ma_window = 20);

    // Retrieve stored anomalies
    std::vector<Anomaly> getAnomalies(int pair_id = 0, int limit = 100);

private:
    DatabaseManager& db_;
    double zscore_threshold_;
    double iqr_multiplier_;
    double ma_deviation_pct_;
    double volume_spike_sigma_;

    static std::vector<double> logReturns(const std::vector<Candlestick>& c);
    static double mean(const std::vector<double>& v);
    static double stddev(const std::vector<double>& v);
};
