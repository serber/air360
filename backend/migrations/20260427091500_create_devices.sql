CREATE TABLE devices (
  device_id        BIGINT        PRIMARY KEY,
  public_id        UUID          NOT NULL UNIQUE DEFAULT gen_random_uuid(),
  name             TEXT          NOT NULL,
  latitude         NUMERIC(9,6)  NOT NULL,
  longitude        NUMERIC(9,6)  NOT NULL,
  firmware_version TEXT          NOT NULL,
  registered_at    TIMESTAMPTZ   NOT NULL DEFAULT NOW(),
  last_seen_at     TIMESTAMPTZ   NOT NULL DEFAULT NOW()
);
