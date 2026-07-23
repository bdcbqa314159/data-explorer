use datawire_shared::Series;
use eframe::egui;
use egui_plot::{Line, Plot, PlotPoints};
use std::sync::{Arc, Mutex};

// Fetched-once state, filled by the ehttp callback (runs on the browser's event
// loop, so a plain Mutex is fine on wasm).
type Loaded = Arc<Mutex<Option<Result<Series, String>>>>;

struct App {
    series: Loaded,
}

impl App {
    fn new(cc: &eframe::CreationContext<'_>) -> Self {
        let series: Loaded = Arc::new(Mutex::new(None));
        let sink = series.clone();
        let ctx = cc.egui_ctx.clone();
        ehttp::fetch(ehttp::Request::get("/api/series/UNRATE"), move |result| {
            let parsed = match result {
                Ok(r) if r.ok => {
                    serde_json::from_slice::<Series>(&r.bytes).map_err(|e| e.to_string())
                }
                Ok(r) => Err(format!("HTTP {}", r.status)),
                Err(e) => Err(e),
            };
            *sink.lock().unwrap() = Some(parsed);
            ctx.request_repaint();
        });
        Self { series }
    }
}

// "YYYY-MM-DD" -> decimal year, for a monotonic time x-axis.
fn to_x(date: &str) -> f64 {
    let y: f64 = date.get(0..4).and_then(|s| s.parse().ok()).unwrap_or(0.0);
    let m: f64 = date.get(5..7).and_then(|s| s.parse().ok()).unwrap_or(1.0);
    y + (m - 1.0) / 12.0
}

impl eframe::App for App {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let guard = self.series.lock().unwrap();
        match &*guard {
            None => {
                ui.heading("datawire");
                ui.label("Loading UNRATE…");
            }
            Some(Err(e)) => {
                ui.heading("datawire");
                ui.colored_label(egui::Color32::RED, format!("Error: {e}"));
            }
            Some(Ok(s)) => {
                ui.heading(format!("{}  ({}, {})", s.meta.title, s.meta.frequency, s.meta.unit));
                let latest = s.observations.last();
                ui.label(format!(
                    "latest {} on {} · {} obs · drag to pan, scroll to zoom, double-click to reset",
                    latest.map(|o| o.value).unwrap_or(f64::NAN),
                    latest.map(|o| o.date.as_str()).unwrap_or("-"),
                    s.observations.len()
                ));
                let points: PlotPoints =
                    s.observations.iter().map(|o| [to_x(&o.date), o.value]).collect();
                Plot::new("series")
                    .allow_zoom(true)
                    .allow_drag(true)
                    .show(ui, |plot_ui| {
                        plot_ui.line(Line::new(s.meta.id.clone(), points));
                    });
            }
        }
    }
}

#[cfg(not(target_arch = "wasm32"))]
fn main() -> eframe::Result<()> {
    eframe::run_native(
        "datawire-web",
        eframe::NativeOptions::default(),
        Box::new(|cc| Ok(Box::new(App::new(cc)))),
    )
}

// Web entry: trunk runs `main`, which starts eframe on the <canvas>. No JS written.
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
                Box::new(|cc| Ok(Box::new(App::new(cc)))),
            )
            .await
            .expect("failed to start eframe");
    });
}
