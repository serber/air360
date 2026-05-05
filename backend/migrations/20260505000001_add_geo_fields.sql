ALTER TABLE devices
  ADD COLUMN IF NOT EXISTS geo_country      TEXT,
  ADD COLUMN IF NOT EXISTS geo_country_code TEXT,
  ADD COLUMN IF NOT EXISTS geo_city         TEXT,
  ADD COLUMN IF NOT EXISTS geo_display      TEXT;

CREATE TABLE IF NOT EXISTS geo_update_queue (
  device_id INTEGER PRIMARY KEY REFERENCES devices(device_id) ON DELETE CASCADE,
  queued_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
