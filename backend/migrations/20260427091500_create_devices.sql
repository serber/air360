CREATE TABLE devices (
  device_id        BIGINT        PRIMARY KEY,
  name             TEXT          NOT NULL,
  latitude         NUMERIC(9,6)  NOT NULL,
  longitude        NUMERIC(9,6)  NOT NULL,
  firmware_version TEXT          NOT NULL,
  registered_at    TIMESTAMPTZ   NOT NULL DEFAULT NOW(),
  last_seen_at     TIMESTAMPTZ   NOT NULL DEFAULT NOW()
);
