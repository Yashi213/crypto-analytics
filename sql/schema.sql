-- Crypto Analytics System - Database Schema

CREATE EXTENSION IF NOT EXISTS "pg_stat_statements";

-- Exchanges
CREATE TABLE IF NOT EXISTS exchanges (
    id   SERIAL PRIMARY KEY,
    name VARCHAR(50)  NOT NULL UNIQUE,
    url  VARCHAR(200)
);

-- Crypto assets
CREATE TABLE IF NOT EXISTS assets (
    id       SERIAL PRIMARY KEY,
    symbol   VARCHAR(20)  NOT NULL UNIQUE,
    name     VARCHAR(100) NOT NULL,
    added_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- Trading pairs
CREATE TABLE IF NOT EXISTS trading_pairs (
    id          SERIAL PRIMARY KEY,
    base_asset  VARCHAR(20) NOT NULL,
    quote_asset VARCHAR(20) NOT NULL,
    exchange_id INT NOT NULL REFERENCES exchanges(id) ON DELETE CASCADE,
    pair_symbol VARCHAR(50) NOT NULL,
    UNIQUE(pair_symbol, exchange_id)
);

-- OHLCV candlestick data
CREATE TABLE IF NOT EXISTS candlesticks (
    id          BIGSERIAL PRIMARY KEY,
    pair_id     INT  NOT NULL REFERENCES trading_pairs(id) ON DELETE CASCADE,
    timestamp   TIMESTAMP WITH TIME ZONE NOT NULL,
    open_price  NUMERIC(30, 10) NOT NULL,
    close_price NUMERIC(30, 10) NOT NULL,
    high_price  NUMERIC(30, 10) NOT NULL,
    low_price   NUMERIC(30, 10) NOT NULL,
    volume      NUMERIC(40, 10) NOT NULL,
    UNIQUE(pair_id, timestamp)
);

-- Current/recent market snapshots
CREATE TABLE IF NOT EXISTS market_tickers (
    id                  BIGSERIAL PRIMARY KEY,
    pair_id             INT NOT NULL REFERENCES trading_pairs(id) ON DELETE CASCADE,
    timestamp           TIMESTAMP WITH TIME ZONE NOT NULL,
    last_price          NUMERIC(30, 10),
    market_cap          NUMERIC(40,  2),
    volume_24h          NUMERIC(40, 10),
    price_change_24h_pct NUMERIC(10,  4)
);

-- Computed volatility metrics
CREATE TABLE IF NOT EXISTS volatility_metrics (
    id           BIGSERIAL PRIMARY KEY,
    pair_id      INT NOT NULL REFERENCES trading_pairs(id) ON DELETE CASCADE,
    calculated_at TIMESTAMP WITH TIME ZONE NOT NULL,
    period_type  VARCHAR(20) NOT NULL CHECK (period_type IN ('daily','weekly','monthly')),
    volatility   NUMERIC(20, 10),
    std_dev      NUMERIC(20, 10),
    -- One current row per (pair, period): re-running volatility updates in place.
    CONSTRAINT uq_vol_pair_period UNIQUE(pair_id, period_type)
);

-- Pairwise correlation results
CREATE TABLE IF NOT EXISTS correlation_metrics (
    id                   BIGSERIAL PRIMARY KEY,
    pair1_id             INT NOT NULL REFERENCES trading_pairs(id) ON DELETE CASCADE,
    pair2_id             INT NOT NULL REFERENCES trading_pairs(id) ON DELETE CASCADE,
    calculated_at        TIMESTAMP WITH TIME ZONE NOT NULL,
    pearson_correlation  NUMERIC(10, 8),
    spearman_correlation NUMERIC(10, 8),
    period_days          INT NOT NULL,
    -- One current row per (pair1, pair2, period).
    CONSTRAINT uq_corr_pairs_period UNIQUE(pair1_id, pair2_id, period_days)
);

-- Detected market anomalies
CREATE TABLE IF NOT EXISTS anomalies (
    id                   BIGSERIAL PRIMARY KEY,
    pair_id              INT NOT NULL REFERENCES trading_pairs(id) ON DELETE CASCADE,
    detected_at          TIMESTAMP WITH TIME ZONE NOT NULL,
    anomaly_type         VARCHAR(50) NOT NULL,
    description          TEXT,
    severity             NUMERIC(10, 4),
    price_at_detection   NUMERIC(30, 10),
    volume_at_detection  NUMERIC(40, 10),
    -- One row per (pair, moment, type): re-detecting updates instead of duplicating.
    CONSTRAINT uq_anom_pair_time_type UNIQUE(pair_id, detected_at, anomaly_type)
);

-- Price / volume forecasts
CREATE TABLE IF NOT EXISTS forecasts (
    id               BIGSERIAL PRIMARY KEY,
    pair_id          INT NOT NULL REFERENCES trading_pairs(id) ON DELETE CASCADE,
    created_at       TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    model_type       VARCHAR(50) NOT NULL,
    forecast_for     TIMESTAMP WITH TIME ZONE NOT NULL,
    predicted_price  NUMERIC(30, 10),
    predicted_volume NUMERIC(40, 10),
    confidence_lower NUMERIC(30, 10),
    confidence_upper NUMERIC(30, 10),
    -- One row per (pair, model, target day).
    CONSTRAINT uq_forecast_pair_model_for UNIQUE(pair_id, model_type, forecast_for)
);

-- ETL execution audit log
CREATE TABLE IF NOT EXISTS etl_logs (
    id                BIGSERIAL PRIMARY KEY,
    started_at        TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    completed_at      TIMESTAMP WITH TIME ZONE,
    source            VARCHAR(100),
    operation         VARCHAR(50),
    status            VARCHAR(20) CHECK (status IN ('running','success','failed')),
    records_processed INT DEFAULT 0,
    error_message     TEXT
);

-- Performance indexes
CREATE INDEX IF NOT EXISTS idx_candles_pair_ts    ON candlesticks(pair_id, timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_tickers_pair_ts    ON market_tickers(pair_id, timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_vol_pair_ts        ON volatility_metrics(pair_id, calculated_at DESC);
CREATE INDEX IF NOT EXISTS idx_anomaly_pair_ts    ON anomalies(pair_id, detected_at DESC);
CREATE INDEX IF NOT EXISTS idx_forecast_pair      ON forecasts(pair_id, forecast_for);

-- ─── Migration for pre-existing databases ────────────────────────────────────
-- CREATE TABLE IF NOT EXISTS does not alter tables that already exist, so the
-- UPSERT keys above must be back-filled here. Each step de-duplicates (keeping
-- the freshest row) and then adds the constraint if it is missing. Idempotent:
-- safe to run on every startup.
DO $$
BEGIN
    -- volatility_metrics: keep newest per (pair_id, period_type)
    DELETE FROM volatility_metrics a USING volatility_metrics b
     WHERE a.pair_id = b.pair_id AND a.period_type = b.period_type
       AND (a.calculated_at < b.calculated_at
            OR (a.calculated_at = b.calculated_at AND a.id < b.id));
    ALTER TABLE volatility_metrics
        DROP CONSTRAINT IF EXISTS volatility_metrics_pair_id_calculated_at_period_type_key;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'uq_vol_pair_period') THEN
        ALTER TABLE volatility_metrics
            ADD CONSTRAINT uq_vol_pair_period UNIQUE(pair_id, period_type);
    END IF;

    -- correlation_metrics: keep newest per (pair1_id, pair2_id, period_days)
    DELETE FROM correlation_metrics a USING correlation_metrics b
     WHERE a.pair1_id = b.pair1_id AND a.pair2_id = b.pair2_id
       AND a.period_days = b.period_days
       AND (a.calculated_at < b.calculated_at
            OR (a.calculated_at = b.calculated_at AND a.id < b.id));
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'uq_corr_pairs_period') THEN
        ALTER TABLE correlation_metrics
            ADD CONSTRAINT uq_corr_pairs_period UNIQUE(pair1_id, pair2_id, period_days);
    END IF;

    -- anomalies: keep newest per (pair_id, detected_at, anomaly_type)
    DELETE FROM anomalies a USING anomalies b
     WHERE a.pair_id = b.pair_id AND a.detected_at = b.detected_at
       AND a.anomaly_type = b.anomaly_type AND a.id < b.id;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'uq_anom_pair_time_type') THEN
        ALTER TABLE anomalies
            ADD CONSTRAINT uq_anom_pair_time_type UNIQUE(pair_id, detected_at, anomaly_type);
    END IF;

    -- forecasts: keep newest per (pair_id, model_type, forecast_for)
    DELETE FROM forecasts a USING forecasts b
     WHERE a.pair_id = b.pair_id AND a.model_type = b.model_type
       AND a.forecast_for = b.forecast_for AND a.id < b.id;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'uq_forecast_pair_model_for') THEN
        ALTER TABLE forecasts
            ADD CONSTRAINT uq_forecast_pair_model_for UNIQUE(pair_id, model_type, forecast_for);
    END IF;
END $$;

-- Seed reference data
INSERT INTO exchanges (name, url) VALUES
    ('gate.io',    'https://www.gate.io'),
    ('coingecko',  'https://www.coingecko.com')
ON CONFLICT (name) DO NOTHING;

INSERT INTO assets (symbol, name) VALUES
    ('BTC',  'Bitcoin'),
    ('ETH',  'Ethereum'),
    ('BNB',  'Binance Coin'),
    ('SOL',  'Solana'),
    ('USDT', 'Tether USD')
ON CONFLICT (symbol) DO NOTHING;
