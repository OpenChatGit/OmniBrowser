-- QSearch owned document index
CREATE TABLE IF NOT EXISTS documents (
  id TEXT PRIMARY KEY,
  url TEXT NOT NULL UNIQUE,
  title TEXT NOT NULL,
  snippet TEXT NOT NULL DEFAULT '',
  domain TEXT NOT NULL DEFAULT '',
  fetched_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_documents_domain ON documents(domain);
CREATE INDEX IF NOT EXISTS idx_documents_title ON documents(title);
