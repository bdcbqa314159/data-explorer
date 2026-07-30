-- datawire-server schema. Mirrors the terminal's local SQLite store so the
-- server is the shared, canonical copy the SDK syncs against.
-- Loaded automatically by the server on startup (CREATE ... IF NOT EXISTS);
-- also usable by hand: mysql -u root < sql/init.sql

CREATE DATABASE IF NOT EXISTS datawire;
USE datawire;

CREATE TABLE IF NOT EXISTS series (
  id           VARCHAR(64) PRIMARY KEY,
  title        TEXT,
  unit         VARCHAR(64),
  frequency    VARCHAR(64),
  seasonal_adj VARCHAR(64),
  as_of        VARCHAR(32),
  source_url   TEXT,
  source       VARCHAR(32),
  fetched_at   BIGINT            -- unix seconds
);

CREATE TABLE IF NOT EXISTS observation (
  series_id VARCHAR(64) NOT NULL,
  obs_date  VARCHAR(16) NOT NULL,  -- ISO YYYY-MM-DD ("date" is reserved)
  value     DOUBLE,
  PRIMARY KEY (series_id, obs_date)
);
