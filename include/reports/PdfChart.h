#pragma once
#include <string>
#include <vector>
#include <hpdf.h>

class PdfChart {
public:
    // Single-series line chart. Pass overlay=true to skip background/axes redraw
    // so multiple series can be drawn on the same chart area.
    static void lineChart(HPDF_Doc pdf, HPDF_Page page,
                          float x, float y, float w, float h,
                          const std::vector<double>& values,
                          const std::vector<std::string>& x_labels,
                          const std::string& title,
                          const std::string& y_label,
                          float r = 0.2f, float g = 0.4f, float b = 0.8f,
                          bool overlay = false);

    // Multi-series line chart with shared axes and legend
    static void multiLineChart(HPDF_Doc pdf, HPDF_Page page,
                               float x, float y, float w, float h,
                               const std::vector<std::vector<double>>& series,
                               const std::vector<std::string>& x_labels,
                               const std::vector<std::string>& series_names,
                               const std::string& title,
                               const std::string& y_label);

    // Bar chart — supports negative values (drawn downward from zero line)
    static void barChart(HPDF_Doc pdf, HPDF_Page page,
                         float x, float y, float w, float h,
                         const std::vector<double>& values,
                         const std::vector<std::string>& labels,
                         const std::string& title,
                         float r = 0.2f, float g = 0.6f, float b = 0.4f);

    // Correlation heat-map with color legend
    static void heatmap(HPDF_Doc pdf, HPDF_Page page,
                        float x, float y, float w, float h,
                        const std::vector<std::vector<double>>& matrix,
                        const std::vector<std::string>& labels,
                        const std::string& title);

    // Dual-axis: price line (top 67%) + volume bars (bottom 28%)
    static void priceVolume(HPDF_Doc pdf, HPDF_Page page,
                            float x, float y, float w, float h,
                            const std::vector<double>& prices,
                            const std::vector<double>& volumes,
                            const std::vector<std::string>& dates,
                            const std::string& title);

    // Forecast chart: historical line (gray) flowing into a colored forecast
    // line, with a shaded confidence band over the forecast region and a
    // vertical "now" divider. x_labels covers history + forecast points.
    static void forecastChart(HPDF_Doc pdf, HPDF_Page page,
                              float x, float y, float w, float h,
                              const std::vector<double>& history,
                              const std::vector<double>& forecast,
                              const std::vector<double>& lower,
                              const std::vector<double>& upper,
                              const std::vector<std::string>& x_labels,
                              const std::string& title,
                              const std::string& y_label,
                              float fr, float fg, float fb);

    // Comparison forecast chart: one shared history line (gray) with several
    // model forecasts overlaid in distinct colors, a "now" divider and a legend.
    // forecasts[i] is model i's horizon; colors come from SERIES_COLORS.
    static void multiForecastChart(HPDF_Doc pdf, HPDF_Page page,
                                   float x, float y, float w, float h,
                                   const std::vector<double>& history,
                                   const std::vector<std::vector<double>>& forecasts,
                                   const std::vector<std::string>& model_names,
                                   const std::vector<std::string>& x_labels,
                                   const std::string& title,
                                   const std::string& y_label);

    // Price line with anomalies marked as colored dots at their dates.
    // marker_kind: 0 = down/red (drop, MA-break-down), 1 = up/green
    // (spike, MA-break-up), 2 = volume/orange. A legend is drawn below.
    static void anomalyChart(HPDF_Doc pdf, HPDF_Page page,
                             float x, float y, float w, float h,
                             const std::vector<double>& prices,
                             const std::vector<std::string>& dates,
                             const std::vector<int>& marker_idx,
                             const std::vector<int>& marker_kind,
                             const std::string& title,
                             const std::string& y_label);

private:
    // Draws background, border, Y-axis ticks+grid. X labels are NOT drawn here.
    static void drawAxes(HPDF_Page page,
                         float cx, float cy, float cw, float ch,
                         double y_min, double y_max,
                         const std::string& y_label,
                         HPDF_Font font, int tick_count = 5);

    static std::string formatValue(double v);
    static std::string fmtNum(double v, int decimals = 2);

    // Series colors for multi-line chart
    static const float SERIES_COLORS[6][3];
};
