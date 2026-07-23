use eframe::egui;
use egui_plot::{Line, Plot, PlotPoints};

// De-risk slice: prove egui_plot renders an interactive chart (zoom/pan) over a
// sample series. Real FRED data + the axum proxy come next. Native entry only
// for now; the WASM boot is added once the toolchain (trunk) is ready.
struct App;

impl App {
    fn new(_cc: &eframe::CreationContext<'_>) -> Self {
        Self
    }
}

impl eframe::App for App {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        ui.heading("datawire — sample series (drag to pan, scroll to zoom)");
        let points: PlotPoints = (0..300)
            .map(|i| {
                let x = i as f64 * 0.1;
                [x, x.sin() * (1.0 + x * 0.03)]
            })
            .collect();
        Plot::new("sample")
            .allow_zoom(true)
            .allow_drag(true)
            .show(ui, |plot_ui| {
                plot_ui.line(Line::new("sample", points));
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
