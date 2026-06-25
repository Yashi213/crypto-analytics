#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <iomanip>

#include "utils/Config.h"
#include "utils/Logger.h"
#include "database/DatabaseManager.h"
#include "etl/GateioFetcher.h"
#include "etl/CoinGeckoFetcher.h"
#include "etl/ETLPipeline.h"
#include "analytics/StatisticsAnalyzer.h"
#include "analytics/VolatilityAnalyzer.h"
#include "analytics/CorrelationAnalyzer.h"
#include "analytics/AnomalyDetector.h"
#include "analytics/Forecaster.h"
#include "reports/ReportGenerator.h"

// ─── runtime settings (in-memory only; config file is never modified) ────────────
struct RunSettings {
    std::vector<std::string>          pairs;
    std::map<std::string, std::string> coingecko_ids;
    int    historical_days       = 365;
    int    forecast_days         = 7;
    double anomaly_zscore        = 3.0;
    double anomaly_iqr_mult      = 1.5;
    double anomaly_ma_dev_pct    = 10.0;
    double anomaly_volume_sigma  = 3.0;
    int    forecast_sma_window   = 14;
    double forecast_ema_alpha    = 0.2;
    int    forecast_lin_reg_win  = 30;
};

void runAll(ETLPipeline& etl, DatabaseManager& db,
            StatisticsAnalyzer& stats, VolatilityAnalyzer& vol,
            CorrelationAnalyzer& corr, AnomalyDetector& anom,
            Forecaster& fcast, ReportGenerator& rg,
            const std::vector<std::string>& pairs);

// ─── input helpers ───────────────────────────────────────────────────────────────
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string ask(const std::string& question, const std::string& def) {
    std::cout << question << " [" << def << "]: ";
    std::string line;
    std::getline(std::cin, line);
    std::string v = trim(line);
    return v.empty() ? def : v;
}

static std::vector<std::string> splitCsv(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        std::string v = trim(item);
        if (!v.empty()) out.push_back(v);
    }
    return out;
}

static std::string joinPairs(const std::vector<std::string>& pairs) {
    std::string s;
    for (size_t i = 0; i < pairs.size(); ++i) {
        s += pairs[i];
        if (i + 1 < pairs.size()) s += ", ";
    }
    return s;
}

static RunSettings settingsFromConfig() {
    const auto& cfg = Config::instance();
    RunSettings s;
    s.pairs              = cfg.tradingPairs();
    s.historical_days    = cfg.historicalDays();
    s.forecast_days      = cfg.forecastDays();
    s.anomaly_zscore     = cfg.anomalyZscoreThreshold();
    s.anomaly_iqr_mult   = cfg.anomalyIqrMultiplier();
    s.anomaly_ma_dev_pct = cfg.anomalyMaDeviationPct();
    s.anomaly_volume_sigma = cfg.anomalyVolumeSpikeSigma();
    s.forecast_sma_window  = cfg.forecastSmaWindow();
    s.forecast_ema_alpha   = cfg.forecastEmaAlpha();
    s.forecast_lin_reg_win = cfg.forecastLinRegWindow();
    for (const auto& p : s.pairs) {
        std::string id = cfg.coingeckoId(p);
        if (!id.empty()) s.coingecko_ids[p] = id;
    }
    return s;
}

static void ensureCoingeckoIds(RunSettings& s) {
    for (const auto& p : s.pairs) {
        if (s.coingecko_ids.count(p)) continue;
        std::string from_cfg = Config::instance().coingeckoId(p);
        if (!from_cfg.empty()) {
            s.coingecko_ids[p] = from_cfg;
            continue;
        }
        std::cout << "  CoinGecko id для " << p
                  << " (например, bitcoin): ";
        std::string line;
        std::getline(std::cin, line);
        std::string id = trim(line);
        if (!id.empty()) s.coingecko_ids[p] = id;
    }
}

static void printSettings(const RunSettings& s) {
    std::cout << "\n  Текущие параметры прогона:\n"
              << "  ─────────────────────────────────────────\n"
              << "  Пары:                  " << joinPairs(s.pairs) << "\n"
              << "  Глубина истории:       " << s.historical_days << " дн.\n"
              << "  Горизонт прогноза:     " << s.forecast_days << " дн.\n"
              << "  Z-порог (аномалии):    " << s.anomaly_zscore << "\n"
              << "  Отклонение от СС:      " << s.anomaly_ma_dev_pct << " %\n"
              << "  Всплеск объёма:        " << s.anomaly_volume_sigma << " σ\n"
              << "  Окно SMA:              " << s.forecast_sma_window << "\n"
              << "  α Холта:               " << s.forecast_ema_alpha << "\n"
              << "  Окно регрессии:        " << s.forecast_lin_reg_win << "\n"
              << "  ─────────────────────────────────────────\n";
}

