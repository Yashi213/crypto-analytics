-- Run as PostgreSQL superuser BEFORE building:
--   psql -U postgres -f setup_db.sql

CREATE DATABASE crypto_analytics
    WITH ENCODING = 'UTF8'
         LC_COLLATE = 'C'
         LC_CTYPE   = 'C'
         TEMPLATE   = template0;

\connect crypto_analytics

-- Now run the schema
\i sql/schema.sql
