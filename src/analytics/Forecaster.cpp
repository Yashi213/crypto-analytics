#include "analytics/Forecaster.h"
#include "utils/Logger.h"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <ctime>

Forecaster::Forecaster(DatabaseManager& db,
                        int    sma_window,
                        double ema_alpha,
                        int    lin_reg_win,
                        int    forecast_days)
    : db_(db), sma_window_(sma_window), ema_alpha_(ema_alpha),
      lin_reg_win_(lin_reg_win), forecast_days_(forecast_days) {}

// ─── OLS helper ───────────────────────────────────────────────────────────────
std::pair<double, double>
Forecaster::ols(const std::vector<double>& x, const std::vector<double>& y) {
    size_t n = std::min(x.size(), y.size());
    if (n < 2) return {0, 0};
    double sx = 0, sy = 0, sxy = 0, sxx = 0;
    for (size_t i = 0; i < n; ++i) {
        sx  += x[i]; sy  += y[i];
        sxy += x[i] * y[i]; sxx += x[i] * x[i];
    }
    double denom = n * sxx - sx * sx;
    if (denom == 0) return {0, sy / n};
    double slope     = (n * sxy - sx * sy) / denom;
    double intercept = (sy - slope * sx) / n;
    return {slope, intercept};
}

// ─── SMA forecast ─────────────────────────────────────────────────────────────
// Average of the last `sma_window` closes. Flat forecast (no trend) — serves as
// the conservative baseline. Confidence band widens with the horizon (√d).
std::vector<ForecastResult>
Forecaster::smaForecast(int pair_id, const std::vector<Candlestick>& candles) {
    if (static_cast<int>(candles.size()) < sma_window_) return {};

    double sma_price = 0, sma_vol = 0;
    for (int i = static_cast<int>(candles.size()) - sma_window_;
         i < static_cast<int>(candles.size()); ++i) {
        sma_price += candles[i].close_price;
        sma_vol   += candles[i].volume;
    }
    sma_price /= sma_window_;
    sma_vol   /= sma_window_;

    // Std dev of prices in the window (base for confidence interval)
    double var = 0;
    for (int i = static_cast<int>(candles.size()) - sma_window_;
         i < static_cast<int>(candles.size()); ++i) {
        double d = candles[i].close_price - sma_price;
        var += d * d;
    }
    double sd = std::sqrt(var / sma_window_);

    std::time_t now       = std::time(nullptr);
    std::time_t day_start = (now / 86400) * 86400; // UTC midnight
    std::vector<ForecastResult> out;
    for (int d = 1; d <= forecast_days_; ++d) {
        double band = 2.0 * sd * std::sqrt(static_cast<double>(d));
        ForecastResult f;
        f.pair_id          = pair_id;
        f.created_at       = now;
        f.model_type       = "SMA";
        f.forecast_for     = day_start + static_cast<std::time_t>(d) * 86400;
        f.predicted_price  = sma_price;
        f.predicted_volume = sma_vol;
        f.confidence_lower = sma_price - band;
        f.confidence_upper = sma_price + band;
        out.push_back(f);
    }
    return out;
}

// ─── Holt's double exponential smoothing ───────────────────────────────────────
// Tracks both a smoothed level and a smoothed trend, so the forecast is a sloped
// line (level + h·trend) rather than a flat value. This is the "exponential
// smoothing" model from the task, upgraded to capture direction.
//   level_t = α·y_t + (1-α)·(level_{t-1} + trend_{t-1})
//   trend_t = β·(level_t - level_{t-1}) + (1-β)·trend_{t-1}
//   forecast(h) = level_n + h·trend_n
std::vector<ForecastResult>
Forecaster::holtForecast(int pair_id, const std::vector<Candlestick>& candles) {
    int n = static_cast<int>(candles.size());
    if (n < 3) return {};

    const double a = ema_alpha_;   // level smoothing
    const double b = beta_;        // trend smoothing

    double level = candles[0].close_price;
    double trend = candles[1].close_price - candles[0].close_price;
    double lvl_v = candles[0].volume;
    double trd_v = candles[1].volume - candles[0].volume;

    double resid = 0;
    int    rc    = 0;
    for (int i = 1; i < n; ++i) {
        double prev_level = level, prev_trend = trend;
        double one_step   = prev_level + prev_trend;   // forecast for y[i]
        double y          = candles[i].close_price;

        level = a * y + (1 - a) * (prev_level + prev_trend);
        trend = b * (level - prev_level) + (1 - b) * prev_trend;
        resid += (y - one_step) * (y - one_step);
        ++rc;

        // Volume tracked the same way
        double pv = lvl_v, pt = trd_v;
        double yv = candles[i].volume;
        lvl_v = a * yv + (1 - a) * (pv + pt);
        trd_v = b * (lvl_v - pv) + (1 - b) * pt;
    }
    double sd = std::sqrt(resid / std::max(1, rc));

    std::time_t now       = std::time(nullptr);
    std::time_t day_start = (now / 86400) * 86400;
    std::vector<ForecastResult> out;
    for (int d = 1; d <= forecast_days_; ++d) {
        double price = level + d * trend;
        double band  = 2.0 * sd * std::sqrt(static_cast<double>(d));
        ForecastResult f;
        f.pair_id          = pair_id;
        f.created_at       = now;
        f.model_type       = "Holt";
        f.forecast_for     = day_start + static_cast<std::time_t>(d) * 86400;
        f.predicted_price  = price;
        f.predicted_volume = lvl_v + d * trd_v;
        f.confidence_lower = price - band;
        f.confidence_upper = price + band;
        out.push_back(f);
    }
    return out;
}

