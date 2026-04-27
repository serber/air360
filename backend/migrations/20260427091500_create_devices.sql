CREATE TABLE devices (
  id               UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
  chip_id          BIGINT      NOT NULL UNIQUE,
  name             TEXT        NOT NULL,
  latitude         NUMERIC(9,6)  NOT NULL,
  longitude        NUMERIC(9,6)  NOT NULL,
  firmware_version TEXT        NOT NULL,
  registered_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  last_seen_at     TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
