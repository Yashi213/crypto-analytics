#include "reports/PdfChart.h"
#include "utils/PdfText.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

const float PdfChart::SERIES_COLORS[6][3] = {
    {0.20f, 0.44f, 0.80f},   // blue
    {0.84f, 0.33f, 0.18f},   // orange-red
    {0.22f, 0.68f, 0.32f},   // green
    {0.68f, 0.48f, 0.10f},   // gold
    {0.58f, 0.22f, 0.70f},   // purple
    {0.18f, 0.60f, 0.68f},   // teal
};

// ─── formatting helpers ───────────────────────────────────────────────────────
std::string PdfChart::fmtNum(double v, int dec) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(dec) << v;
    return ss.str();
}

std::string PdfChart::formatValue(double v) {
    if (std::abs(v) >= 1e9)  return fmtNum(v / 1e9, 2) + "B";
    if (std::abs(v) >= 1e6)  return fmtNum(v / 1e6, 2) + "M";
    if (std::abs(v) >= 1e3)  return fmtNum(v / 1e3, 1) + "K";
    return fmtNum(v, 2);
}

// ─── drawAxes: background, border, Y ticks, grid only ────────────────────────
void PdfChart::drawAxes(HPDF_Page page,
                         float cx, float cy, float cw, float ch,
                         double y_min, double y_max,
                         const std::string& y_label,
                         HPDF_Font font, int tick_count) {
    // Background
    HPDF_Page_SetRGBFill(page, 0.97f, 0.97f, 0.97f);
    HPDF_Page_Rectangle(page, cx, cy, cw, ch);
    HPDF_Page_Fill(page);

    // Border
    HPDF_Page_SetRGBStroke(page, 0.65f, 0.65f, 0.65f);
    HPDF_Page_SetLineWidth(page, 0.6f);
    HPDF_Page_Rectangle(page, cx, cy, cw, ch);
    HPDF_Page_Stroke(page);

    double y_range = (y_max - y_min);
    if (std::abs(y_range) < 1e-12) y_range = 1.0;

    HPDF_Page_SetFontAndSize(page, font, 7.f);

    for (int i = 0; i <= tick_count; ++i) {
        double val  = y_min + y_range * i / tick_count;
        float  ypos = cy + ch * static_cast<float>(i) / tick_count;

        // Grid line
        HPDF_Page_SetRGBStroke(page, 0.86f, 0.86f, 0.86f);
        HPDF_Page_SetLineWidth(page, 0.3f);
        HPDF_Page_MoveTo(page, cx,       ypos);
        HPDF_Page_LineTo(page, cx + cw,  ypos);
        HPDF_Page_Stroke(page);

        // Y tick label (right-aligned in 42px left margin)
        std::string lbl = formatValue(val);
        float lx = cx - 4.f - static_cast<float>(lbl.size()) * 4.2f;
        HPDF_Page_SetRGBFill(page, 0.30f, 0.30f, 0.30f);
        HPDF_Page_BeginText(page);
        HPDF_Page_TextOut(page, lx, ypos - 3.f, lbl.c_str());
        HPDF_Page_EndText(page);
    }

    // Y-axis label above top-left corner of chart
    if (!y_label.empty()) {
        HPDF_Page_SetFontAndSize(page, font, 7.f);
        HPDF_Page_SetRGBFill(page, 0.45f, 0.45f, 0.45f);
        HPDF_Page_BeginText(page);
        HPDF_Page_TextOut(page, cx, cy + ch + 6.f, y_label.c_str());
        HPDF_Page_EndText(page);
    }
}

