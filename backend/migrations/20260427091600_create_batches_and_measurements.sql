CREATE TABLE batches (
  device_id   UUID        NOT NULL REFERENCES devices(id),
  batch_id    BIGINT      NOT NULL,
  received_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  PRIMARY KEY (device_id, batch_id)
);

CREATE TABLE measurements (
  id          UUID             PRIMARY KEY DEFAULT gen_random_uuid(),
  device_id   UUID             NOT NULL REFERENCES devices(id),
  batch_id    BIGINT           NOT NULL,
  sensor_type TEXT             NOT NULL,
  kind        TEXT             NOT NULL,
  value       DOUBLE PRECISION NOT NULL,
  sampled_at  TIMESTAMPTZ      NOT NULL,
  received_at TIMESTAMPTZ      NOT NULL DEFAULT NOW(),
  FOREIGN KEY (device_id, batch_id) REFERENCES batches(device_id, batch_id)
);

CREATE INDEX measurements_device_sampled_idx ON measurements (device_id, sampled_at DESC);
