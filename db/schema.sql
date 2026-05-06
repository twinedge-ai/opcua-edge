CREATE TABLE IF NOT EXISTS plants (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS assets (
    id TEXT PRIMARY KEY,
    plant_id TEXT NOT NULL,
    parent_id TEXT,
    type TEXT NOT NULL,
    name TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS nodes (
    node_key TEXT PRIMARY KEY,
    id TEXT NOT NULL,
    asset_id TEXT NOT NULL,
    name TEXT NOT NULL,
    data_type INTEGER NOT NULL,
    writable INTEGER NOT NULL,
    modbus_unit INTEGER,
    modbus_register INTEGER,
    scale REAL NOT NULL
);

CREATE TABLE IF NOT EXISTS event_definitions (
    id TEXT PRIMARY KEY,
    asset_id TEXT NOT NULL,
    source_node_key TEXT NOT NULL,
    condition_type INTEGER NOT NULL,
    threshold REAL NOT NULL,
    severity INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS event_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id TEXT NOT NULL,
    value REAL NOT NULL,
    threshold REAL NOT NULL,
    severity INTEGER NOT NULL,
    created_ms INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS latest_values (
    node_id TEXT PRIMARY KEY,
    double_value REAL NOT NULL,
    updated_ms INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS benchmark_runs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    benchmark_name TEXT NOT NULL,
    tag_count INTEGER NOT NULL,
    asset_count INTEGER NOT NULL,
    event_count INTEGER NOT NULL,
    duration_us INTEGER NOT NULL DEFAULT 0,
    startup_us INTEGER NOT NULL DEFAULT 0,
    reads_per_second INTEGER NOT NULL,
    writes_per_second INTEGER NOT NULL,
    events_per_second INTEGER NOT NULL,
    avg_latency_us INTEGER NOT NULL DEFAULT 0,
    p95_latency_us INTEGER NOT NULL DEFAULT 0,
    p99_latency_us INTEGER NOT NULL DEFAULT 0,
    cpu_percent_avg REAL NOT NULL DEFAULT 0.0,
    rss_memory_kb_start INTEGER NOT NULL DEFAULT 0,
    rss_memory_kb_peak INTEGER NOT NULL,
    rss_memory_kb_end INTEGER NOT NULL DEFAULT 0,
    error_count INTEGER NOT NULL DEFAULT 0,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
