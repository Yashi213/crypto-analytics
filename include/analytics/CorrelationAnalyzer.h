#pragma once
#include <vector>
#include <string>
#include "models/Models.h"
#include "database/DatabaseManager.h"

class CorrelationAnalyzer {
public:
    explicit CorrelationAnalyzer(DatabaseManager& db);

    // Pearson correlation between two return series of equal length
    static double pearson(const std::vector<double>& x,
                          const std::vector<double>& y);

    // Spearman rank correlation
    static double spearman(std::vector<double> x, std::vector<double> y);

    // Compute all pairwise correlations for the given pair IDs and persist
    void computeAndSave(const std::vector<int>& pair_ids, int period_days = 30);

    // Returns the full correlation matrix (pairs x pairs)
    std::vector<std::vector<double>>
    correlationMatrix(const std::vector<int>& pair_ids, int period_days = 30);

    std::vector<CorrelationMetric> getStoredMetrics(int period_days);

private:
    DatabaseManager& db_;

    // Returns aligned daily log-return series for both pairs
    std::pair<std::vector<double>, std::vector<double>>
    alignedReturns(int pair1_id, int pair2_id, int period_days);
};
