CREATE TABLE batches (
  id              UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
  device_id       UUID        NOT NULL REFERENCES devices(id),
  client_batch_id TEXT        NOT NULL,
  received_at     TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  UNIQUE (device_id, client_batch_id)
);

CREATE TABLE measurements (
  id          UUID             PRIMARY KEY DEFAULT gen_random_uuid(),
  device_id   UUID             NOT NULL REFERENCES devices(id),
  batch_id    UUID             NOT NULL REFERENCES batches(id),
  sensor_type TEXT             NOT NULL,
  kind        TEXT             NOT NULL,
  value       DOUBLE PRECISION NOT NULL,
  sampled_at  TIMESTAMPTZ      NOT NULL,
  received_at TIMESTAMPTZ      NOT NULL DEFAULT NOW()
);

CREATE INDEX measurements_device_sampled_idx ON measurements (device_id, sampled_at DESC);

INSERT INTO schema_migrations (version) VALUES ('20260427091600_create_batches_and_measurements');