// ─── Linear Regression forecast ───────────────────────────────────────────────
// OLS straight line through the last `lin_reg_win` closes, extrapolated forward.
std::vector<ForecastResult>
Forecaster::linearRegressionForecast(int pair_id,
                                      const std::vector<Candlestick>& candles) {
    int win = std::min(lin_reg_win_, static_cast<int>(candles.size()));
    if (win < 2) return {};

    size_t start = candles.size() - win;
    std::vector<double> xs(win), ys_price(win), ys_vol(win);
    for (int i = 0; i < win; ++i) {
        xs[i]       = static_cast<double>(i);
        ys_price[i] = candles[start + i].close_price;
        ys_vol[i]   = candles[start + i].volume;
    }

    auto [sp, ip] = ols(xs, ys_price);
    auto [sv, iv] = ols(xs, ys_vol);

    // Residual RMSE for confidence band
    double resid_p = 0;
    for (int i = 0; i < win; ++i) {
        double e = ys_price[i] - (sp * xs[i] + ip);
        resid_p += e * e;
    }
    double rmse = std::sqrt(resid_p / win);

    std::time_t now       = std::time(nullptr);
    std::time_t day_start = (now / 86400) * 86400;
    std::vector<ForecastResult> out;
    for (int d = 1; d <= forecast_days_; ++d) {
        double x    = static_cast<double>(win - 1 + d);
        double band = 2.0 * rmse * std::sqrt(static_cast<double>(d));
        ForecastResult f;
        f.pair_id          = pair_id;
        f.created_at       = now;
        f.model_type       = "LinearRegression";
        f.forecast_for     = day_start + static_cast<std::time_t>(d) * 86400;
        f.predicted_price  = sp * x + ip;
        f.predicted_volume = sv * x + iv;
        f.confidence_lower = f.predicted_price - band;
        f.confidence_upper = f.predicted_price + band;
        out.push_back(f);
    }
    return out;
}

// ─── one-step-ahead backtest ────────────────────────────────────────────────────
// For each model, walk the last `test_days` historical points and make a
// one-step-ahead prediction using only data strictly before each point, then
// compare to the actual close. Returns MAPE (%) and RMSE per model so the report
// can say which method tracked the series best.
std::map<std::string, Forecaster::ModelAccuracy>
Forecaster::backtest(int pair_id, int test_days) {
    auto candles = db_.getCandlesticks(pair_id);
    std::map<std::string, ModelAccuracy> out;
    int n = static_cast<int>(candles.size());
    if (n < sma_window_ + 5) return out;

    std::vector<double> px(n);
    for (int i = 0; i < n; ++i) px[i] = candles[i].close_price;

    int t0 = std::max(sma_window_ + 1, n - test_days);

    auto accumulate = [&](double pred, double actual,
                          double& abs_pct, double& sq, int& cnt) {
        if (actual <= 0) return;
        abs_pct += std::abs(actual - pred) / actual;
        sq      += (actual - pred) * (actual - pred);
        ++cnt;
    };

    // SMA: mean of previous sma_window closes
    {
        double ap = 0, sq = 0; int c = 0;
        for (int t = t0; t < n; ++t) {
            double s = 0;
            for (int j = t - sma_window_; j < t; ++j) s += px[j];
            accumulate(s / sma_window_, px[t], ap, sq, c);
        }
        if (c) out["SMA"] = { ap / c * 100.0, std::sqrt(sq / c), c };
    }

    // Holt: re-run recursion up to t-1, predict level+trend
    {
        double ap = 0, sq = 0; int c = 0;
        const double a = ema_alpha_, b = beta_;
        for (int t = t0; t < n; ++t) {
            double level = px[0], trend = px[1] - px[0];
            for (int i = 1; i < t; ++i) {
                double pl = level, pt = trend;
                level = a * px[i] + (1 - a) * (pl + pt);
                trend = b * (level - pl) + (1 - b) * pt;
            }
            accumulate(level + trend, px[t], ap, sq, c);
        }
        if (c) out["Holt"] = { ap / c * 100.0, std::sqrt(sq / c), c };
    }

    // Linear regression: fit on previous lin_reg_win closes, predict next
    {
        double ap = 0, sq = 0; int c = 0;
        for (int t = t0; t < n; ++t) {
            int win = std::min(lin_reg_win_, t);
            std::vector<double> xs(win), ys(win);
            for (int i = 0; i < win; ++i) { xs[i] = i; ys[i] = px[t - win + i]; }
            auto [s, ip] = ols(xs, ys);
            accumulate(s * win + ip, px[t], ap, sq, c);
        }
        if (c) out["LinearRegression"] = { ap / c * 100.0, std::sqrt(sq / c), c };
    }

    return out;
}

// ─── run all models ───────────────────────────────────────────────────────────
std::vector<ForecastResult>
Forecaster::forecastAndSave(int pair_id) {
    auto candles = db_.getCandlesticks(pair_id);
    if (candles.empty()) {
        LOG_WARN("No candles for pair_id=" + std::to_string(pair_id));
        return {};
    }

    std::vector<ForecastResult> all;
    auto sma  = smaForecast(pair_id, candles);
    auto holt = holtForecast(pair_id, candles);
    auto lr   = linearRegressionForecast(pair_id, candles);

    all.insert(all.end(), sma.begin(),  sma.end());
    all.insert(all.end(), holt.begin(), holt.end());
    all.insert(all.end(), lr.begin(),   lr.end());

    db_.saveForecasts(all);
    LOG_INFO("Forecasts saved for pair_id=" + std::to_string(pair_id) +
             " (" + std::to_string(all.size()) + " points)");
    return all;
}

std::vector<ForecastResult>
Forecaster::getForecasts(int pair_id, int limit) {
    return db_.getForecasts(pair_id, limit);
}
