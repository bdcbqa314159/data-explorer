use datawire_client::App;
use datawire_server::{api_router, credentials_path, load_api_key, AppState};
use std::path::Path;

// Self-contained native desktop app: it runs the same /api server in-process on a
// localhost port and points the same egui UI at it. One offline binary, every
// feature (select, window, hover, search, add) — no browser, no separate server.
fn main() -> eframe::Result<()> {
    let key = load_api_key().unwrap_or_else(|| {
        eprintln!("No FRED API key. Set FRED_API_KEY or store it with `datawire key set`.");
        std::process::exit(1);
    });

    let watchlist_path =
        std::env::var("DATAWIRE_WATCHLIST").unwrap_or_else(|_| default_watchlist_path());
    seed_watchlist_if_missing(&watchlist_path);

    // Start the embedded server on an ephemeral port in a background thread, and
    // learn the port it bound to.
    let (tx, rx) = std::sync::mpsc::channel();
    std::thread::spawn(move || {
        let rt = tokio::runtime::Runtime::new().expect("tokio runtime");
        rt.block_on(async move {
            let listener = tokio::net::TcpListener::bind("127.0.0.1:0")
                .await
                .expect("bind localhost");
            tx.send(listener.local_addr().unwrap().port()).unwrap();
            let app = api_router().with_state(AppState::new(key, watchlist_path));
            axum::serve(listener, app).await.unwrap();
        });
    });
    let port = rx.recv().expect("server port");
    let base = format!("http://127.0.0.1:{port}");

    eframe::run_native(
        "datawire",
        eframe::NativeOptions::default(),
        Box::new(move |cc| Ok(Box::new(App::new(cc, base.clone())))),
    )
}

// Per-user watchlist, next to the shared credentials file.
fn default_watchlist_path() -> String {
    match credentials_path() {
        Some(cred) => Path::new(&cred)
            .parent()
            .map(|p| p.join("watchlist.txt").to_string_lossy().into_owned())
            .unwrap_or_else(|| "watchlist.txt".to_string()),
        None => "watchlist.txt".to_string(),
    }
}

fn seed_watchlist_if_missing(path: &str) {
    if Path::new(path).exists() {
        return;
    }
    if let Some(parent) = Path::new(path).parent() {
        let _ = std::fs::create_dir_all(parent);
    }
    let _ = std::fs::write(
        path,
        "# GROWTH & LABOR\nUNRATE\nPAYEMS\n# PRICES\nCPIAUCSL\n",
    );
}
