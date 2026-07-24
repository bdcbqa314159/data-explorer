use datawire_server::{api_router, load_api_key, load_watchlist, AppState};
use tower_http::services::ServeDir;

#[tokio::main]
async fn main() {
    let key = load_api_key().unwrap_or_else(|| {
        eprintln!(
            "No FRED API key. Set FRED_API_KEY or store it via the C++ tool \
             (`datawire key set`)."
        );
        std::process::exit(1);
    });

    // Reuse the same watchlist the C++ tool edits (shared contract, not an ABI).
    let watchlist_path = std::env::var("DATAWIRE_WATCHLIST")
        .unwrap_or_else(|_| format!("{}/../../datawire/watchlist.txt", env!("CARGO_MANIFEST_DIR")));
    println!("watchlist: {} signals from {watchlist_path}", load_watchlist(&watchlist_path).len());

    // Resolve the WASM dir relative to this crate, not the invocation cwd.
    let dist = std::env::var("DATAWIRE_DIST")
        .unwrap_or_else(|_| format!("{}/../client/dist", env!("CARGO_MANIFEST_DIR")));
    if !std::path::Path::new(&dist).join("index.html").exists() {
        eprintln!("warning: {dist}/index.html not found — run `trunk build` in client/ first");
    }

    let app = api_router()
        .fallback_service(ServeDir::new(&dist))
        .with_state(AppState::new(key, watchlist_path));

    let addr = "127.0.0.1:8080";
    println!("datawire-server on http://{addr}  (serving {dist})");
    let listener = tokio::net::TcpListener::bind(addr).await.unwrap();
    axum::serve(listener, app).await.unwrap();
}
