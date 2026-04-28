-- Enable TimescaleDB extension for hypertable support and time-series optimizations
CREATE EXTENSION IF NOT EXISTS timescaledb CASCADE;

-- Convert measurements table to hypertable partitioned by sampled_at with 7-day chunks
SELECT create_hypertable('measurements', 'sampled_at',
  if_not_exists => TRUE,
  chunk_time_interval => INTERVAL '7 days'
);

-- Drop generic index in favor of composite index tuned to actual query patterns
DROP INDEX IF EXISTS measurements_device_sampled_idx;

-- Composite index optimized for findLatestMeasurements query:
-- WHERE device_id = ? ORDER BY sensor_type, kind, sampled_at DESC
-- TimescaleDB will benefit from (device_id, sensor_type, kind) leading columns
-- for efficient filtering and sorting on the leading conditions
CREATE INDEX measurements_device_sensor_kind_sampled_idx ON measurements
  (device_id, sensor_type, kind, sampled_at DESC);
