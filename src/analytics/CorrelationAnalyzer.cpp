#include "analytics/CorrelationAnalyzer.h"
#include "utils/Logger.h"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <map>
#include <stdexcept>
#include <ctime>

CorrelationAnalyzer::CorrelationAnalyzer(DatabaseManager& db) : db_(db) {}

// ─── Pearson ──────────────────────────────────────────────────────────────────
double CorrelationAnalyzer::pearson(const std::vector<double>& x,
                                     const std::vector<double>& y) {
    size_t n = std::min(x.size(), y.size());
    if (n < 2) return 0;

    double mx = std::accumulate(x.begin(), x.begin() + n, 0.0) / n;
    double my = std::accumulate(y.begin(), y.begin() + n, 0.0) / n;

    double num = 0, dx2 = 0, dy2 = 0;
    for (size_t i = 0; i < n; ++i) {
        double dxi = x[i] - mx, dyi = y[i] - my;
        num += dxi * dyi;
        dx2 += dxi * dxi;
        dy2 += dyi * dyi;
    }
    double denom = std::sqrt(dx2 * dy2);
    return (denom == 0) ? 0 : num / denom;
}

// ─── Spearman ─────────────────────────────────────────────────────────────────
static std::vector<double> ranks(std::vector<double> v) {
    size_t n = v.size();
    std::vector<size_t> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(),
              [&v](size_t a, size_t b){ return v[a] < v[b]; });
    std::vector<double> r(n);
    for (size_t i = 0; i < n; ) {
        size_t j = i;
        while (j < n && v[idx[j]] == v[idx[i]]) ++j;
        double avg_rank = (i + j - 1) / 2.0 + 1;
        for (size_t k = i; k < j; ++k) r[idx[k]] = avg_rank;
        i = j;
    }
    return r;
}

double CorrelationAnalyzer::spearman(std::vector<double> x,
                                      std::vector<double> y) {
    return pearson(ranks(x), ranks(y));
}

// ─── Aligned returns ──────────────────────────────────────────────────────────
std::pair<std::vector<double>, std::vector<double>>
CorrelationAnalyzer::alignedReturns(int pair1_id, int pair2_id, int period_days) {
    std::time_t now  = std::time(nullptr);
    std::time_t from = now - static_cast<std::time_t>(period_days) * 86400;

    auto c1 = db_.getCandlesticks(pair1_id, from, now);
    auto c2 = db_.getCandlesticks(pair2_id, from, now);

    // Build map timestamp -> price for each
    std::map<std::time_t, double> m1, m2;
    for (const auto& c : c1) m1[c.timestamp] = c.close_price;
    for (const auto& c : c2) m2[c.timestamp] = c.close_price;

    // Intersect timestamps
    std::vector<double> r1, r2;
    std::time_t prev1 = 0, prev2 = 0;
    for (const auto& [ts, p1] : m1) {
        auto it = m2.find(ts);
        if (it == m2.end()) continue;
        double p2 = it->second;
        if (prev1 > 0 && prev2 > 0 && prev1 > 0 && prev2 > 0) {
            r1.push_back(std::log(p1 / prev1));
            r2.push_back(std::log(p2 / prev2));
        }
        prev1 = p1; prev2 = p2;
    }
    return {r1, r2};
}

// ─── Compute & save all pairs ─────────────────────────────────────────────────
void CorrelationAnalyzer::computeAndSave(const std::vector<int>& pair_ids,
                                          int period_days) {
    std::time_t now = std::time(nullptr);
    for (size_t i = 0; i < pair_ids.size(); ++i) {
        for (size_t j = i + 1; j < pair_ids.size(); ++j) {
            auto [r1, r2] = alignedReturns(pair_ids[i], pair_ids[j], period_days);
            if (r1.size() < 5) continue;

            CorrelationMetric m;
            m.pair1_id             = pair_ids[i];
            m.pair2_id             = pair_ids[j];
            m.calculated_at        = now;
            m.period_days          = period_days;
            m.pearson_correlation  = pearson(r1, r2);
            m.spearman_correlation = spearman(r1, r2);
            db_.saveCorrelationMetric(m);
            LOG_INFO("Correlation pair " + std::to_string(pair_ids[i]) +
                     " <-> " + std::to_string(pair_ids[j]) +
                     " Pearson=" + std::to_string(m.pearson_correlation));
        }
    }
}

std::vector<std::vector<double>>
CorrelationAnalyzer::correlationMatrix(const std::vector<int>& pair_ids,
                                        int period_days) {
    size_t n = pair_ids.size();
    std::vector<std::vector<double>> mat(n, std::vector<double>(n, 1.0));
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            auto [r1, r2] = alignedReturns(pair_ids[i], pair_ids[j], period_days);
            double p = (r1.size() >= 2) ? pearson(r1, r2) : 0;
            mat[i][j] = mat[j][i] = p;
        }
    }
    return mat;
}

std::vector<CorrelationMetric>
CorrelationAnalyzer::getStoredMetrics(int period_days) {
    return db_.getCorrelationMetrics(period_days);
}
