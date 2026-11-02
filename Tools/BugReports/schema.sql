CREATE TABLE IF NOT EXISTS reports (
  id TEXT PRIMARY KEY,
  received_at INTEGER NOT NULL,
  ip_hash TEXT NOT NULL,
  payload_hash TEXT NOT NULL,
  state TEXT NOT NULL CHECK (state IN ('pending', 'ready')),
  attempts INTEGER NOT NULL DEFAULT 1,
  title TEXT NOT NULL,
  kind TEXT NOT NULL,
  launcher_version TEXT NOT NULL,
  engine_version TEXT NOT NULL,
  object_keys TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS reports_received ON reports(received_at);
CREATE INDEX IF NOT EXISTS reports_ip_received ON reports(ip_hash, received_at);
