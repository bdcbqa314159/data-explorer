use datawire_shared::{Series, WatchItem};
use eframe::egui;
use egui_plot::{HoverPosition, Line, Plot, PlotPoints};
use std::collections::{HashMap, HashSet};
use std::sync::{Arc, Mutex};

// All shared state lives behind Mutexes, filled by ehttp callbacks (which run on
// the browser event loop, so a plain Mutex never contends on wasm).
type Watchlist = Arc<Mutex<Option<Result<Vec<WatchItem>, String>>>>;
type SeriesCache = Arc<Mutex<HashMap<String, Result<Series, String>>>>;

struct App {
    watchlist: Watchlist,
    cache: SeriesCache,
    pending: Arc<Mutex<HashSet<String>>>,
    selected: Option<String>,
    ctx: egui::Context,
}

fn parse_json<T: serde::de::DeserializeOwned>(
    result: Result<ehttp::Response, String>,
) -> Result<T, String> {
    match result {
        Ok(r) if r.ok => serde_json::from_slice::<T>(&r.bytes).map_err(|e| e.to_string()),
        Ok(r) => Err(format!("HTTP {}", r.status)),
        Err(e) => Err(e),
    }
}

impl App {
    fn new(cc: &eframe::CreationContext<'_>) -> Self {
        let ctx = cc.egui_ctx.clone();
        let watchlist: Watchlist = Arc::new(Mutex::new(None));
        let sink = watchlist.clone();
        let ctx2 = ctx.clone();
        ehttp::fetch(ehttp::Request::get("/api/watchlist"), move |result| {
            *sink.lock().unwrap() = Some(parse_json::<Vec<WatchItem>>(result));
            ctx2.request_repaint();
        });
        Self {
            watchlist,
            cache: Arc::new(Mutex::new(HashMap::new())),
            pending: Arc::new(Mutex::new(HashSet::new())),
            selected: None,
            ctx,
        }
    }

    // Kick off a fetch for `id` unless it's already loaded or in flight.
    fn ensure_series(&self, id: &str) {
        if self.cache.lock().unwrap().contains_key(id) {
            return;
        }
        if !self.pending.lock().unwrap().insert(id.to_string()) {
            return; // already fetching
        }
        let (cache, pending, ctx, id_owned) =
            (self.cache.clone(), self.pending.clone(), self.ctx.clone(), id.to_string());
        ehttp::fetch(ehttp::Request::get(format!("/api/series/{id}")), move |result| {
            cache.lock().unwrap().insert(id_owned.clone(), parse_json::<Series>(result));
            pending.lock().unwrap().remove(&id_owned);
            ctx.request_repaint();
        });
    }
}

// "YYYY-MM-DD" -> decimal year, for a monotonic time x-axis.
fn to_x(date: &str) -> f64 {
    let y: f64 = date.get(0..4).and_then(|s| s.parse().ok()).unwrap_or(0.0);
    let m: f64 = date.get(5..7).and_then(|s| s.parse().ok()).unwrap_or(1.0);
    y + (m - 1.0) / 12.0
}

// decimal year -> (year, month) for axis/hover labels.
fn x_to_ym(x: f64) -> (i32, i32) {
    let year = x.floor() as i32;
    let month = (((x - year as f64) * 12.0).round() as i32 + 1).clamp(1, 12);
    (year, month)
}

impl eframe::App for App {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        // Left: the watchlist.
        let mut clicked: Option<String> = None;
        egui::Panel::left("watchlist")
            .resizable(true)
            .default_size(230.0)
            .show(ui, |ui| {
                ui.heading("datawire");
                ui.separator();
                match &*self.watchlist.lock().unwrap() {
                    None => {
                        ui.label("Loading watchlist…");
                    }
                    Some(Err(e)) => {
                        ui.colored_label(egui::Color32::RED, format!("Error: {e}"));
                    }
                    Some(Ok(items)) if items.is_empty() => {
                        ui.label("No signals in watchlist.txt");
                    }
                    Some(Ok(items)) => {
                        let mut group = String::new();
                        for it in items {
                            if it.group != group {
                                group = it.group.clone();
                                if !group.is_empty() {
                                    ui.add_space(6.0);
                                    ui.label(egui::RichText::new(&group).weak());
                                }
                            }
                            let is_sel = self.selected.as_deref() == Some(it.id.as_str());
                            if ui.selectable_label(is_sel, &it.id).clicked() {
                                clicked = Some(it.id.clone());
                            }
                        }
                    }
                }
            });
        if let Some(id) = clicked {
            self.ensure_series(&id);
            self.selected = Some(id);
        }

        // Right: the selected series' chart.
        egui::CentralPanel::default().show(ui, |ui| {
            let Some(id) = self.selected.clone() else {
                ui.centered_and_justified(|ui| ui.label("Select a series"));
                return;
            };
            self.ensure_series(&id);
            match self.cache.lock().unwrap().get(&id) {
                None => {
                    ui.label(format!("Loading {id}…"));
                }
                Some(Err(e)) => {
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
                    // Per-series id so each series keeps its own view (fixes the
                    // "wrong scale" from inheriting the previous series' zoom).
                    Plot::new(format!("plot-{}", s.meta.id))
                        .allow_zoom(true)
                        .allow_drag(true)
                        .y_axis_label(s.meta.unit.clone())
                        .x_axis_formatter(|mark, range| {
                            let span = *range.end() - *range.start();
                            let (y, m) = x_to_ym(mark.value);
                            if span <= 3.0 {
                                format!("{y}-{m:02}")
                            } else {
                                format!("{y}")
                            }
                        })
                        .label_formatter(|pos| {
                            let (name, p) = match pos {
                                HoverPosition::NearDataPoint { plot_name, position, .. } => {
                                    (Some(*plot_name), *position)
                                }
                                HoverPosition::Elsewhere { position } => (None, *position),
                            };
                            let (y, m) = x_to_ym(p.x);
                            Some(match name {
                                Some(n) => format!("{n}\n{y}-{m:02}:  {:.2}", p.y),
                                None => format!("{y}-{m:02}:  {:.2}", p.y),
                            })
                        })
                        .show(ui, |plot_ui| {
                            plot_ui.line(Line::new(s.meta.id.clone(), points));
                        });
                }
            }
        });
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