// ─── line chart ───────────────────────────────────────────────────────────────
void PdfChart::lineChart(HPDF_Doc pdf, HPDF_Page page,
                          float x, float y, float w, float h,
                          const std::vector<double>& values,
                          const std::vector<std::string>& x_labels,
                          const std::string& title,
                          const std::string& y_label,
                          float r, float g, float b,
                          bool overlay) {
    if (values.empty()) return;

    // Inner chart area: left margin=55 (Y labels), bottom=22 (X labels), top=28 (title)
    float cx = x + 55.f, cy = y + 22.f, cw = w - 65.f, ch = h - 38.f;
    if (cw < 10 || ch < 10) return;

    double y_min = *std::min_element(values.begin(), values.end());
    double y_max = *std::max_element(values.begin(), values.end());
    double margin = (y_max - y_min) * 0.06;
    y_min -= margin;
    y_max += margin;
    if (std::abs(y_max - y_min) < 1e-9) { y_min -= 1; y_max += 1; }
    double y_range = y_max - y_min;

    HPDF_Font font   = HPDF_GetFont(pdf, PDF_FONT_REG, PDF_ENC);
    HPDF_Font font_b = HPDF_GetFont(pdf, PDF_FONT_BOLD, PDF_ENC);

    if (!overlay) {
        drawAxes(page, cx, cy, cw, ch, y_min, y_max, y_label, font);

        // Title centred above chart
        if (!title.empty()) {
            HPDF_Page_SetFontAndSize(page, font_b, 9.f);
            HPDF_Page_SetRGBFill(page, 0.10f, 0.10f, 0.10f);
            HPDF_Page_BeginText(page);
            float tx = cx + cw / 2.f - static_cast<float>(title.size()) * 2.8f;
            HPDF_Page_TextOut(page, std::max(cx, tx), cy + ch + 17.f, title.c_str());
            HPDF_Page_EndText(page);
        }

        // X labels — at most 8, linearly spaced along data positions
        if (!x_labels.empty()) {
            int n_lab = static_cast<int>(x_labels.size());
            int stride = std::max(1, (n_lab - 1) / 7);
            HPDF_Page_SetFontAndSize(page, font, 6.5f);
            HPDF_Page_SetRGBFill(page, 0.30f, 0.30f, 0.30f);
            for (int i = 0; i < n_lab; i += stride) {
                float xpos = cx + cw * static_cast<float>(i) / std::max(1, n_lab - 1);
                HPDF_Page_BeginText(page);
                // Shift left ~14px so the label is roughly centred under the tick
                HPDF_Page_TextOut(page, xpos - 14.f, cy - 15.f, x_labels[i].c_str());
                HPDF_Page_EndText(page);
                // Small tick mark
                HPDF_Page_SetRGBStroke(page, 0.60f, 0.60f, 0.60f);
                HPDF_Page_SetLineWidth(page, 0.5f);
                HPDF_Page_MoveTo(page, xpos, cy);
                HPDF_Page_LineTo(page, xpos, cy - 4.f);
                HPDF_Page_Stroke(page);
            }
        }
    }

    int n = static_cast<int>(values.size());

    // Data line
    HPDF_Page_SetRGBStroke(page, r, g, b);
    HPDF_Page_SetLineWidth(page, 1.6f);
    for (int i = 0; i < n; ++i) {
        float px = cx + cw * static_cast<float>(i) / std::max(1, n - 1);
        float py = cy + static_cast<float>(ch * (values[i] - y_min) / y_range);
        if (i == 0) HPDF_Page_MoveTo(page, px, py);
        else        HPDF_Page_LineTo(page, px, py);
    }
    HPDF_Page_Stroke(page);

    // Dots only when data is sparse enough to be readable
    if (n <= 60) {
        HPDF_Page_SetRGBFill(page, r, g, b);
        float dot_r = (n <= 20) ? 2.5f : 1.8f;
        for (int i = 0; i < n; ++i) {
            float px = cx + cw * static_cast<float>(i) / std::max(1, n - 1);
            float py = cy + static_cast<float>(ch * (values[i] - y_min) / y_range);
            HPDF_Page_Circle(page, px, py, dot_r);
            HPDF_Page_Fill(page);
        }
    }
}

// ─── multi-series line chart ──────────────────────────────────────────────────
void PdfChart::multiLineChart(HPDF_Doc pdf, HPDF_Page page,
                               float x, float y, float w, float h,
                               const std::vector<std::vector<double>>& series,
                               const std::vector<std::string>& x_labels,
                               const std::vector<std::string>& series_names,
                               const std::string& title,
                               const std::string& y_label) {
    if (series.empty()) return;

    // Reserve bottom 18px for legend, top 28px for title
    float legend_h = 18.f;
    float cx = x + 55.f, cy = y + legend_h + 22.f;
    float cw = w - 65.f,  ch = h - legend_h - 38.f;
    if (cw < 10 || ch < 10) return;

    // Global Y range across all series
    double y_min =  1e18, y_max = -1e18;
    for (const auto& s : series) {
        if (s.empty()) continue;
        y_min = std::min(y_min, *std::min_element(s.begin(), s.end()));
        y_max = std::max(y_max, *std::max_element(s.begin(), s.end()));
    }
    double margin = (y_max - y_min) * 0.06;
    y_min -= margin; y_max += margin;
    if (std::abs(y_max - y_min) < 1e-9) { y_min -= 1; y_max += 1; }
    double y_range = y_max - y_min;

    HPDF_Font font   = HPDF_GetFont(pdf, PDF_FONT_REG, PDF_ENC);
    HPDF_Font font_b = HPDF_GetFont(pdf, PDF_FONT_BOLD, PDF_ENC);

    drawAxes(page, cx, cy, cw, ch, y_min, y_max, y_label, font);

    // Title
    if (!title.empty()) {
        HPDF_Page_SetFontAndSize(page, font_b, 9.f);
        HPDF_Page_SetRGBFill(page, 0.10f, 0.10f, 0.10f);
        HPDF_Page_BeginText(page);
        float tx = cx + cw / 2.f - static_cast<float>(title.size()) * 2.8f;
        HPDF_Page_TextOut(page, std::max(cx, tx), cy + ch + 17.f, title.c_str());
        HPDF_Page_EndText(page);
    }

    // X labels
    if (!x_labels.empty()) {
        int n_lab = static_cast<int>(x_labels.size());
        int stride = std::max(1, (n_lab - 1) / 7);
        HPDF_Page_SetFontAndSize(page, font, 6.5f);
        HPDF_Page_SetRGBFill(page, 0.30f, 0.30f, 0.30f);
        for (int i = 0; i < n_lab; i += stride) {
            float xpos = cx + cw * static_cast<float>(i) / std::max(1, n_lab - 1);
            HPDF_Page_BeginText(page);
            HPDF_Page_TextOut(page, xpos - 14.f, cy - 15.f, x_labels[i].c_str());
            HPDF_Page_EndText(page);
            HPDF_Page_SetRGBStroke(page, 0.60f, 0.60f, 0.60f);
            HPDF_Page_SetLineWidth(page, 0.5f);
            HPDF_Page_MoveTo(page, xpos, cy);
            HPDF_Page_LineTo(page, xpos, cy - 4.f);
            HPDF_Page_Stroke(page);
        }
    }

    // Draw each series
    for (size_t si = 0; si < series.size(); ++si) {
        const auto& s = series[si];
        if (s.empty()) continue;
        int n = static_cast<int>(s.size());
        float cr = SERIES_COLORS[si % 6][0];
        float cg = SERIES_COLORS[si % 6][1];
        float cb = SERIES_COLORS[si % 6][2];

        HPDF_Page_SetRGBStroke(page, cr, cg, cb);
        HPDF_Page_SetLineWidth(page, 1.6f);
        for (int i = 0; i < n; ++i) {
            float px = cx + cw * static_cast<float>(i) / std::max(1, n - 1);
            float py = cy + static_cast<float>(ch * (s[i] - y_min) / y_range);
            if (i == 0) HPDF_Page_MoveTo(page, px, py);
            else        HPDF_Page_LineTo(page, px, py);
        }
        HPDF_Page_Stroke(page);
    }

    // Legend (below chart)
    float lx = cx;
    float ly  = y + 4.f;
    HPDF_Page_SetFontAndSize(page, font, 7.f);
    for (size_t si = 0; si < series.size() && si < series_names.size(); ++si) {
        float cr = SERIES_COLORS[si % 6][0];
        float cg = SERIES_COLORS[si % 6][1];
        float cb_col = SERIES_COLORS[si % 6][2];
        // Color swatch
        HPDF_Page_SetRGBFill(page, cr, cg, cb_col);
        HPDF_Page_Rectangle(page, lx, ly, 12.f, 8.f);
        HPDF_Page_Fill(page);
        // Label
        HPDF_Page_SetRGBFill(page, 0.15f, 0.15f, 0.15f);
        HPDF_Page_BeginText(page);
        HPDF_Page_TextOut(page, lx + 14.f, ly + 1.f, series_names[si].c_str());
        HPDF_Page_EndText(page);
        lx += 14.f + static_cast<float>(series_names[si].size()) * 4.5f + 8.f;
    }
}

