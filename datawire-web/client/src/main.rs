use datawire_shared::{AddRequest, Observation, SearchResult, Series, WatchItem};
use eframe::egui;
use egui_plot::{HoverPosition, Line, Plot, PlotPoints};
use std::collections::{HashMap, HashSet};
use std::sync::{Arc, Mutex};

// All shared state lives behind Mutexes, filled by ehttp callbacks (which run on
// the browser event loop, so a plain Mutex never contends on wasm).
type Watchlist = Arc<Mutex<Option<Result<Vec<WatchItem>, String>>>>;
type SeriesCache = Arc<Mutex<HashMap<String, Result<Series, String>>>>;

#[derive(Clone, Copy, PartialEq, Debug)]
enum Window {
    Y1,
    Y5,
    Max,
}

enum SearchState {
    Idle,
    Loading,
    Done(Result<Vec<SearchResult>, String>),
}

type Search = Arc<Mutex<SearchState>>;

struct App {
    watchlist: Watchlist,
    cache: SeriesCache,
    pending: Arc<Mutex<HashSet<String>>>,
    selected: Option<String>,
    window: Window,
    search_open: bool,
    search_text: String,
    search: Search,
    ctx: egui::Context,
}

// One list row's summary, read from the cache.
enum Cell {
    Loading,
    Failed,
    Val(f64, Option<f64>), // latest value, delta vs previous
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
        let app = Self {
            watchlist: Arc::new(Mutex::new(None)),
            cache: Arc::new(Mutex::new(HashMap::new())),
            pending: Arc::new(Mutex::new(HashSet::new())),
            selected: None,
            window: Window::Y5,
            search_open: false,
            search_text: String::new(),
            search: Arc::new(Mutex::new(SearchState::Idle)),
            ctx: cc.egui_ctx.clone(),
        };
        app.reload_watchlist();
        app
    }

    fn reload_watchlist(&self) {
        let (sink, ctx) = (self.watchlist.clone(), self.ctx.clone());
        ehttp::fetch(ehttp::Request::get("/api/watchlist"), move |result| {
            *sink.lock().unwrap() = Some(parse_json::<Vec<WatchItem>>(result));
            ctx.request_repaint();
        });
    }

    fn do_search(&self, query: &str) {
        *self.search.lock().unwrap() = SearchState::Loading;
        let (sink, ctx) = (self.search.clone(), self.ctx.clone());
        let url = format!("/api/search?q={}", url_encode(query));
        ehttp::fetch(ehttp::Request::get(url), move |result| {
            *sink.lock().unwrap() = SearchState::Done(parse_json::<Vec<SearchResult>>(result));
            ctx.request_repaint();
        });
    }

    // POST the id; the server returns the updated watchlist, which we store.
    fn add_signal(&self, id: &str) {
        let body = match serde_json::to_vec(&AddRequest { id: id.to_string(), group: None }) {
            Ok(b) => b,
            Err(_) => return,
        };
        let mut req = ehttp::Request::post("/api/watchlist", body);
        req.headers.insert("Content-Type", "application/json");
        let (sink, ctx) = (self.watchlist.clone(), self.ctx.clone());
        ehttp::fetch(req, move |result| {
            *sink.lock().unwrap() = Some(parse_json::<Vec<WatchItem>>(result));
            ctx.request_repaint();
        });
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

    fn summary_cell(&self, id: &str) -> Cell {
        let cache = self.cache.lock().unwrap();
        match cache.get(id) {
            None => Cell::Loading,
            Some(Err(_)) => Cell::Failed,
            Some(Ok(s)) => match s.observations.last() {
                Some(o) => {
                    let n = s.observations.len();
                    let delta = (n >= 2).then(|| o.value - s.observations[n - 2].value);
                    Cell::Val(o.value, delta)
                }
                None => Cell::Failed,
            },
        }
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

fn fmt_val(v: f64) -> String {
    if v.abs() >= 1000.0 {
        format!("{v:.0}")
    } else {
        format!("{v:.2}")
    }
}

fn url_encode(s: &str) -> String {
    let mut out = String::new();
    for b in s.bytes() {
        match b {
            b'a'..=b'z' | b'A'..=b'Z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => out.push(b as char),
            b' ' => out.push_str("%20"),
            _ => out.push_str(&format!("%{b:02X}")),
        }
    }
    out
}

// "YYYY-MM-DD" minus `years` -> cutoff date string (same suffix).
fn window_cutoff(last: &str, years: i32) -> String {
    if last.len() < 4 {
        return String::new();
    }
    match last[0..4].parse::<i32>() {
        Ok(y) => format!("{}{}", y - years, &last[4..]),
        Err(_) => String::new(),
    }
}

fn windowed_points(obs: &[Observation], window: Window) -> PlotPoints<'static> {
    let years = match window {
        Window::Y1 => Some(1),
        Window::Y5 => Some(5),
        Window::Max => None,
    };
    let cutoff = match (years, obs.last()) {
        (Some(y), Some(o)) => window_cutoff(&o.date, y),
        _ => String::new(),
    };
    obs.iter()
        .filter(|o| cutoff.is_empty() || o.date.as_str() >= cutoff.as_str())
        .map(|o| [to_x(&o.date), o.value])
        .collect()
}

fn render_cell(ui: &mut egui::Ui, cell: Cell) {
    match cell {
        Cell::Loading => {
            ui.weak("…");
        }
        Cell::Failed => {
            ui.weak("—");
        }
        Cell::Val(v, delta) => {
            if let Some(d) = delta {
                let (txt, col) = if d > 0.0 {
                    (format!("▲{:.2}", d.abs()), egui::Color32::from_rgb(0x46, 0xb5, 0x5f))
                } else if d < 0.0 {
                    (format!("▼{:.2}", d.abs()), egui::Color32::from_rgb(0xd0, 0x54, 0x54))
                } else {
                    ("▬".to_string(), egui::Color32::GRAY)
                };
                ui.colored_label(col, txt);
            }
            ui.monospace(fmt_val(v));
        }
    }
}

fn draw_chart(ui: &mut egui::Ui, s: &Series, window: Window) {
    ui.heading(format!("{}  ({}, {})", s.meta.title, s.meta.frequency, s.meta.unit));
    if let Some(o) = s.observations.last() {
        ui.label(format!(
            "latest {} on {} · {} obs · drag to pan, scroll to zoom, double-click to reset",
            fmt_val(o.value),
            o.date,
            s.observations.len()
        ));
    }
    let points = windowed_points(&s.observations, window);
    // id includes the window so switching re-fits instead of keeping stale bounds.
    Plot::new(format!("plot-{}-{:?}", s.meta.id, window))
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

impl eframe::App for App {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        // Left: watchlist, each row showing latest value + Δ.
        let mut clicked: Option<String> = None;
        egui::Panel::left("watchlist")
            .resizable(true)
            .default_size(260.0)
            .show(ui, |ui| {
                ui.horizontal(|ui| {
                    ui.heading("datawire");
                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        if ui.button("＋ Add").clicked() {
                            self.search_open = true;
                        }
                    });
                });
                ui.separator();
                let wl = self.watchlist.lock().unwrap();
                match &*wl {
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
                            // ponytail: eager-load every row so its latest/Δ shows.
                            // Fine for small watchlists; add a /api/board summary if it grows.
                            self.ensure_series(&it.id);
                            let cell = self.summary_cell(&it.id);
                            let is_sel = self.selected.as_deref() == Some(it.id.as_str());
                            ui.horizontal(|ui| {
                                if ui.selectable_label(is_sel, &it.id).clicked() {
                                    clicked = Some(it.id.clone());
                                }
                                ui.with_layout(
                                    egui::Layout::right_to_left(egui::Align::Center),
                                    |ui| render_cell(ui, cell),
                                );
                            });
                        }
                    }
                }
            });
        if let Some(id) = clicked {
            self.ensure_series(&id);
            self.selected = Some(id);
        }

        // Right: window buttons + chart.
        let mut new_window: Option<Window> = None;
        egui::CentralPanel::default().show(ui, |ui| {
            ui.horizontal(|ui| {
                for (label, w) in [("1Y", Window::Y1), ("5Y", Window::Y5), ("MAX", Window::Max)] {
                    if ui.selectable_label(self.window == w, label).clicked() {
                        new_window = Some(w);
                    }
                }
            });
            ui.separator();
            let Some(id) = self.selected.clone() else {
                ui.centered_and_justified(|ui| ui.label("Select a series"));
                return;
            };
            let cache = self.cache.lock().unwrap();
            match cache.get(&id) {
                None => {
                    ui.label(format!("Loading {id}…"));
                }
                Some(Err(e)) => {
                    ui.colored_label(egui::Color32::RED, format!("Error: {e}"));
                }
                Some(Ok(s)) => draw_chart(ui, s, self.window),
            }
        });
        if let Some(w) = new_window {
            self.window = w;
        }

        // Floating "Add signal" search window.
        let mut open = self.search_open;
        let mut run_search: Option<String> = None;
        let mut add_id: Option<String> = None;
        egui::Window::new("Add signal")
            .open(&mut open)
            .resizable(true)
            .default_width(440.0)
            .show(ui.ctx(), |ui| {
                ui.horizontal(|ui| {
                    let resp = ui.text_edit_singleline(&mut self.search_text);
                    let entered =
                        resp.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter));
                    if (ui.button("Search").clicked() || entered) && !self.search_text.trim().is_empty()
                    {
                        run_search = Some(self.search_text.trim().to_string());
                    }
                });
                ui.separator();
                egui::ScrollArea::vertical().max_height(380.0).show(ui, |ui| {
                    match &*self.search.lock().unwrap() {
                        SearchState::Idle => {
                            ui.weak("Search FRED (e.g. \"mortgage rate\") and add to your board.");
                        }
                        SearchState::Loading => {
                            ui.weak("Searching…");
                        }
                        SearchState::Done(Err(e)) => {
                            ui.colored_label(egui::Color32::RED, format!("Error: {e}"));
                        }
                        SearchState::Done(Ok(rs)) if rs.is_empty() => {
                            ui.weak("No results.");
                        }
                        SearchState::Done(Ok(rs)) => {
                            for r in rs {
                                ui.horizontal(|ui| {
                                    if ui.button("＋").clicked() {
                                        add_id = Some(r.id.clone());
                                    }
                                    ui.monospace(&r.id);
                                    ui.weak(format!("{} · {}", r.frequency, r.unit));
                                });
                                ui.label(&r.title);
                                ui.add_space(4.0);
                            }
                        }
                    }
                });
            });
        self.search_open = open;
        if let Some(q) = run_search {
            self.do_search(&q);
        }
        if let Some(id) = add_id {
            self.add_signal(&id);
            self.ensure_series(&id); // start loading its data for the row/chart
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
