#include "reports/ReportGenerator.h"
#include "reports/PdfChart.h"
#include "utils/PdfText.h"
#include "utils/Logger.h"
#include <hpdf.h>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <map>

namespace fs = std::filesystem;

// ─── file-local helpers ───────────────────────────────────────────────────────
namespace {

// Light-gray explanatory note (one line). Keep each call short enough to fit
// the page width — PDF text does not wrap automatically.
void note(HPDF_Doc pdf, HPDF_Page page, const std::string& text, float& y) {
    y -= 11.f;
    HPDF_Page_SetFontAndSize(page, HPDF_GetFont(pdf, PDF_FONT_REG, PDF_ENC), 7.5f);
    HPDF_Page_SetRGBFill(page, 0.42f, 0.42f, 0.42f);
    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, 50, y, ru(text).c_str());
    HPDF_Page_EndText(page);
}

// Human-readable Russian label for a stored anomaly type code.
std::string anomalyTypeRu(const std::string& code) {
    if (code == "price_spike")      return "скачок цены";
    if (code == "price_drop")       return "падение цены";
    if (code == "volume_spike")     return "всплеск объёма";
    if (code == "ma_breakout_up")   return "пробой СС вверх";
    if (code == "ma_breakout_down") return "пробой СС вниз";
    return code;
}

} // namespace

// ─── HPDF error handler ───────────────────────────────────────────────────────
void ReportGenerator::hpdfErrorHandler(HPDF_STATUS error_no,
                                        HPDF_STATUS detail_no,
                                        void*) {
    std::ostringstream ss;
    ss << "HPDF error " << std::hex << error_no
       << " (detail " << detail_no << ")";
    throw std::runtime_error(ss.str());
}

// ─── constructor ──────────────────────────────────────────────────────────────
ReportGenerator::ReportGenerator(DatabaseManager&     db,
                                  StatisticsAnalyzer&  stats,
                                  VolatilityAnalyzer&  vol,
                                  CorrelationAnalyzer& corr,
                                  AnomalyDetector&     anom,
                                  Forecaster&          fcast,
                                  const std::string&   output_dir)
    : db_(db), stats_(stats), vol_(vol), corr_(corr),
      anom_(anom), fcast_(fcast), output_dir_(output_dir) {
    // Each program run gets its own subfolder: <reports_dir>/run_YYYYmmdd_HHMMSS/
    output_dir_ = output_dir + "/run_" + timestamp();
    fs::create_directories(output_dir_);
    LOG_INFO("Reports folder for this run: " + output_dir_);
}

// ─── helpers ──────────────────────────────────────────────────────────────────
std::string ReportGenerator::timestamp() {
    auto t  = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return ss.str();
}

std::string ReportGenerator::fmt(double v, int dec) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(dec) << v;
    return ss.str();
}

std::string ReportGenerator::pctFmt(double v) {
    return fmt(v, 2) + "%";
}

std::string ReportGenerator::dateFmt(std::time_t t) {
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d");
    return ss.str();
}

std::string ReportGenerator::outputPath(const std::string& name) const {
    // The run folder is already unique, so file names stay clean (no timestamp).
    return output_dir_ + "/" + name + ".pdf";
}

// ─── PDF layout helpers (auto-convert text to CP1251) ─────────────────────────
HPDF_Page ReportGenerator::newPage(HPDF_Doc pdf) {
    HPDF_Page p = HPDF_AddPage(pdf);
    HPDF_Page_SetSize(p, HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT);
    return p;
}

void ReportGenerator::addTitle(HPDF_Doc pdf, HPDF_Page page,
                                const std::string& text, float y) {
    HPDF_Font font = HPDF_GetFont(pdf, PDF_FONT_BOLD, PDF_ENC);
    HPDF_Page_SetFontAndSize(page, font, 18.f);
    HPDF_Page_SetRGBFill(page, 0.1f, 0.2f, 0.5f);
    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, 50, y, ru(text).c_str());
    HPDF_Page_EndText(page);

    // Underline
    HPDF_Page_SetRGBStroke(page, 0.1f, 0.2f, 0.5f);
    HPDF_Page_SetLineWidth(page, 1.5f);
    HPDF_Page_MoveTo(page, 50,      y - 4);
    HPDF_Page_LineTo(page, 545,     y - 4);
    HPDF_Page_Stroke(page);
}

void ReportGenerator::addSection(HPDF_Doc pdf, HPDF_Page page,
                                  const std::string& text, float& y) {
    y -= 18;
    HPDF_Font font = HPDF_GetFont(pdf, PDF_FONT_BOLD, PDF_ENC);
    HPDF_Page_SetFontAndSize(page, font, 12.f);
    HPDF_Page_SetRGBFill(page, 0.15f, 0.35f, 0.6f);
    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, 50, y, ru(text).c_str());
    HPDF_Page_EndText(page);
    y -= 4;
}