// ─── bar chart ────────────────────────────────────────────────────────────────
void PdfChart::barChart(HPDF_Doc pdf, HPDF_Page page,
                         float x, float y, float w, float h,
                         const std::vector<double>& values,
                         const std::vector<std::string>& labels,
                         const std::string& title,
                         float r, float g, float b) {
    if (values.empty()) return;

    float cx = x + 55.f, cy = y + 22.f, cw = w - 65.f, ch = h - 38.f;
    if (cw < 10 || ch < 10) return;

    double v_min = *std::min_element(values.begin(), values.end());
    double v_max = *std::max_element(values.begin(), values.end());

    // Always include 0 as the baseline
    double y_min = std::min(0.0, v_min * 1.12);
    double y_max = std::max(0.0, v_max * 1.12);
    if (std::abs(y_max - y_min) < 1e-9) { y_min -= 1; y_max += 1; }
    double y_range = y_max - y_min;

    HPDF_Font font   = HPDF_GetFont(pdf, PDF_FONT_REG, PDF_ENC);
    HPDF_Font font_b = HPDF_GetFont(pdf, PDF_FONT_BOLD, PDF_ENC);
    // Fewer Y ticks on short charts (e.g. the volume strip) to avoid crowding.
    int y_ticks = (ch < 90.f) ? 3 : 5;
    drawAxes(page, cx, cy, cw, ch, y_min, y_max, "", font, y_ticks);

    // Title centred above chart
    if (!title.empty()) {
        HPDF_Page_SetFontAndSize(page, font_b, 9.f);
        HPDF_Page_SetRGBFill(page, 0.10f, 0.10f, 0.10f);
        HPDF_Page_BeginText(page);
        float tx = cx + cw / 2.f - static_cast<float>(title.size()) * 2.8f;
        HPDF_Page_TextOut(page, std::max(cx, tx), cy + ch + 17.f, title.c_str());
        HPDF_Page_EndText(page);
    }

    int n = static_cast<int>(values.size());
    float slot  = cw / static_cast<float>(n);
    float bar_w = slot * 0.60f;
    float pad   = slot * 0.20f;

    // Density gating: with many bars (e.g. 365 daily volumes) per-bar value
    // labels and x-labels overlap into an unreadable smear. Show value labels
    // only for a small number of bars, and thin out x-labels to ~14 max.
    bool show_vals  = (n <= 14);
    int  lab_stride = std::max(1, n / 14);

    // Zero line
    float zero_y = cy + static_cast<float>(ch * (0.0 - y_min) / y_range);
    HPDF_Page_SetRGBStroke(page, 0.45f, 0.45f, 0.45f);
    HPDF_Page_SetLineWidth(page, 0.7f);
    HPDF_Page_MoveTo(page, cx,        zero_y);
    HPDF_Page_LineTo(page, cx + cw,   zero_y);
    HPDF_Page_Stroke(page);

    HPDF_Page_SetFontAndSize(page, font, 6.5f);

    for (int i = 0; i < n; ++i) {
        float bx    = cx + slot * static_cast<float>(i) + pad;
        float val_y = cy + static_cast<float>(ch * (values[i] - y_min) / y_range);
        float by    = std::min(zero_y, val_y);
        float bh    = std::abs(val_y - zero_y);
        if (bh < 0.5f) bh = 0.5f;

        bool neg = values[i] < 0;
        float fr = neg ? 0.82f : r;
        float fg = neg ? 0.22f : g;
        float fb = neg ? 0.22f : b;

        HPDF_Page_SetRGBFill(page, fr, fg, fb);
        HPDF_Page_Rectangle(page, bx, by, bar_w, bh);
        HPDF_Page_Fill(page);
        HPDF_Page_SetRGBStroke(page, fr * 0.72f, fg * 0.72f, fb * 0.72f);
        HPDF_Page_SetLineWidth(page, 0.4f);
        HPDF_Page_Rectangle(page, bx, by, bar_w, bh);
        HPDF_Page_Stroke(page);

        // Value label above/below bar (only when bars are few enough to read)
        if (show_vals) {
            std::string vlbl = fmtNum(values[i], 1);
            float vlbl_x = bx + bar_w / 2.f - static_cast<float>(vlbl.size()) * 2.2f;
            float vlbl_y = neg ? (by - 10.f) : (by + bh + 2.f);
            HPDF_Page_SetRGBFill(page, 0.15f, 0.15f, 0.15f);
            HPDF_Page_BeginText(page);
            HPDF_Page_TextOut(page, vlbl_x, vlbl_y, vlbl.c_str());
            HPDF_Page_EndText(page);
        }

        // X label centred under bar slot (thinned out to avoid overlap)
        if (i < static_cast<int>(labels.size()) && (i % lab_stride == 0)) {
            float lx2 = bx + bar_w / 2.f - static_cast<float>(labels[i].size()) * 2.2f;
            HPDF_Page_SetRGBFill(page, 0.30f, 0.30f, 0.30f);
            HPDF_Page_BeginText(page);
            HPDF_Page_TextOut(page, lx2, cy - 14.f, labels[i].c_str());
            HPDF_Page_EndText(page);
        }
    }
}