static void editPairs(RunSettings& s) {
    std::cout << "\n  Текущие пары: " << joinPairs(s.pairs) << "\n"
              << "  1 — заменить список (через запятую)\n"
              << "  2 — добавить пару\n"
              << "  Enter — оставить без изменений\n"
              << "  Выбор: ";
    std::string line;
    std::getline(std::cin, line);
    line = trim(line);
    if (line.empty()) return;

    if (line == "1") {
        auto chosen = splitCsv(ask("  Новый список пар", joinPairs(s.pairs)));
        if (!chosen.empty()) s.pairs = chosen;
    } else if (line == "2") {
        std::string pair = trim(ask("  Символ пары (например, ADA_USDT)", ""));
        if (pair.empty()) return;
        if (std::find(s.pairs.begin(), s.pairs.end(), pair) == s.pairs.end())
            s.pairs.push_back(pair);
    }
    ensureCoingeckoIds(s);
}

static bool configureSettings(RunSettings& s) {
    while (true) {
        printSettings(s);
        std::cout << "\n  1 — торговые пары\n"
                  << "  2 — глубина истории (дней)\n"
                  << "  3 — горизонт прогноза (дней)\n"
                  << "  4 — z-порог аномалий\n"
                  << "  5 — отклонение от СС, %\n"
                  << "  6 — всплеск объёма, σ\n"
                  << "  7 — окно SMA\n"
                  << "  8 — α Холта\n"
                  << "  9 — окно линейной регрессии\n"
                  << "  0 — назад в главное меню\n"
                  << "  Enter — запустить прогон с этими параметрами\n"
                  << "\n  Выбор: ";
        std::string line;
        std::getline(std::cin, line);
        line = trim(line);

        if (line.empty()) return true;

        if (line == "0") return false;

        try {
            if      (line == "1") editPairs(s);
            else if (line == "2") s.historical_days =
                    std::stoi(ask("  Глубина истории", std::to_string(s.historical_days)));
            else if (line == "3") s.forecast_days =
                    std::stoi(ask("  Горизонт прогноза", std::to_string(s.forecast_days)));
            else if (line == "4") s.anomaly_zscore =
                    std::stod(ask("  Z-порог", std::to_string(s.anomaly_zscore)));
            else if (line == "5") s.anomaly_ma_dev_pct =
                    std::stod(ask("  Отклонение от СС, %", std::to_string(s.anomaly_ma_dev_pct)));
            else if (line == "6") s.anomaly_volume_sigma =
                    std::stod(ask("  Всплеск объёма, σ", std::to_string(s.anomaly_volume_sigma)));
            else if (line == "7") s.forecast_sma_window =
                    std::stoi(ask("  Окно SMA", std::to_string(s.forecast_sma_window)));
            else if (line == "8") s.forecast_ema_alpha =
                    std::stod(ask("  α Холта", std::to_string(s.forecast_ema_alpha)));
            else if (line == "9") s.forecast_lin_reg_win =
                    std::stoi(ask("  Окно регрессии", std::to_string(s.forecast_lin_reg_win)));
            else std::cout << "  Неизвестный пункт.\n";
        } catch (const std::exception&) {
            std::cout << "  Некорректное значение, параметр не изменён.\n";
        }
    }
}

static void executeRun(const RunSettings& s) {
    if (s.pairs.empty()) {
        std::cout << "  Список пар пуст — прогон отменён.\n";
        return;
    }

    DatabaseManager db(Config::instance().dbConnStr());
    db.initializeSchema("sql/schema.sql");

    GateioFetcher gateio(
        Config::instance().gateioBaseUrl(),
        Config::instance().gateioRateLimitMs());

    CoinGeckoFetcher coingecko(
        Config::instance().coingeckoBaseUrl(),
        Config::instance().coingeckoRateLimitMs());

    ETLPipeline etl(db, gateio, coingecko, s.pairs, s.coingecko_ids, s.historical_days);

    StatisticsAnalyzer stats(db);
    VolatilityAnalyzer vol(db);
    CorrelationAnalyzer corr(db);
    AnomalyDetector anom(db,
        s.anomaly_zscore,
        s.anomaly_iqr_mult,
        s.anomaly_ma_dev_pct,
        s.anomaly_volume_sigma);
    Forecaster fcast(db,
        s.forecast_sma_window,
        s.forecast_ema_alpha,
        s.forecast_lin_reg_win,
        s.forecast_days);

    ReportGenerator rg(db, stats, vol, corr, anom, fcast,
                       Config::instance().reportsDir());

    runAll(etl, db, stats, vol, corr, anom, fcast, rg, s.pairs);
}