void ReportGenerator::addText(HPDF_Doc pdf, HPDF_Page page,
                               const std::string& text,
                               float x, float& y, float font_size) {
    y -= font_size + 2;
    HPDF_Page_SetFontAndSize(page, HPDF_GetFont(pdf, PDF_FONT_REG, PDF_ENC),
                              font_size);
    HPDF_Page_SetRGBFill(page, 0.15f, 0.15f, 0.15f);
    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, x, y, ru(text).c_str());
    HPDF_Page_EndText(page);
}

void ReportGenerator::addKeyValue(HPDF_Doc pdf, HPDF_Page page,
                                   const std::string& key,
                                   const std::string& val,
                                   float x, float& y) {
    y -= 13;
    HPDF_Page_SetFontAndSize(page, HPDF_GetFont(pdf, PDF_FONT_BOLD, PDF_ENC), 9.f);
    HPDF_Page_SetRGBFill(page, 0.25f, 0.25f, 0.25f);
    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, x, y, ru(key + ":").c_str());
    HPDF_Page_EndText(page);
    HPDF_Page_SetFontAndSize(page, HPDF_GetFont(pdf, PDF_FONT_REG, PDF_ENC), 9.f);
    HPDF_Page_SetRGBFill(page, 0.05f, 0.05f, 0.05f);
    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, x + 170, y, ru(val).c_str());
    HPDF_Page_EndText(page);
}

// ─── asset report ─────────────────────────────────────────────────────────────
std::string ReportGenerator::generateAssetReport(int pair_id) {
    auto pair   = db_.getTradingPairById(pair_id);
    auto ps     = stats_.priceStats(pair_id);
    auto vs     = stats_.volumeStats(pair_id);
    auto candles = db_.getCandlesticks(pair_id);
    auto vol_m  = vol_.getMetrics(pair_id);

    HPDF_Doc pdf = HPDF_New(hpdfErrorHandler, nullptr);
    pdfLoadFonts(pdf);
    HPDF_SetCompressionMode(pdf, HPDF_COMP_ALL);

    HPDF_Page page = newPage(pdf);
    float height   = HPDF_Page_GetHeight(page);
    float y        = height - 60;

    addTitle(pdf, page, "Отчёт по активу: " + pair.pair_symbol, y);
    y -= 30;
    addText(pdf, page, "Сформирован: " + dateFmt(std::time(nullptr)) +
            "   |   Биржа: " + pair.exchange_name +
            "   |   Записей (дней): " + std::to_string(candles.size()), 50, y, 8.f);

    // ── Basic statistics ──
    addSection(pdf, page, "1. Базовая статистика цены (USDT)", y);
    note(pdf, page, "Описательная статистика цен закрытия за весь период. "
                    "Медиана устойчивее к выбросам, чем среднее; СКО — мера разброса.", y);
    addKeyValue(pdf, page, "Минимальная цена",   fmt(ps.min_val),  50, y);
    addKeyValue(pdf, page, "Максимальная цена",  fmt(ps.max_val),  50, y);
    addKeyValue(pdf, page, "Средняя цена",        fmt(ps.mean),     50, y);
    addKeyValue(pdf, page, "Медиана",             fmt(ps.median),   50, y);
    addKeyValue(pdf, page, "Стандартное откл. (СКО)", fmt(ps.std_dev), 50, y);
    addKeyValue(pdf, page, "Нижний квартиль (Q1)", fmt(ps.q1),      50, y);
    addKeyValue(pdf, page, "Верхний квартиль (Q3)", fmt(ps.q3),     50, y);
    if (!candles.empty())
        addKeyValue(pdf, page, "Текущая цена",
                    fmt(candles.back().close_price), 50, y);

    // ── Volume statistics ──
    addSection(pdf, page, "2. Статистика объёма торгов", y);
    note(pdf, page, "Объём — суточный оборот в USDT. Высокий объём подтверждает "
                    "значимость ценового движения.", y);
    addKeyValue(pdf, page, "Минимальный объём", fmt(vs.min_val, 0), 50, y);
    addKeyValue(pdf, page, "Максимальный объём", fmt(vs.max_val, 0), 50, y);
    addKeyValue(pdf, page, "Средний объём",      fmt(vs.mean,    0), 50, y);

    // ── Returns ──
    addSection(pdf, page, "3. Доходность", y);
    note(pdf, page, "Изменение цены за период: (P_конец - P_начало) / P_начало.", y);
    addKeyValue(pdf, page, "За 7 дней",   pctFmt(stats_.priceChange(pair_id,  7)), 50, y);
    addKeyValue(pdf, page, "За 30 дней",  pctFmt(stats_.priceChange(pair_id, 30)), 50, y);
    addKeyValue(pdf, page, "За 90 дней",  pctFmt(stats_.priceChange(pair_id, 90)), 50, y);
    addKeyValue(pdf, page, "За 1 год",    pctFmt(stats_.priceChange(pair_id,365)), 50, y);

    // ── Volatility ──
    addSection(pdf, page, "4. Показатели волатильности", y);
    note(pdf, page, "СКО дневных лог-доходностей, масштабированное на горизонт: "
                    "x1 — в день, x корень(7) — в неделю, x корень(30) — в месяц.", y);
    for (const auto& vm : vol_m) {
        std::string per  = vm.period_type == "daily"   ? "Дневная волатильность" :
                           vm.period_type == "weekly"  ? "Недельная волатильность" :
                           vm.period_type == "monthly" ? "Месячная волатильность" : vm.period_type;
        std::string unit = vm.period_type == "daily"   ? "в день" :
                           vm.period_type == "weekly"  ? "в неделю" :
                           vm.period_type == "monthly" ? "в месяц" : "";
        addKeyValue(pdf, page, per,
            pctFmt(vm.volatility * 100) + "   (" + unit + ")",
            50, y);
    }
    // СКО доходности — отдельной строкой (= дневная σ)
    if (!vol_m.empty()) {
        addKeyValue(pdf, page, "СКО доходности",
            pctFmt(vol_m[0].std_dev * 100) + "   (дневное СКО лог-доходностей)",
            50, y);
    }

    y -= 20;

    // ── Price chart ──
    if (!candles.empty()) {
        std::vector<double> prices, volumes;
        std::vector<std::string> dates;
        for (const auto& c : candles) {
            prices.push_back(c.close_price);
            volumes.push_back(c.volume);
            dates.push_back(dateFmt(c.timestamp));
        }
        if (y < 250) { page = newPage(pdf); y = height - 60; }
        addSection(pdf, page, "5. График цены и объёма торгов", y);
        y -= 6;
        PdfChart::priceVolume(pdf, page, 50, y - 230, 495, 220,
                               prices, volumes, dates,
                               pair.pair_symbol);
        y -= 240;
    }

    // ── Rolling volatility chart ──
    {
        auto rv = vol_.rollingVolatility(pair_id, 30);
        if (!rv.empty()) {
            if (y < 150) { page = newPage(pdf); y = height - 60; }
            std::vector<double> vvals;
            std::vector<std::string> vdates;
            for (const auto& [ts, v] : rv) {
                vvals.push_back(v * 100);
                vdates.push_back(dateFmt(ts));
            }
            addSection(pdf, page, "6. Скользящая волатильность (окно 30 дней), %", y);
            note(pdf, page, "Аннуализированная волатильность, пересчитанная по "
                            "скользящему окну в 30 дней — показывает, как менялся риск.", y);
            PdfChart::lineChart(pdf, page, 50, y - 160, 495, 150,
                                 vvals, vdates,
                                 ru("Скользящая волатильность (30 дней)"),
                                 ru("Вол., %"), 0.8f, 0.3f, 0.2f);
            y -= 170;
        }
    }

    // ── Footer ──
    HPDF_Page_SetFontAndSize(page, HPDF_GetFont(pdf, PDF_FONT_REG, PDF_ENC), 7.f);
    HPDF_Page_SetRGBFill(page, 0.5f, 0.5f, 0.5f);
    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, 50, 20,
        ru("Система аналитики криптовалют  |  Данные: gate.io + coingecko  |  Учебный проект").c_str());
    HPDF_Page_EndText(page);

    std::string path = outputPath("asset_" + pair.pair_symbol);
    HPDF_SaveToFile(pdf, path.c_str());
    HPDF_Free(pdf);
    LOG_INFO("Asset report saved: " + path);
    return path;
}