// ─── correlation heat-map ─────────────────────────────────────────────────────
void PdfChart::heatmap(HPDF_Doc pdf, HPDF_Page page,
                        float x, float y, float w, float h,
                        const std::vector<std::vector<double>>& matrix,
                        const std::vector<std::string>& labels,
                        const std::string& title) {
    if (matrix.empty()) return;
    int n = static_cast<int>(matrix.size());

    // Layout: left margin for row labels, top margin for title+col labels
    float col_label_h = 16.f;
    float row_label_w = 36.f;
    float legend_w    = 28.f;   // color bar + text
    float usable_w    = w - row_label_w - legend_w;
    float usable_h    = h - col_label_h - 14.f; // 14 for title
    float cell = std::min(usable_w / n, usable_h / n);
    cell = std::max(cell, 8.f);

    float grid_w = cell * n;
    float grid_h = cell * n;
    float ox = x + row_label_w + (usable_w - grid_w) / 2.f;
    float oy = y + (usable_h - grid_h) / 2.f;

    HPDF_Font font   = HPDF_GetFont(pdf, PDF_FONT_REG, PDF_ENC);
    HPDF_Font font_b = HPDF_GetFont(pdf, PDF_FONT_BOLD, PDF_ENC);

    // Title
    HPDF_Page_SetFontAndSize(page, font_b, 9.f);
    HPDF_Page_SetRGBFill(page, 0.10f, 0.10f, 0.10f);
    HPDF_Page_BeginText(page);
    float tx = ox + grid_w / 2.f - static_cast<float>(title.size()) * 2.8f;
    HPDF_Page_TextOut(page, std::max(x, tx), oy + grid_h + col_label_h + 2.f, title.c_str());
    HPDF_Page_EndText(page);

    HPDF_Page_SetFontAndSize(page, font, 7.f);
    for (int i = 0; i < n; ++i) {
        // Row label (left, vertically centred in row)
        HPDF_Page_SetRGBFill(page, 0.20f, 0.20f, 0.20f);
        HPDF_Page_BeginText(page);
        HPDF_Page_TextOut(page, x,
            oy + (n - 1 - i) * cell + cell / 2.f - 3.f,
            labels[i].c_str());
        HPDF_Page_EndText(page);

        // Col label (top, centred in column)
        HPDF_Page_BeginText(page);
        float lx = ox + i * cell + cell / 2.f
                   - static_cast<float>(labels[i].size()) * 2.1f;
        HPDF_Page_TextOut(page, lx, oy + grid_h + 3.f, labels[i].c_str());
        HPDF_Page_EndText(page);

        for (int j = 0; j < n; ++j) {
            double val = matrix[i][j];
            float cr, cg, cb;
            if (val >= 0) {
                cr = 1.0f - 0.80f * static_cast<float>(val);
                cg = 1.0f - 0.55f * static_cast<float>(val);
                cb = 1.0f;
            } else {
                cr = 1.0f;
                cg = 1.0f + 0.75f * static_cast<float>(val);
                cb = 1.0f + 0.75f * static_cast<float>(val);
            }
            cr = std::max(0.f, std::min(1.f, cr));
            cg = std::max(0.f, std::min(1.f, cg));
            cb = std::max(0.f, std::min(1.f, cb));

            float cx2 = ox + j * cell;
            float cy2 = oy + (n - 1 - i) * cell;

            HPDF_Page_SetRGBFill(page, cr, cg, cb);
            HPDF_Page_Rectangle(page, cx2, cy2, cell - 1.f, cell - 1.f);
            HPDF_Page_Fill(page);
            HPDF_Page_SetRGBStroke(page, 0.78f, 0.78f, 0.78f);
            HPDF_Page_SetLineWidth(page, 0.3f);
            HPDF_Page_Rectangle(page, cx2, cy2, cell - 1.f, cell - 1.f);
            HPDF_Page_Stroke(page);

            // Value text centred in cell
            HPDF_Page_SetFontAndSize(page, font, std::max(5.5f, cell * 0.25f));
            HPDF_Page_SetRGBFill(page, 0.10f, 0.10f, 0.10f);
            HPDF_Page_BeginText(page);
            std::string vstr = fmtNum(val, 2);
            HPDF_Page_TextOut(page,
                cx2 + cell / 2.f - static_cast<float>(vstr.size()) * 2.0f,
                cy2 + cell / 2.f - 3.f,
                vstr.c_str());
            HPDF_Page_EndText(page);
        }
    }

    // Color legend bar (right of grid)
    float leg_x  = ox + grid_w + 8.f;
    float leg_y  = oy;
    float leg_h2 = grid_h;
    float leg_w2 = 10.f;
    int   steps  = 40;
    for (int s = 0; s < steps; ++s) {
        float t = static_cast<float>(s) / steps;
        double v = -1.0 + 2.0 * t;
        float cr2, cg2, cb2;
        if (v >= 0) {
            cr2 = 1.0f - 0.80f * static_cast<float>(v);
            cg2 = 1.0f - 0.55f * static_cast<float>(v);
            cb2 = 1.0f;
        } else {
            cr2 = 1.0f;
            cg2 = 1.0f + 0.75f * static_cast<float>(v);
            cb2 = 1.0f + 0.75f * static_cast<float>(v);
        }
        cr2 = std::max(0.f, std::min(1.f, cr2));
        cg2 = std::max(0.f, std::min(1.f, cg2));
        cb2 = std::max(0.f, std::min(1.f, cb2));
        HPDF_Page_SetRGBFill(page, cr2, cg2, cb2);
        float slice = leg_h2 / steps;
        HPDF_Page_Rectangle(page, leg_x, leg_y + t * leg_h2, leg_w2, slice + 0.6f);
        HPDF_Page_Fill(page);
    }
    HPDF_Page_SetRGBStroke(page, 0.55f, 0.55f, 0.55f);
    HPDF_Page_SetLineWidth(page, 0.5f);
    HPDF_Page_Rectangle(page, leg_x, leg_y, leg_w2, leg_h2);
    HPDF_Page_Stroke(page);

    HPDF_Page_SetFontAndSize(page, font, 6.5f);
    HPDF_Page_SetRGBFill(page, 0.25f, 0.25f, 0.25f);
    for (auto [label_txt, frac] : std::initializer_list<std::pair<const char*, float>>{
            {"+1", 1.f}, {" 0", 0.5f}, {"-1", 0.f}}) {
        HPDF_Page_BeginText(page);
        HPDF_Page_TextOut(page, leg_x + leg_w2 + 2.f,
            leg_y + frac * leg_h2 - 3.f, label_txt);
        HPDF_Page_EndText(page);
    }
}

