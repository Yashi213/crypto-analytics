#pragma once
#include <vector>
#include <string>
#include <map>
#include "models/Models.h"
#include "database/DatabaseManager.h"

class Forecaster {
public:
    explicit Forecaster(DatabaseManager& db,
                        int    sma_window    = 14,
                        double ema_alpha     = 0.2,
                        int    lin_reg_win   = 30,
                        int    forecast_days = 7);

    // Run all models for a pair and persist
    std::vector<ForecastResult> forecastAndSave(int pair_id);

    // --- Individual model methods ---

    // Simple Moving Average — flat-level forecast (baseline)
    std::vector<ForecastResult> smaForecast(int pair_id,
                                             const std::vector<Candlestick>& candles);

    // Holt's double exponential smoothing — level + trend (trending forecast)
    std::vector<ForecastResult> holtForecast(int pair_id,
                                             const std::vector<Candlestick>& candles);

    // Ordinary Least Squares linear regression on prices
    std::vector<ForecastResult> linearRegressionForecast(
        int pair_id,
        const std::vector<Candlestick>& candles);

    // Retrieve stored forecasts
    std::vector<ForecastResult> getForecasts(int pair_id, int limit = 30);

    // --- Accuracy evaluation ---
    // One-step-ahead backtest error of each model over the most recent
    // `test_days` historical points. Lets the report state which model fits best.
    struct ModelAccuracy {
        double mape = 0;  // mean absolute percentage error, %
        double rmse = 0;  // root-mean-square error, price units
        int    n    = 0;  // number of test points
    };
    std::map<std::string, ModelAccuracy> backtest(int pair_id, int test_days = 60);

private:
    DatabaseManager& db_;
    int    sma_window_;
    double ema_alpha_;
    int    lin_reg_win_;
    int    forecast_days_;

    double beta_ = 0.1; // Holt trend-smoothing factor

    // OLS: returns (slope, intercept)
    static std::pair<double, double> ols(const std::vector<double>& x,
                                         const std::vector<double>& y);
};
