use axum::{
    extract::{Path, Query, State},
    http::StatusCode,
    routing::get,
    Json, Router,
};
use datawire_shared::{AddRequest, Observation, SearchResult, Series, SeriesMeta, WatchItem};
use std::sync::Arc;
use tower_http::services::ServeDir;

#[derive(Clone)]
struct AppState {
    key: Arc<String>,
    http: reqwest::Client,
    watchlist_path: Arc<String>,
}

#[tokio::main]
async fn main() {
    let key = load_api_key().unwrap_or_else(|| {
        eprintln!(
            "No FRED API key. Set FRED_API_KEY or store it via the C++ tool \
             (`datawire key set`) at ~/.config/datawire/credentials."
        );
        std::process::exit(1);
    });

    // Reuse the same watchlist the C++ tool edits (shared contract, not an ABI).
    // Read fresh on each request so adds are reflected immediately.
    let watchlist_path = std::env::var("DATAWIRE_WATCHLIST")
        .unwrap_or_else(|_| format!("{}/../../datawire/watchlist.txt", env!("CARGO_MANIFEST_DIR")));
    println!("watchlist: {} signals from {watchlist_path}", load_watchlist(&watchlist_path).len());

    let state = AppState {
        key: Arc::new(key),
        http: reqwest::Client::new(),
        watchlist_path: Arc::new(watchlist_path),
    };

    // Resolve the WASM dir relative to this crate (not the invocation cwd), so
    // `cargo run` works from anywhere. Override with DATAWIRE_DIST if needed.
    let dist = std::env::var("DATAWIRE_DIST")
        .unwrap_or_else(|_| format!("{}/../client/dist", env!("CARGO_MANIFEST_DIR")));
    if !std::path::Path::new(&dist).join("index.html").exists() {
        eprintln!("warning: {dist}/index.html not found — run `trunk build` in client/ first");
    }

    let app = Router::new()
        .route("/api/watchlist", get(watchlist_handler).post(add_handler))
        .route("/api/search", get(search_handler))
        .route("/api/series/:id", get(series_handler))
        .fallback_service(ServeDir::new(&dist))
        .with_state(state);

    let addr = "127.0.0.1:8080";
    println!("datawire-server on http://{addr}  (serving {dist})");
    let listener = tokio::net::TcpListener::bind(addr).await.unwrap();
    axum::serve(listener, app).await.unwrap();
}

async fn watchlist_handler(State(st): State<AppState>) -> Json<Vec<WatchItem>> {
    Json(load_watchlist(&st.watchlist_path))
}

// POST /api/watchlist {id, group?} -> append to the file, return the new list.
async fn add_handler(State(st): State<AppState>, Json(req): Json<AddRequest>) -> Json<Vec<WatchItem>> {
    add_to_watchlist(&st.watchlist_path, &req.id, req.group.as_deref());
    Json(load_watchlist(&st.watchlist_path))
}

// Append `id` under `group` (default "Added"), skipping duplicates. Only accepts
// FRED-shaped ids so nothing dangerous is written to the file.
fn add_to_watchlist(path: &str, id: &str, group: Option<&str>) {
    let id = id.trim();
    if id.is_empty()
        || !id.chars().all(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '_' | '-'))
    {
        return;
    }
    let content = std::fs::read_to_string(path).unwrap_or_default();
    if content.lines().any(|l| l.trim() == id) {
        return; // already present
    }
    let group = group.unwrap_or("Added");
    let last_group = content
        .lines()
        .rev()
        .find_map(|l| l.trim().strip_prefix('#').map(|g| g.trim().to_string()));

    let mut out = content;
    if !out.is_empty() && !out.ends_with('\n') {
        out.push('\n');
    }
    if last_group.as_deref() != Some(group) {
        out.push_str(&format!("# {group}\n"));
    }
    out.push_str(&format!("{id}\n"));
    let _ = std::fs::write(path, out);
}

#[derive(serde::Deserialize)]
struct SearchQuery {
    q: String,
}

async fn search_handler(
    State(st): State<AppState>,
    Query(query): Query<SearchQuery>,
) -> Result<Json<Vec<SearchResult>>, (StatusCode, String)> {
    fred_search(&st, &query.q)
        .await
        .map(Json)
        .map_err(|e| (StatusCode::BAD_GATEWAY, e))
}