// ─── forecast chart: history + forecast + confidence band ──────────────────────
void PdfChart::forecastChart(HPDF_Doc pdf, HPDF_Page page,
                             float x, float y, float w, float h,
                             const std::vector<double>& history,
                             const std::vector<double>& forecast,
                             const std::vector<double>& lower,
                             const std::vector<double>& upper,
                             const std::vector<std::string>& x_labels,
                             const std::string& title,
                             const std::string& y_label,
                             float fr, float fg, float fb) {
    int H = static_cast<int>(history.size());
    int F = static_cast<int>(forecast.size());
    if (H == 0 || F == 0) return;
    int total = H + F;

    float cx = x + 55.f, cy = y + 22.f, cw = w - 65.f, ch = h - 38.f;
    if (cw < 10 || ch < 10) return;

    // Y range over everything that gets drawn (history + band)
    double y_min = 1e18, y_max = -1e18;
    auto upd = [&](double v) { y_min = std::min(y_min, v); y_max = std::max(y_max, v); };
    for (double v : history)  upd(v);
    for (double v : forecast) upd(v);
    for (double v : lower)    upd(v);
    for (double v : upper)    upd(v);
    double margin = (y_max - y_min) * 0.08;
    y_min -= margin; y_max += margin;
    if (std::abs(y_max - y_min) < 1e-9) { y_min -= 1; y_max += 1; }
    double y_range = y_max - y_min;

    HPDF_Font font   = HPDF_GetFont(pdf, PDF_FONT_REG, PDF_ENC);
    HPDF_Font font_b = HPDF_GetFont(pdf, PDF_FONT_BOLD, PDF_ENC);

    drawAxes(page, cx, cy, cw, ch, y_min, y_max, y_label, font);

    if (!title.empty()) {
        HPDF_Page_SetFontAndSize(page, font_b, 9.f);
        HPDF_Page_SetRGBFill(page, 0.10f, 0.10f, 0.10f);
        HPDF_Page_BeginText(page);
        float tx = cx + cw / 2.f - static_cast<float>(title.size()) * 2.8f;
        HPDF_Page_TextOut(page, std::max(cx, tx), cy + ch + 17.f, title.c_str());
        HPDF_Page_EndText(page);
    }

    auto xpos = [&](int idx) {
        return cx + cw * static_cast<float>(idx) / std::max(1, total - 1);
    };
    auto ypos = [&](double v) {
        return cy + static_cast<float>(ch * (v - y_min) / y_range);
    };

    // X labels (thinned to ~8)
    if (!x_labels.empty()) {
        int n_lab  = static_cast<int>(x_labels.size());
        int stride = std::max(1, (n_lab - 1) / 7);
        HPDF_Page_SetFontAndSize(page, font, 6.5f);
        HPDF_Page_SetRGBFill(page, 0.30f, 0.30f, 0.30f);
        for (int i = 0; i < n_lab; i += stride) {
            float px = xpos(i);
            HPDF_Page_BeginText(page);
            HPDF_Page_TextOut(page, px - 14.f, cy - 15.f, x_labels[i].c_str());
            HPDF_Page_EndText(page);
        }
    }

    // Confidence band over forecast region (light tint of the forecast color),
    // anchored at the last historical point so the band emerges from "now".
    if (F == static_cast<int>(lower.size()) && F == static_cast<int>(upper.size())) {
        float tr = fr + (1.f - fr) * 0.78f;
        float tg = fg + (1.f - fg) * 0.78f;
        float tb = fb + (1.f - fb) * 0.78f;
        HPDF_Page_SetRGBFill(page, tr, tg, tb);
        // upper edge left→right
        HPDF_Page_MoveTo(page, xpos(H - 1), ypos(history.back()));
        for (int i = 0; i < F; ++i) HPDF_Page_LineTo(page, xpos(H + i), ypos(upper[i]));
        // lower edge right→left
        for (int i = F - 1; i >= 0; --i) HPDF_Page_LineTo(page, xpos(H + i), ypos(lower[i]));
        HPDF_Page_ClosePath(page);
        HPDF_Page_Fill(page);
    }

    // Vertical "now" divider — drawn as short dashes (version-agnostic, no SetDash)
    {
        float dx = xpos(H - 1);
        HPDF_Page_SetRGBStroke(page, 0.55f, 0.55f, 0.55f);
        HPDF_Page_SetLineWidth(page, 0.7f);
        for (float yy = cy; yy < cy + ch; yy += 5.f) {
            HPDF_Page_MoveTo(page, dx, yy);
            HPDF_Page_LineTo(page, dx, std::min(yy + 2.5f, cy + ch));
            HPDF_Page_Stroke(page);
        }
    }

    // History line (muted gray-blue)
    HPDF_Page_SetRGBStroke(page, 0.40f, 0.45f, 0.55f);
    HPDF_Page_SetLineWidth(page, 1.4f);
    for (int i = 0; i < H; ++i) {
        float px = xpos(i), py = ypos(history[i]);
        if (i == 0) HPDF_Page_MoveTo(page, px, py);
        else        HPDF_Page_LineTo(page, px, py);
    }
    HPDF_Page_Stroke(page);

    // Forecast line (model color), starting from the last historical point
    HPDF_Page_SetRGBStroke(page, fr, fg, fb);
    HPDF_Page_SetLineWidth(page, 1.8f);
    HPDF_Page_MoveTo(page, xpos(H - 1), ypos(history.back()));
    for (int i = 0; i < F; ++i) HPDF_Page_LineTo(page, xpos(H + i), ypos(forecast[i]));
    HPDF_Page_Stroke(page);

    // Forecast dots
    HPDF_Page_SetRGBFill(page, fr, fg, fb);
    for (int i = 0; i < F; ++i) {
        HPDF_Page_Circle(page, xpos(H + i), ypos(forecast[i]), 2.2f);
        HPDF_Page_Fill(page);
    }

    // Mini legend (top-left inside chart)
    float lgx = cx + 6.f, lgy = cy + ch - 12.f;
    HPDF_Page_SetFontAndSize(page, font, 7.f);
    HPDF_Page_SetRGBStroke(page, 0.40f, 0.45f, 0.55f);
    HPDF_Page_SetLineWidth(page, 1.4f);
    HPDF_Page_MoveTo(page, lgx, lgy + 3.f);
    HPDF_Page_LineTo(page, lgx + 14.f, lgy + 3.f);
    HPDF_Page_Stroke(page);
    HPDF_Page_SetRGBFill(page, 0.20f, 0.20f, 0.20f);
    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, lgx + 18.f, lgy, ru("история").c_str());
    HPDF_Page_EndText(page);
    HPDF_Page_SetRGBStroke(page, fr, fg, fb);
    HPDF_Page_SetLineWidth(page, 1.8f);
    HPDF_Page_MoveTo(page, lgx + 70.f, lgy + 3.f);
    HPDF_Page_LineTo(page, lgx + 84.f, lgy + 3.f);
    HPDF_Page_Stroke(page);
    HPDF_Page_SetRGBFill(page, 0.20f, 0.20f, 0.20f);
    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, lgx + 88.f, lgy, ru("прогноз").c_str());
    HPDF_Page_EndText(page);
}