// ─── comparative report ───────────────────────────────────────────────────────
std::string ReportGenerator::generateComparativeReport(
        const std::vector<int>& pair_ids) {

    HPDF_Doc pdf = HPDF_New(hpdfErrorHandler, nullptr);
    pdfLoadFonts(pdf);
    HPDF_SetCompressionMode(pdf, HPDF_COMP_ALL);
    HPDF_Page page = newPage(pdf);
    float height   = HPDF_Page_GetHeight(page);
    float y        = height - 60;

    addTitle(pdf, page, "Сравнительный отчёт по рынку", y);
    y -= 30;
    addText(pdf, page, "Сформирован: " + dateFmt(std::time(nullptr)), 50, y, 8.f);

    // Table header
    addSection(pdf, page, "1. Сводная таблица показателей", y);
    note(pdf, page, "Ключевые метрики по всем активам. Капитализация и объём 24ч — "
                    "по данным CoinGecko; доходность и волатильность — за 30 дней.", y);
    y -= 4;
    {
        // Draw table header
        HPDF_Page_SetRGBFill(page, 0.2f, 0.3f, 0.6f);
        HPDF_Page_Rectangle(page, 50, y - 14, 495, 14);
        HPDF_Page_Fill(page);

        HPDF_Page_SetFontAndSize(page, HPDF_GetFont(pdf, PDF_FONT_BOLD, PDF_ENC), 8.f);
        HPDF_Page_SetRGBFill(page, 1.f, 1.f, 1.f);
        HPDF_Page_BeginText(page);
        float cols[] = {55, 110, 185, 270, 350, 425, 495};
        const char* headers[] = {"Пара","Цена","Капит-ция","Объём 24ч",
                                  "Дох. 30д","Вол. 30д","СКО"};
        for (int i = 0; i < 7; ++i)
            HPDF_Page_TextOut(page, cols[i], y - 10, ru(headers[i]).c_str());
        HPDF_Page_EndText(page);
        y -= 16;
    }

    bool alt = false;
    for (int pid : pair_ids) {
        auto pair = db_.getTradingPairById(pid);
        auto ps   = stats_.priceStats(pid);
        auto ticker = db_.getLatestTicker(pid);
        auto vm_list = vol_.getMetrics(pid, "monthly");
        double vol_pct  = vm_list.empty() ? 0 : vm_list[0].volatility * 100;
        double ret30    = stats_.priceChange(pid, 30);

        if (alt) {
            HPDF_Page_SetRGBFill(page, 0.95f, 0.95f, 0.98f);
            HPDF_Page_Rectangle(page, 50, y - 12, 495, 13);
            HPDF_Page_Fill(page);
        }
        alt = !alt;

        HPDF_Page_SetFontAndSize(page, HPDF_GetFont(pdf, PDF_FONT_REG, PDF_ENC), 8.f);
        HPDF_Page_SetRGBFill(page, 0.1f, 0.1f, 0.1f);
        HPDF_Page_BeginText(page);
        float cols[] = {55, 110, 185, 270, 350, 425, 495};
        std::string row[] = {
            pair.pair_symbol,
            "$" + fmt(ps.mean),
            ticker.market_cap > 0 ? "$" + fmt(ticker.market_cap / 1e9, 2) + " млрд" : "—",
            ticker.volume_24h > 0 ? "$" + fmt(ticker.volume_24h / 1e6, 1) + " млн" : "—",
            pctFmt(ret30),
            pctFmt(vol_pct),
            fmt(ps.std_dev)
        };
        for (int i = 0; i < 7; ++i)
            HPDF_Page_TextOut(page, cols[i], y - 9, ru(row[i]).c_str());
        HPDF_Page_EndText(page);
        y -= 14;
    }

    y -= 10;

    // Market cap bar chart
    {
        std::vector<double> caps;
        std::vector<std::string> lbls;
        for (int pid : pair_ids) {
            auto pair   = db_.getTradingPairById(pid);
            auto ticker = db_.getLatestTicker(pid);
            if (ticker.market_cap > 0) {
                caps.push_back(ticker.market_cap / 1e9);
                lbls.push_back(pair.base_asset);
            }
        }
        if (!caps.empty()) {
            if (y < 190) { page = newPage(pdf); y = height - 60; }
            addSection(pdf, page, "2. Рыночная капитализация (млрд USD)", y);
            note(pdf, page, "Капитализация = цена x объём предложения в обращении. "
                            "Чем выше, тем «крупнее» актив.", y);
            PdfChart::barChart(pdf, page, 50, y - 160, 495, 150,
                                caps, lbls, ru("Капитализация, млрд USD"),
                                0.2f, 0.5f, 0.8f);
            y -= 170;
        }
    }

    // Volume comparison chart
    {
        std::vector<double> vols;
        std::vector<std::string> lbls;
        for (int pid : pair_ids) {
            auto pair   = db_.getTradingPairById(pid);
            auto ticker = db_.getLatestTicker(pid);
            if (ticker.volume_24h > 0) {
                vols.push_back(ticker.volume_24h / 1e6); // millions
                lbls.push_back(pair.base_asset);
            }
        }
        if (!vols.empty()) {
            if (y < 190) { page = newPage(pdf); y = height - 60; }
            addSection(pdf, page, "3. Объём торгов за 24ч (млн USD)", y);
            note(pdf, page, "Суточный объём торгов показывает ликвидность актива. "
                            "Высокий объём — больше участников, ниже спред.", y);
            PdfChart::barChart(pdf, page, 50, y - 160, 495, 150,
                                vols, lbls, ru("Объём торгов 24ч, млн USD"),
                                0.2f, 0.6f, 0.5f);
            y -= 170;
        }
    }

    // 30-day return chart
    {
        std::vector<double> rets;
        std::vector<std::string> lbls;
        for (int pid : pair_ids) {
            auto pair = db_.getTradingPairById(pid);
            rets.push_back(stats_.priceChange(pid, 30));
            lbls.push_back(pair.base_asset);
        }
        if (y < 190) { page = newPage(pdf); y = height - 60; }
        addSection(pdf, page, "4. Доходность за 30 дней (%)", y);
        note(pdf, page, "Отрицательные значения (красные столбцы) — снижение цены за период.", y);
        PdfChart::barChart(pdf, page, 50, y - 160, 495, 150,
                            rets, lbls, ru("Доходность за 30 дней, %"),
                            0.3f, 0.7f, 0.4f);
        y -= 170;
    }

    // Price dynamics — all pairs on one shared chart (indexed to 100 at start)
    {
        std::vector<std::vector<double>> all_series;
        std::vector<std::string>         series_names;
        std::vector<std::string>         shared_dates;

        for (int pid : pair_ids) {
            auto candles = db_.getCandlesticks(pid);
            if (candles.size() < 2) continue;
            double base = candles.front().close_price;
            if (base == 0) continue;
            std::vector<double> idx;
            std::vector<std::string> dates;
            for (const auto& c : candles) {
                idx.push_back(c.close_price / base * 100.0);
                dates.push_back(dateFmt(c.timestamp));
            }
            all_series.push_back(std::move(idx));
            series_names.push_back(db_.getTradingPairById(pid).pair_symbol);
            if (shared_dates.empty()) shared_dates = std::move(dates);
        }

        if (!all_series.empty()) {
            if (y < 220) { page = newPage(pdf); y = height - 60; }
            addSection(pdf, page, "5. Динамика цен (нормировано к 100 на старте)", y);
            note(pdf, page, "Цена каждого актива приведена к 100 в начале периода — "
                            "это позволяет сравнить относительную динамику в одном масштабе.", y);
            PdfChart::multiLineChart(pdf, page, 50, y - 190, 495, 180,
                                      all_series, shared_dates, series_names,
                                      ru("Относительная динамика (старт = 100)"), ru("Индекс"));
            y -= 200;
        }
    }

    // Footer
    HPDF_Page_SetFontAndSize(page, HPDF_GetFont(pdf, PDF_FONT_REG, PDF_ENC), 7.f);
    HPDF_Page_SetRGBFill(page, 0.5f, 0.5f, 0.5f);
    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, 50, 20,
        ru("Система аналитики криптовалют  |  Данные: gate.io + coingecko  |  Учебный проект").c_str());
    HPDF_Page_EndText(page);

    std::string path = outputPath("comparative");
    HPDF_SaveToFile(pdf, path.c_str());
    HPDF_Free(pdf);
    LOG_INFO("Comparative report saved: " + path);
    return path;
}