async fn fred_search(st: &AppState, query: &str) -> Result<Vec<SearchResult>, String> {
    let v: serde_json::Value = st
        .http
        .get("https://api.stlouisfed.org/fred/series/search")
        .query(&[
            ("search_text", query),
            ("api_key", st.key.as_str()),
            ("file_type", "json"),
            ("order_by", "popularity"),
            ("sort_order", "desc"),
            ("limit", "25"),
        ])
        .send()
        .await
        .map_err(|e| e.to_string())?
        .json()
        .await
        .map_err(|e| e.to_string())?;

    let mut out = Vec::new();
    if let Some(arr) = v.get("seriess").and_then(|x| x.as_array()) {
        for s in arr {
            let get = |k: &str| s.get(k).and_then(|x| x.as_str()).unwrap_or("").to_string();
            let short_or = |short: &str, long: &str| {
                let v = get(short);
                if v.is_empty() { get(long) } else { v }
            };
            out.push(SearchResult {
                id: get("id"),
                title: get("title"),
                unit: short_or("units_short", "units"),
                frequency: short_or("frequency_short", "frequency"),
            });
        }
    }
    Ok(out)
}

// Watchlist file: "# Group" lines start a group; other non-blank lines are ids.
fn load_watchlist(path: &str) -> Vec<WatchItem> {
    let content = match std::fs::read_to_string(path) {
        Ok(c) => c,
        Err(_) => return Vec::new(),
    };
    let mut out = Vec::new();
    let mut group = String::new();
    for raw in content.lines() {
        let line = raw.trim();
        if line.is_empty() {
            continue;
        }
        if let Some(g) = line.strip_prefix('#') {
            group = g.trim().to_string();
            continue;
        }
        let id = line.split_whitespace().next().unwrap_or("").to_string();
        if !id.is_empty() {
            out.push(WatchItem {
                group: group.clone(),
                id,
            });
        }
    }
    out
}

async fn series_handler(
    State(st): State<AppState>,
    Path(id): Path<String>,
) -> Result<Json<Series>, (StatusCode, String)> {
    fetch_series(&st, &id)
        .await
        .map(Json)
        .map_err(|e| (StatusCode::BAD_GATEWAY, e))
}

// Proxy + normalise FRED into the shared Series (the key never reaches the client).
async fn fetch_series(st: &AppState, id: &str) -> Result<Series, String> {
    let base = "https://api.stlouisfed.org/fred/";
    let meta_url =
        format!("{base}series?series_id={id}&api_key={}&file_type=json", st.key);
    let obs_url = format!(
        "{base}series/observations?series_id={id}&api_key={}&file_type=json&sort_order=asc",
        st.key
    );

    let meta_v: serde_json::Value = st
        .http
        .get(&meta_url)
        .send()
        .await
        .map_err(|e| e.to_string())?
        .json()
        .await
        .map_err(|e| e.to_string())?;
    let obs_v: serde_json::Value = st
        .http
        .get(&obs_url)
        .send()
        .await
        .map_err(|e| e.to_string())?
        .json()
        .await
        .map_err(|e| e.to_string())?;

    let mut meta = SeriesMeta::default();
    if let Some(s) = meta_v.get("seriess").and_then(|a| a.get(0)) {
        let get = |k: &str| s.get(k).and_then(|v| v.as_str()).unwrap_or("").to_string();
        meta.id = get("id");
        meta.title = get("title");
        meta.unit = {
            let short = get("units_short");
            if short.is_empty() { get("units") } else { short }
        };
        meta.frequency = get("frequency");
        meta.seasonal_adj = get("seasonal_adjustment_short");
        meta.as_of = get("observation_end");
    }
    if meta.id.is_empty() {
        meta.id = id.to_string();
    }
    meta.source_url = format!("https://fred.stlouisfed.org/series/{}", meta.id);

    let mut observations = Vec::new();
    if let Some(arr) = obs_v.get("observations").and_then(|v| v.as_array()) {
        for o in arr {
            let val = o.get("value").and_then(|v| v.as_str()).unwrap_or(".");
            if val == "." {
                continue; // FRED marks missing values with a dot
            }
            if let Ok(value) = val.parse::<f64>() {
                let date = o.get("date").and_then(|v| v.as_str()).unwrap_or("").to_string();
                observations.push(Observation { date, value });
            }
        }
    }

    Ok(Series { meta, observations })
}

// FRED key: env FRED_API_KEY, else the shared credentials file the C++ tool writes.
fn load_api_key() -> Option<String> {
    if let Ok(k) = std::env::var("FRED_API_KEY") {
        if !k.is_empty() {
            return Some(k);
        }
    }
    let home = std::env::var("HOME").ok()?;
    let path = format!("{home}/.config/datawire/credentials");
    let content = std::fs::read_to_string(path).ok()?;
    for line in content.lines() {
        if let Some(v) = line.strip_prefix("FRED_API_KEY=") {
            let v = v.trim();
            if !v.is_empty() {
                return Some(v.to_string());
            }
        }
    }
    None
}