// ─── comparison forecast chart: history + several model forecasts ───────────────
void PdfChart::multiForecastChart(HPDF_Doc pdf, HPDF_Page page,
                                  float x, float y, float w, float h,
                                  const std::vector<double>& history,
                                  const std::vector<std::vector<double>>& forecasts,
                                  const std::vector<std::string>& model_names,
                                  const std::vector<std::string>& x_labels,
                                  const std::string& title,
                                  const std::string& y_label) {
    int H = static_cast<int>(history.size());
    if (H == 0 || forecasts.empty()) return;
    int F = 0;
    for (const auto& f : forecasts) F = std::max(F, static_cast<int>(f.size()));
    if (F == 0) return;
    int total = H + F;

    // Reserve bottom 18px for legend
    float legend_h = 18.f;
    float cx = x + 55.f, cy = y + legend_h + 22.f;
    float cw = w - 65.f,  ch = h - legend_h - 38.f;
    if (cw < 10 || ch < 10) return;

    // Y range over history + all forecasts
    double y_min = 1e18, y_max = -1e18;
    auto upd = [&](double v) { y_min = std::min(y_min, v); y_max = std::max(y_max, v); };
    for (double v : history) upd(v);
    for (const auto& f : forecasts) for (double v : f) upd(v);
    double margin = (y_max - y_min) * 0.08;
    y_min -= margin; y_max += margin;
    if (std::abs(y_max - y_min) < 1e-9) { y_min -= 1; y_max += 1; }
    double y_range = y_max - y_min;

    HPDF_Font font   = HPDF_GetFont(pdf, PDF_FONT_REG, PDF_ENC);
    HPDF_Font font_b = HPDF_GetFont(pdf, PDF_FONT_BOLD, PDF_ENC);

    drawAxes(page, cx, cy, cw, ch, y_min, y_max, y_label, font);

    if (!title.empty()) {
        HPDF_Page_SetFontAndSize(page, font_b, 9.f);
        HPDF_Page_SetRGBFill(page, 0.10f, 0.10f, 0.10f);
        HPDF_Page_BeginText(page);
        float tx = cx + cw / 2.f - static_cast<float>(title.size()) * 2.8f;
        HPDF_Page_TextOut(page, std::max(cx, tx), cy + ch + 17.f, title.c_str());
        HPDF_Page_EndText(page);
    }

    auto xpos = [&](int idx) {
        return cx + cw * static_cast<float>(idx) / std::max(1, total - 1);
    };
    auto ypos = [&](double v) {
        return cy + static_cast<float>(ch * (v - y_min) / y_range);
    };

    // X labels (thinned to ~8)
    if (!x_labels.empty()) {
        int n_lab  = static_cast<int>(x_labels.size());
        int stride = std::max(1, (n_lab - 1) / 7);
        HPDF_Page_SetFontAndSize(page, font, 6.5f);
        HPDF_Page_SetRGBFill(page, 0.30f, 0.30f, 0.30f);
        for (int i = 0; i < n_lab; i += stride) {
            float px = xpos(i);
            HPDF_Page_BeginText(page);
            HPDF_Page_TextOut(page, px - 14.f, cy - 15.f, x_labels[i].c_str());
            HPDF_Page_EndText(page);
        }
    }

    // "Now" divider (manual dashes)
    {
        float dx = xpos(H - 1);
        HPDF_Page_SetRGBStroke(page, 0.55f, 0.55f, 0.55f);
        HPDF_Page_SetLineWidth(page, 0.7f);
        for (float yy = cy; yy < cy + ch; yy += 5.f) {
            HPDF_Page_MoveTo(page, dx, yy);
            HPDF_Page_LineTo(page, dx, std::min(yy + 2.5f, cy + ch));
            HPDF_Page_Stroke(page);
        }
    }

    // History line (muted gray-blue)
    HPDF_Page_SetRGBStroke(page, 0.40f, 0.45f, 0.55f);
    HPDF_Page_SetLineWidth(page, 1.4f);
    for (int i = 0; i < H; ++i) {
        float px = xpos(i), py = ypos(history[i]);
        if (i == 0) HPDF_Page_MoveTo(page, px, py);
        else        HPDF_Page_LineTo(page, px, py);
    }
    HPDF_Page_Stroke(page);

    // Each model's forecast, starting from the last historical point
    for (size_t si = 0; si < forecasts.size(); ++si) {
        const auto& f = forecasts[si];
        if (f.empty()) continue;
        float cr = SERIES_COLORS[si % 6][0];
        float cg = SERIES_COLORS[si % 6][1];
        float cb = SERIES_COLORS[si % 6][2];
        HPDF_Page_SetRGBStroke(page, cr, cg, cb);
        HPDF_Page_SetLineWidth(page, 1.8f);
        HPDF_Page_MoveTo(page, xpos(H - 1), ypos(history.back()));
        for (int i = 0; i < static_cast<int>(f.size()); ++i)
            HPDF_Page_LineTo(page, xpos(H + i), ypos(f[i]));
        HPDF_Page_Stroke(page);

        HPDF_Page_SetRGBFill(page, cr, cg, cb);
        for (int i = 0; i < static_cast<int>(f.size()); ++i) {
            HPDF_Page_Circle(page, xpos(H + i), ypos(f[i]), 2.0f);
            HPDF_Page_Fill(page);
        }
    }

    // Legend: history + each model
    float lx = cx, ly = y + 4.f;
    HPDF_Page_SetFontAndSize(page, font, 7.f);
    // history swatch
    HPDF_Page_SetRGBFill(page, 0.40f, 0.45f, 0.55f);
    HPDF_Page_Rectangle(page, lx, ly, 12.f, 8.f);
    HPDF_Page_Fill(page);
    HPDF_Page_SetRGBFill(page, 0.15f, 0.15f, 0.15f);
    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, lx + 14.f, ly + 1.f, ru("история").c_str());
    HPDF_Page_EndText(page);
    lx += 14.f + 8.f * 4.5f + 8.f;
    for (size_t si = 0; si < forecasts.size() && si < model_names.size(); ++si) {
        float cr = SERIES_COLORS[si % 6][0];
        float cg = SERIES_COLORS[si % 6][1];
        float cb = SERIES_COLORS[si % 6][2];
        HPDF_Page_SetRGBFill(page, cr, cg, cb);
        HPDF_Page_Rectangle(page, lx, ly, 12.f, 8.f);
        HPDF_Page_Fill(page);
        HPDF_Page_SetRGBFill(page, 0.15f, 0.15f, 0.15f);
        HPDF_Page_BeginText(page);
        HPDF_Page_TextOut(page, lx + 14.f, ly + 1.f, model_names[si].c_str());
        HPDF_Page_EndText(page);
        lx += 14.f + static_cast<float>(model_names[si].size()) * 4.5f + 8.f;
    }
}

