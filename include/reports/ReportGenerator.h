#pragma once
#include <string>
#include <vector>
#include <hpdf.h>
#include "database/DatabaseManager.h"
#include "analytics/StatisticsAnalyzer.h"
#include "analytics/VolatilityAnalyzer.h"
#include "analytics/CorrelationAnalyzer.h"
#include "analytics/AnomalyDetector.h"
#include "analytics/Forecaster.h"

class ReportGenerator {
public:
    ReportGenerator(DatabaseManager&     db,
                    StatisticsAnalyzer&  stats,
                    VolatilityAnalyzer&  vol,
                    CorrelationAnalyzer& corr,
                    AnomalyDetector&     anom,
                    Forecaster&          fcast,
                    const std::string&   output_dir = "reports");

    // Individual asset report (price dynamics, volume, volatility, return)
    std::string generateAssetReport(int pair_id);

    // Comparison of multiple assets
    std::string generateComparativeReport(const std::vector<int>& pair_ids);

    // Correlation matrix + narrative
    std::string generateCorrelationReport(const std::vector<int>& pair_ids,
                                           int period_days = 30);

    // List of anomalies with charts
    std::string generateAnomalyReport(const std::vector<int>& pair_ids);

    // Forecast report
    std::string generateForecastReport(int pair_id);

    // All-in-one master report
    std::string generateFullReport(const std::vector<int>& pair_ids);

private:
    DatabaseManager&     db_;
    StatisticsAnalyzer&  stats_;
    VolatilityAnalyzer&  vol_;
    CorrelationAnalyzer& corr_;
    AnomalyDetector&     anom_;
    Forecaster&          fcast_;
    std::string          output_dir_;

    // PDF layout helpers
    static void      addTitle(HPDF_Doc pdf, HPDF_Page page,
                               const std::string& text, float y);
    static void      addSection(HPDF_Doc pdf, HPDF_Page page,
                                 const std::string& text, float& y);
    static void      addText(HPDF_Doc pdf, HPDF_Page page,
                              const std::string& text,
                              float x, float& y, float font_size = 10.f);
    static void      addKeyValue(HPDF_Doc pdf, HPDF_Page page,
                                  const std::string& key,
                                  const std::string& val,
                                  float x, float& y);
    static HPDF_Page newPage(HPDF_Doc pdf);
    static void      hpdfErrorHandler(HPDF_STATUS error_no,
                                       HPDF_STATUS detail_no,
                                       void* user_data);

    static std::string timestamp();
    static std::string fmt(double v, int dec = 2);
    static std::string pctFmt(double v);
    static std::string dateFmt(std::time_t t);
    std::string        outputPath(const std::string& name) const;
};
