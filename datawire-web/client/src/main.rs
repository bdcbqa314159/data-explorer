use datawire_client::App;

// Native entry: point at a running server (default localhost:8080). Override with
// DATAWIRE_API. The self-contained desktop app lives in the `desktop` crate.
#[cfg(not(target_arch = "wasm32"))]
fn main() -> eframe::Result<()> {
    let base = std::env::var("DATAWIRE_API").unwrap_or_else(|_| "http://127.0.0.1:8080".to_string());
    eframe::run_native(
        "datawire",
        eframe::NativeOptions::default(),
        Box::new(move |cc| Ok(Box::new(App::new(cc, base.clone())))),
    )
}

// Web entry: trunk runs `main`, which starts eframe on the <canvas>. Base "" =
// relative urls, served by the same origin. No JS written.
#[cfg(target_arch = "wasm32")]
fn main() {
    use eframe::wasm_bindgen::JsCast as _;
    wasm_bindgen_futures::spawn_local(async {
        let document = web_sys::window().unwrap().document().unwrap();
        let canvas = document
            .get_element_by_id("the_canvas_id")
            .expect("missing <canvas id=\"the_canvas_id\">")
            .dyn_into::<web_sys::HtmlCanvasElement>()
            .unwrap();
        eframe::WebRunner::new()
            .start(
                canvas,
                eframe::WebOptions::default(),
                Box::new(|cc| Ok(Box::new(App::new(cc, String::new())))),
            )
            .await
            .expect("failed to start eframe");
    });
}