// ─── price line with anomaly markers ───────────────────────────────────────────
void PdfChart::anomalyChart(HPDF_Doc pdf, HPDF_Page page,
                            float x, float y, float w, float h,
                            const std::vector<double>& prices,
                            const std::vector<std::string>& dates,
                            const std::vector<int>& marker_idx,
                            const std::vector<int>& marker_kind,
                            const std::string& title,
                            const std::string& y_label) {
    if (prices.empty()) return;

    float legend_h = 16.f;
    float cx = x + 55.f, cy = y + legend_h + 22.f;
    float cw = w - 65.f,  ch = h - legend_h - 38.f;
    if (cw < 10 || ch < 10) return;

    double y_min = *std::min_element(prices.begin(), prices.end());
    double y_max = *std::max_element(prices.begin(), prices.end());
    double margin = (y_max - y_min) * 0.08;
    y_min -= margin; y_max += margin;
    if (std::abs(y_max - y_min) < 1e-9) { y_min -= 1; y_max += 1; }
    double y_range = y_max - y_min;

    HPDF_Font font   = HPDF_GetFont(pdf, PDF_FONT_REG, PDF_ENC);
    HPDF_Font font_b = HPDF_GetFont(pdf, PDF_FONT_BOLD, PDF_ENC);

    drawAxes(page, cx, cy, cw, ch, y_min, y_max, y_label, font);

    if (!title.empty()) {
        HPDF_Page_SetFontAndSize(page, font_b, 9.f);
        HPDF_Page_SetRGBFill(page, 0.10f, 0.10f, 0.10f);
        HPDF_Page_BeginText(page);
        float tx = cx + cw / 2.f - static_cast<float>(title.size()) * 2.8f;
        HPDF_Page_TextOut(page, std::max(cx, tx), cy + ch + 17.f, title.c_str());
        HPDF_Page_EndText(page);
    }

    int n = static_cast<int>(prices.size());
    auto xpos = [&](int i) { return cx + cw * static_cast<float>(i) / std::max(1, n - 1); };
    auto ypos = [&](double v) { return cy + static_cast<float>(ch * (v - y_min) / y_range); };

    // X labels (thinned to ~8)
    if (!dates.empty()) {
        int stride = std::max(1, (n - 1) / 7);
        HPDF_Page_SetFontAndSize(page, font, 6.5f);
        HPDF_Page_SetRGBFill(page, 0.30f, 0.30f, 0.30f);
        for (int i = 0; i < n; i += stride) {
            HPDF_Page_BeginText(page);
            HPDF_Page_TextOut(page, xpos(i) - 14.f, cy - 15.f, dates[i].c_str());
            HPDF_Page_EndText(page);
        }
    }

    // Price line (muted blue)
    HPDF_Page_SetRGBStroke(page, 0.30f, 0.45f, 0.70f);
    HPDF_Page_SetLineWidth(page, 1.3f);
    for (int i = 0; i < n; ++i) {
        if (i == 0) HPDF_Page_MoveTo(page, xpos(i), ypos(prices[i]));
        else        HPDF_Page_LineTo(page, xpos(i), ypos(prices[i]));
    }
    HPDF_Page_Stroke(page);

    // Marker colors by kind: 0 red (down), 1 green (up), 2 orange (volume)
    const float mc[3][3] = {
        {0.85f, 0.20f, 0.20f},
        {0.18f, 0.65f, 0.30f},
        {0.95f, 0.60f, 0.10f},
    };
    for (size_t k = 0; k < marker_idx.size(); ++k) {
        int idx = marker_idx[k];
        if (idx < 0 || idx >= n) continue;
        int kind = (k < marker_kind.size()) ? marker_kind[k] : 0;
        if (kind < 0 || kind > 2) kind = 0;
        float px = xpos(idx), py = ypos(prices[idx]);
        HPDF_Page_SetRGBFill(page, mc[kind][0], mc[kind][1], mc[kind][2]);
        HPDF_Page_Circle(page, px, py, 3.0f);
        HPDF_Page_Fill(page);
        HPDF_Page_SetRGBStroke(page, 1.f, 1.f, 1.f);
        HPDF_Page_SetLineWidth(page, 0.5f);
        HPDF_Page_Circle(page, px, py, 3.0f);
        HPDF_Page_Stroke(page);
    }

    // Legend
    struct Lg { const char* txt; int kind; };
    const Lg items[3] = {
        {"падение / пробой вниз", 0},
        {"рост / пробой вверх",   1},
        {"всплеск объёма",        2},
    };
    float lx = cx, ly = y + 2.f;
    HPDF_Page_SetFontAndSize(page, font, 7.f);
    for (const auto& it : items) {
        HPDF_Page_SetRGBFill(page, mc[it.kind][0], mc[it.kind][1], mc[it.kind][2]);
        HPDF_Page_Circle(page, lx + 4.f, ly + 4.f, 3.0f);
        HPDF_Page_Fill(page);
        HPDF_Page_SetRGBFill(page, 0.15f, 0.15f, 0.15f);
        HPDF_Page_BeginText(page);
        std::string t = ru(it.txt);
        HPDF_Page_TextOut(page, lx + 11.f, ly + 1.f, t.c_str());
        HPDF_Page_EndText(page);
        lx += 11.f + static_cast<float>(t.size()) * 4.3f + 12.f;
    }
}

// ─── price + volume dual chart ────────────────────────────────────────────────
void PdfChart::priceVolume(HPDF_Doc pdf, HPDF_Page page,
                            float x, float y, float w, float h,
                            const std::vector<double>& prices,
                            const std::vector<double>& volumes,
                            const std::vector<std::string>& dates,
                            const std::string& title) {
    float vol_h   = h * 0.27f;
    float gap     = h * 0.04f;
    float price_h = h - vol_h - gap;
    float price_y = y + vol_h + gap;

    lineChart(pdf, page, x, price_y, w, price_h,
              prices, dates, title + ru("  —  Цена (USDT)"), ru("Цена, USDT"),
              0.20f, 0.42f, 0.80f);
    barChart(pdf, page, x, y, w, vol_h,
             volumes, {}, ru("Объём торгов (USDT)"), 0.55f, 0.75f, 0.55f);
}