// ─── correlation report ───────────────────────────────────────────────────────
std::string ReportGenerator::generateCorrelationReport(
        const std::vector<int>& pair_ids, int period_days) {

    HPDF_Doc pdf = HPDF_New(hpdfErrorHandler, nullptr);
    pdfLoadFonts(pdf);
    HPDF_SetCompressionMode(pdf, HPDF_COMP_ALL);
    HPDF_Page page = newPage(pdf);
    float height   = HPDF_Page_GetHeight(page);
    float y        = height - 60;

    addTitle(pdf, page, "Отчёт по корреляциям активов", y);
    y -= 30;
    addText(pdf, page, "Период: " + std::to_string(period_days) +
            " дней   |   Сформирован: " + dateFmt(std::time(nullptr)), 50, y, 8.f);

    addSection(pdf, page, "1. Парные корреляции Пирсона (по лог-доходностям)", y);
    note(pdf, page, "Коэффициент r от -1 до +1. Около +1 — активы движутся синхронно; "
                    "около 0 — независимы; около -1 — в противоположных направлениях.", y);

    // Compute matrix
    std::vector<std::string> labels;
    for (int pid : pair_ids)
        labels.push_back(db_.getTradingPairById(pid).base_asset);

    auto mat = corr_.correlationMatrix(pair_ids, period_days);

    // Text table
    for (size_t i = 0; i < pair_ids.size(); ++i) {
        for (size_t j = i + 1; j < pair_ids.size(); ++j) {
            double r = mat[i][j];
            std::string interp = r > 0.7 ? "сильная положительная" :
                                 r > 0.3 ? "умеренная положительная" :
                                 r < -0.7 ? "сильная отрицательная" :
                                 r < -0.3 ? "умеренная отрицательная" :
                                 "слабая / отсутствует";
            addKeyValue(pdf, page, labels[i] + " <-> " + labels[j],
                        fmt(r, 4) + "   (" + interp + ")", 50, y);
        }
    }

    y -= 20;
    addSection(pdf, page, "2. Тепловая карта корреляций", y);
    note(pdf, page, "Синий — положительная корреляция, красный — отрицательная. "
                    "Шкала справа: от -1 до +1.", y);

    // Heatmap
    if (y < 220) { page = newPage(pdf); y = height - 60; }
    PdfChart::heatmap(pdf, page, 100, y - 220, 400, 200,
                       mat, labels, ru("Матрица корреляций (Пирсон)"));
    y -= 230;

    // Spearman
    addSection(pdf, page, "3. Ранговые корреляции Спирмена", y);
    note(pdf, page, "Корреляция рангов цен. Устойчива к нелинейности и выбросам, "
                    "дополняет коэффициент Пирсона.", y);
    for (size_t i = 0; i < pair_ids.size(); ++i) {
        for (size_t j = i + 1; j < pair_ids.size(); ++j) {
            std::time_t now  = std::time(nullptr);
            std::time_t from = now - static_cast<std::time_t>(period_days) * 86400;
            auto c1 = db_.getCandlesticks(pair_ids[i], from, now);
            auto c2 = db_.getCandlesticks(pair_ids[j], from, now);
            if (c1.size() < 5 || c2.size() < 5) continue;
            std::vector<double> p1, p2;
            for (const auto& c : c1) p1.push_back(c.close_price);
            for (const auto& c : c2) p2.push_back(c.close_price);
            size_t n = std::min(p1.size(), p2.size());
            p1.resize(n); p2.resize(n);
            double sp = CorrelationAnalyzer::spearman(p1, p2);
            addKeyValue(pdf, page,
                labels[i] + " <-> " + labels[j],
                fmt(sp, 4), 50, y);
        }
    }

    // Footer
    HPDF_Page_SetFontAndSize(page, HPDF_GetFont(pdf, PDF_FONT_REG, PDF_ENC), 7.f);
    HPDF_Page_SetRGBFill(page, 0.5f, 0.5f, 0.5f);
    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, 50, 20,
        ru("Система аналитики криптовалют  |  Данные: gate.io + coingecko  |  Учебный проект").c_str());
    HPDF_Page_EndText(page);

    std::string path = outputPath("correlation");
    HPDF_SaveToFile(pdf, path.c_str());
    HPDF_Free(pdf);
    LOG_INFO("Correlation report saved: " + path);
    return path;
}

