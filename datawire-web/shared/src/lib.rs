use serde::{Deserialize, Serialize};

// The one model, shared by server (serialize) and client (deserialize) so the
// JSON contract can't drift. Mirrors the C++ datawire `Series`.

#[derive(Clone, Serialize, Deserialize)]
pub struct Observation {
    pub date: String, // YYYY-MM-DD
    pub value: f64,
}

#[derive(Clone, Default, Serialize, Deserialize)]
pub struct SeriesMeta {
    pub id: String,
    pub title: String,
    pub unit: String,
    pub frequency: String,
    pub seasonal_adj: String,
    pub as_of: String,
    pub source_url: String,
}

#[derive(Clone, Default, Serialize, Deserialize)]
pub struct Series {
    pub meta: SeriesMeta,
    pub observations: Vec<Observation>,
}

#[derive(Clone, Serialize, Deserialize)]
pub struct WatchItem {
    pub group: String,
    pub id: String,
}

#[derive(Clone, Serialize, Deserialize)]
pub struct SearchResult {
    pub id: String,
    pub title: String,
    pub unit: String,
    pub frequency: String,
}

#[derive(Clone, Serialize, Deserialize)]
pub struct AddRequest {
    pub id: String,
    pub group: Option<String>,
}
