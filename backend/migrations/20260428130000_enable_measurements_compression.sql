ALTER TABLE measurements SET (
  timescaledb.compress,
  timescaledb.compress_orderby = 'sampled_at DESC',
  timescaledb.compress_segmentby = 'device_id, sensor_type'
);

SELECT add_compression_policy('measurements', INTERVAL '1 day');