// ─── anomaly report ───────────────────────────────────────────────────────────
std::string ReportGenerator::generateAnomalyReport(
        const std::vector<int>& pair_ids) {

    HPDF_Doc pdf = HPDF_New(hpdfErrorHandler, nullptr);
    pdfLoadFonts(pdf);
    HPDF_SetCompressionMode(pdf, HPDF_COMP_ALL);
    HPDF_Page page = newPage(pdf);
    float height   = HPDF_Page_GetHeight(page);
    float y        = height - 60;

    addTitle(pdf, page, "Отчёт по аномалиям рынка", y);
    y -= 30;
    addText(pdf, page, "Сформирован: " + dateFmt(std::time(nullptr)), 50, y, 8.f);
    note(pdf, page, "Аномалии выявлены тремя методами: z-оценка лог-доходностей (|z|>3), "
                    "z-оценка объёма и пробой 20-дн. скользящей средней (разворот при отклонении >10%).", y);

    // Marker kind by anomaly type: 0 = down/red, 1 = up/green, 2 = volume/orange
    auto markerKind = [](const std::string& t) -> int {
        if (t == "price_spike" || t == "ma_breakout_up")   return 1;
        if (t == "volume_spike")                            return 2;
        return 0; // price_drop, ma_breakout_down
    };

    // Pre-fetch all anomalies per asset (for the summary + detail sections)
    std::map<int, std::vector<Anomaly>> by_pid;
    int tot_spike = 0, tot_drop = 0, tot_vol = 0, tot_ma = 0;
    for (int pid : pair_ids) {
        auto a = anom_.getAnomalies(pid, 1000);
        for (const auto& x : a) {
            if      (x.anomaly_type == "price_spike")  ++tot_spike;
            else if (x.anomaly_type == "price_drop")   ++tot_drop;
            else if (x.anomaly_type == "volume_spike") ++tot_vol;
            else                                        ++tot_ma;
        }
        by_pid[pid] = std::move(a);
    }

    // ── Overall dashboard ──
    addSection(pdf, page, "Сводка по всем активам", y);
    note(pdf, page, "Всего обнаруженных событий по типам (по всем парам за период).", y);
    addKeyValue(pdf, page, "Резкий рост цены (скачок)",  std::to_string(tot_spike), 50, y);
    addKeyValue(pdf, page, "Резкое падение цены",        std::to_string(tot_drop),  50, y);
    addKeyValue(pdf, page, "Аномальный объём торгов",    std::to_string(tot_vol),   50, y);
    addKeyValue(pdf, page, "Пробой скольз. средней",     std::to_string(tot_ma),    50, y);
    y -= 8;

    // ── Per-asset detail ──
    for (int pid : pair_ids) {
        const auto& anomalies = by_pid[pid];
        auto pair    = db_.getTradingPairById(pid);
        auto candles = db_.getCandlesticks(pid);

        if (y < 280) { page = newPage(pdf); y = height - 60; }

        // Per-type counts for the header line
        int spikes = 0, drops = 0, vol_spikes = 0, ma_breaks = 0;
        for (const auto& a : anomalies) {
            if      (a.anomaly_type == "price_spike")  ++spikes;
            else if (a.anomaly_type == "price_drop")   ++drops;
            else if (a.anomaly_type == "volume_spike") ++vol_spikes;
            else                                        ++ma_breaks;
        }

        addSection(pdf, page, pair.pair_symbol + " — всего событий: " +
                   std::to_string(anomalies.size()), y);
        addText(pdf, page,
            "Скачки: " + std::to_string(spikes) +
            "   |   Падения: " + std::to_string(drops) +
            "   |   Объём: " + std::to_string(vol_spikes) +
            "   |   Пробой СС: " + std::to_string(ma_breaks),
            55, y, 8.5f);

        if (anomalies.empty()) {
            addText(pdf, page, "  Аномалий не обнаружено.", 55, y, 9.f);
            continue;
        }

        // Map candle date -> index, then place markers
        std::vector<double>      prices;
        std::vector<std::string> dates;
        std::map<std::string, int> date_idx;
        for (const auto& c : candles) {
            date_idx[dateFmt(c.timestamp)] = static_cast<int>(prices.size());
            prices.push_back(c.close_price);
            dates.push_back(dateFmt(c.timestamp));
        }
        std::vector<int> m_idx, m_kind;
        for (const auto& a : anomalies) {
            auto it = date_idx.find(dateFmt(a.detected_at));
            if (it == date_idx.end()) continue;
            m_idx.push_back(it->second);
            m_kind.push_back(markerKind(a.anomaly_type));
        }

        // Price chart with anomaly markers
        if (!prices.empty()) {
            if (y < 230) { page = newPage(pdf); y = height - 60; }
            PdfChart::anomalyChart(pdf, page, 50, y - 185, 495, 180,
                                    prices, dates, m_idx, m_kind,
                                    "",  // asset name already in the section header above
                                    ru("Цена, USDT"));
            y -= 195;
        }

        // Curated "most significant" events: strongest of each type
        std::map<std::string, Anomaly> strongest;
        for (const auto& a : anomalies) {
            auto it = strongest.find(a.anomaly_type);
            if (it == strongest.end() || a.severity > it->second.severity)
                strongest[a.anomaly_type] = a;
        }
        // Display in a fixed, meaningful order
        const std::vector<std::string> order = {
            "price_drop", "price_spike", "volume_spike",
            "ma_breakout_down", "ma_breakout_up"
        };
        if (y < 90) { page = newPage(pdf); y = height - 60; }
        addText(pdf, page, "Самые значимые события:", 55, y, 8.5f);
        for (const auto& code : order) {
            auto it = strongest.find(code);
            if (it == strongest.end()) continue;
            const auto& a = it->second;
            if (y < 40) { page = newPage(pdf); y = height - 60; }
            addText(pdf, page,
                dateFmt(a.detected_at) + "  [" + anomalyTypeRu(a.anomaly_type) + "]  " +
                a.description, 60, y, 8.f);
        }
        y -= 8;
    }

    // Footer
    HPDF_Page_SetFontAndSize(page, HPDF_GetFont(pdf, PDF_FONT_REG, PDF_ENC), 7.f);
    HPDF_Page_SetRGBFill(page, 0.5f, 0.5f, 0.5f);
    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, 50, 20,
        ru("Система аналитики криптовалют  |  Данные: gate.io + coingecko  |  Учебный проект").c_str());
    HPDF_Page_EndText(page);

    std::string path = outputPath("anomalies");
    HPDF_SaveToFile(pdf, path.c_str());
    HPDF_Free(pdf);
    LOG_INFO("Anomaly report saved: " + path);
    return path;
}