static int showMainMenu() {
    std::cout << "\n══════════════════════════════════════════════════\n"
              <<   "   Crypto Analytics\n"
              <<   "══════════════════════════════════════════════════\n"
              <<   "  1 — Запуск с настройками по умолчанию (config.json)\n"
              <<   "  2 — Настроить параметры и запустить\n"
              <<   "  0 — Выход\n"
              <<   "══════════════════════════════════════════════════\n"
              <<   "  Выбор: ";
    std::string line;
    std::getline(std::cin, line);
    line = trim(line);
    if (line.empty()) return -1;
    try { return std::stoi(line); } catch (...) { return -1; }
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    std::string cfg_path = "config/config.json";
    for (int i = 1; i < argc; ++i)
        cfg_path = argv[i];

    try {
        Config::instance().load(cfg_path);
        LOG_INFO("Config loaded: " + cfg_path);

        while (true) {
            int choice = showMainMenu();
            if (choice == 0) {
                std::cout << "  Выход.\n";
                return 0;
            }
            if (choice != 1 && choice != 2) {
                std::cout << "  Неизвестный пункт меню.\n";
                continue;
            }

            RunSettings settings = settingsFromConfig();

            if (choice == 2) {
                std::cout << "\n  Настройка прогона (файл config.json не изменяется).\n";
                if (!configureSettings(settings)) continue;
            }

            executeRun(settings);
        }

    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << "\n";
        return 1;
    }
}

// ─── full automatic pipeline ───────────────────────────────────────────────────
void runAll(ETLPipeline& etl, DatabaseManager& db,
            StatisticsAnalyzer& stats, VolatilityAnalyzer& vol,
            CorrelationAnalyzer& corr, AnomalyDetector& anom,
            Forecaster& fcast, ReportGenerator& rg,
            const std::vector<std::string>& pairs) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n══════════════════════════════════════════════════\n"
              <<   "   ПОЛНЫЙ АВТОМАТИЧЕСКИЙ ПРОГОН СИСТЕМЫ\n"
              <<   "══════════════════════════════════════════════════\n";

    std::cout << "\n[1/6] ETL: загрузка свечей, тикеров и капитализации...\n";
    etl.runFull();

    auto all_pairs = db.getAllTradingPairs();
    std::vector<int> pair_ids;
    for (const auto& tp : all_pairs)
        if (tp.exchange_name == "gate.io" &&
            std::find(pairs.begin(), pairs.end(), tp.pair_symbol) != pairs.end())
            pair_ids.push_back(tp.id);

    std::cout << "\n[2/6] Базовая статистика по активам:\n";
    for (int pid : pair_ids) stats.printSummary(pid);

    std::cout << "\n[3/6] Волатильность (дневная / недельная / месячная):\n";
    for (int pid : pair_ids) {
        vol.computeAndSave(pid);
        auto p = db.getTradingPairById(pid);
        std::cout << "  " << p.pair_symbol << ":";
        for (const auto& m : vol.getMetrics(pid)) {
            std::string label = m.period_type == "daily"   ? "дневная" :
                                m.period_type == "weekly"  ? "недельная" :
                                m.period_type == "monthly" ? "месячная" : m.period_type;
            std::cout << "  " << label << "=" << m.volatility * 100 << "%";
        }
        auto ms = vol.getMetrics(pid, "daily");
        if (!ms.empty())
            std::cout << "  σ=" << ms[0].std_dev * 100 << "%";
        std::cout << "\n";
    }

    std::cout << "\n[4/6] Корреляции (окно 30 дней):\n";
    corr.computeAndSave(pair_ids, 30);
    for (const auto& m : corr.getStoredMetrics(30)) {
        auto p1 = db.getTradingPairById(m.pair1_id);
        auto p2 = db.getTradingPairById(m.pair2_id);
        std::cout << "  " << p1.pair_symbol << " <-> " << p2.pair_symbol
                  << "  Pearson=" << m.pearson_correlation
                  << "  Spearman=" << m.spearman_correlation << "\n";
    }

    std::cout << "\n[5/6] Аномалии и прогнозы по каждому активу:\n";
    for (int pid : pair_ids) {
        auto a = anom.detectAndSave(pid);
        auto f = fcast.forecastAndSave(pid);
        auto p = db.getTradingPairById(pid);
        std::cout << "  " << p.pair_symbol
                  << ": аномалий=" << a.size()
                  << ", прогнозных точек=" << f.size() << "\n";
    }

    std::cout << "\n  Лидеры по капитализации:\n";
    for (const auto& r : stats.topByMarketCap(5))
        std::cout << "    " << r.pair.pair_symbol << "  $" << r.value / 1e9 << " млрд\n";
    std::cout << "  Лидеры по доходности (30 дней):\n";
    for (const auto& r : stats.topByReturn(5, 30))
        std::cout << "    " << r.pair.pair_symbol << "  " << r.value << "%\n";

    std::cout << "\n[6/6] Генерация PDF-отчётов...\n";
    rg.generateFullReport(pair_ids);

    std::cout << "\n══════════════════════════════════════════════════\n"
              <<   "   ГОТОВО. Отчёты сохранены в папке reports/run_...\n"
              <<   "══════════════════════════════════════════════════\n";
}