// ─── forecast report ──────────────────────────────────────────────────────────
std::string ReportGenerator::generateForecastReport(int pair_id) {
    auto pair     = db_.getTradingPairById(pair_id);
    auto forecasts = fcast_.getForecasts(pair_id, 60);
    auto candles  = db_.getCandlesticks(pair_id);

    if (forecasts.empty())
        forecasts = fcast_.forecastAndSave(pair_id);

    HPDF_Doc pdf = HPDF_New(hpdfErrorHandler, nullptr);
    pdfLoadFonts(pdf);
    HPDF_SetCompressionMode(pdf, HPDF_COMP_ALL);
    HPDF_Page page = newPage(pdf);
    float height   = HPDF_Page_GetHeight(page);
    float y        = height - 60;

    addTitle(pdf, page, "Прогноз цены: " + pair.pair_symbol, y);
    y -= 30;
    addText(pdf, page, "Сформирован: " + dateFmt(std::time(nullptr)), 50, y, 8.f);
    note(pdf, page, "Три модели: SMA, Холт (двойное эксп. сглаживание) и линейная "
                    "регрессия. Горизонт 7 дней, зона — интервал +/-2 СКО.", y);

    // Helper: human-readable model name (full + short for compact legends)
    auto modelRu = [](const std::string& m) -> std::string {
        if (m == "SMA")              return "Скользящее среднее (SMA)";
        if (m == "Holt")             return "Двойное эксп. сглаживание (Холт)";
        if (m == "LinearRegression") return "Линейная регрессия (МНК)";
        return m;
    };
    auto modelShort = [](const std::string& m) -> std::string {
        if (m == "SMA")              return "SMA";
        if (m == "Holt")             return "Холт";
        if (m == "LinearRegression") return "Регрессия";
        return m;
    };

    addSection(pdf, page, "Исторический контекст (за 30 дней)", y);
    {
        std::time_t now30  = std::time(nullptr);
        std::time_t from30 = now30 - 30LL * 86400;
        auto ps30 = stats_.priceStats(pair_id, from30, now30);
        addKeyValue(pdf, page, "Текущая цена", "$" + fmt(candles.empty() ? 0 :
                    candles.back().close_price), 50, y);
        addKeyValue(pdf, page, "Средняя за 30д",   "$" + fmt(ps30.mean),    50, y);
        addKeyValue(pdf, page, "СКО за 30д",       "$" + fmt(ps30.std_dev), 50, y);
        addKeyValue(pdf, page, "Доходность за 30д", pctFmt(stats_.priceChange(pair_id, 30)), 50, y);
    }

    // ── Backtest accuracy: which model tracked history best ──
    addSection(pdf, page, "Точность моделей (бэктест на истории)", y);
    note(pdf, page, "Прогноз на 1 день вперёд проверен по последним ~60 дням. "
                    "MAPE — средняя относительная ошибка (меньше = точнее); "
                    "RMSE — ошибка в долларах.", y);
    auto acc = fcast_.backtest(pair_id, 60);
    std::string best_model; double best_mape = 1e18;
    for (const auto& [m, a] : acc)
        if (a.mape < best_mape) { best_mape = a.mape; best_model = m; }
    for (const auto& [m, a] : acc) {
        std::string mark = (m == best_model) ? "  <-- лучшая" : "";
        addKeyValue(pdf, page, modelRu(m),
            "MAPE " + fmt(a.mape, 2) + "%   |   RMSE $" + fmt(a.rmse) + mark, 50, y);
    }

    // Build shared history series (last 30 actual closes + dates)
    std::vector<double>      hist;
    std::vector<std::string> hist_dates;
    {
        int hN = std::min(30, static_cast<int>(candles.size()));
        for (int i = static_cast<int>(candles.size()) - hN;
             i < static_cast<int>(candles.size()); ++i) {
            hist.push_back(candles[i].close_price);
            hist_dates.push_back(dateFmt(candles[i].timestamp));
        }
    }

    // Group forecasts by model
    std::map<std::string, std::vector<ForecastResult>> by_model;
    for (const auto& f : forecasts) by_model[f.model_type].push_back(f);

    // Concise per-model summary: where each model expects the price to go
    addSection(pdf, page, "Прогноз на 7 дней (итог по моделям)", y);
    note(pdf, page, "Куда каждая модель ведёт цену к концу горизонта. "
                    "Холт и регрессия учитывают тренд, SMA — плоская (базовая линия).", y);
    double cur_price = candles.empty() ? 0 : candles.back().close_price;
    for (const auto& [model, flist] : by_model) {
        if (flist.empty()) continue;
        double target = flist.back().predicted_price;
        double chg    = cur_price > 0 ? (target - cur_price) / cur_price * 100.0 : 0;
        addKeyValue(pdf, page, modelRu(model),
            "$" + fmt(target) + " к " + dateFmt(flist.back().forecast_for) +
            "   (" + (chg >= 0 ? "+" : "") + fmt(chg, 1) + "% от текущей)", 50, y);
    }
    y -= 6;

    // One combined chart: shared history + all model forecasts overlaid
    if (y < 240) { page = newPage(pdf); y = height - 60; }
    addSection(pdf, page, "Сравнение прогнозов на одном графике", y);
    note(pdf, page, "Серая линия — факт за 30 дней; цветные — прогнозы моделей. "
                    "Пунктир = «сегодня». Видно, как трендовые модели расходятся "
                    "с плоской SMA.", y);
    {
        std::vector<std::vector<double>> fc_series;
        std::vector<std::string>         fc_names;
        std::vector<std::string>         all_dates = hist_dates;
        bool dates_done = false;
        for (const auto& [model, flist] : by_model) {
            if (flist.empty()) continue;
            std::vector<double> fc;
            for (const auto& f : flist) {
                fc.push_back(f.predicted_price);
                if (!dates_done) all_dates.push_back(dateFmt(f.forecast_for));
            }
            dates_done = true;
            fc_series.push_back(fc);
            fc_names.push_back(ru(modelShort(model)));
        }
        PdfChart::multiForecastChart(pdf, page, 50, y - 210, 495, 200,
                                      hist, fc_series, fc_names, all_dates,
                                      "", ru("Цена, USDT"));
        y -= 220;
    }

    // Footer
    HPDF_Page_SetFontAndSize(page, HPDF_GetFont(pdf, PDF_FONT_REG, PDF_ENC), 7.f);
    HPDF_Page_SetRGBFill(page, 0.5f, 0.5f, 0.5f);
    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, 50, 20,
        ru("Система аналитики криптовалют  |  Данные: gate.io + coingecko  |  Учебный проект").c_str());
    HPDF_Page_EndText(page);

    std::string path = outputPath("forecast_" + pair.pair_symbol);
    HPDF_SaveToFile(pdf, path.c_str());
    HPDF_Free(pdf);
    LOG_INFO("Forecast report saved: " + path);
    return path;
}

// ─── full report ──────────────────────────────────────────────────────────────
std::string ReportGenerator::generateFullReport(
        const std::vector<int>& pair_ids) {

    std::vector<std::string> files;
    for (int pid : pair_ids)
        files.push_back(generateAssetReport(pid));
    files.push_back(generateComparativeReport(pair_ids));
    files.push_back(generateCorrelationReport(pair_ids));
    files.push_back(generateAnomalyReport(pair_ids));
    for (int pid : pair_ids)
        files.push_back(generateForecastReport(pid));

    LOG_INFO("Full report suite generated (" +
             std::to_string(files.size()) + " files)");
    return files[0]; // return first file path
}
