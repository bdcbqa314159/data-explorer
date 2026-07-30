#include "key_setup.hpp"

#include "fred.hpp"
#include "http_client.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <atomic>
#include <future>
#include <mutex>
#include <string>
#include <vector>

namespace datawire {

using namespace ftxui;

namespace {

// Validate a key with one lightweight FRED metadata request (no observations).
// httpGet throws on HTTP >= 400, so a bad key surfaces as a caught error.
struct KeyCheck {
  bool ok = false;
  std::string detail;  // reason on failure (HTTP/network message)
};

KeyCheck validateApiKey(const std::string& key) {
  try {
    const std::string body = httpGet(fredSeriesUrl("GDP", key), 15);
    if (parseSeriesMeta(body).id.empty()) return {false, "unexpected response from FRED"};
    return {true, ""};
  } catch (const std::exception& ex) {
    return {false, ex.what()};
  }
}

}  // namespace

int runKeySetup(SecretStore& store) {
  CurlGlobal curl;  // libcurl init for the validation request
  auto screen = ScreenInteractive::Fullscreen();

  std::string entry;
  std::atomic<int> phase{0};  // 0 typing · 1 validating · 2 valid+saved · 3 invalid
  std::string detail;
  std::mutex mu;  // guards `detail` (written by the worker thread)
  std::vector<std::future<void>> tasks;

  auto validateAsync = [&] {
    phase = 1;
    tasks.push_back(std::async(std::launch::async, [&, key = entry] {
      const KeyCheck r = validateApiKey(key);
      {
        std::lock_guard<std::mutex> lk(mu);
        detail = r.detail;
      }
      if (r.ok) {
        store.set("FRED_API_KEY", key);  // persist ONLY on green
        phase = 2;
      } else {
        phase = 3;
      }
      screen.PostEvent(Event::Custom);
    }));
  };

  auto comp = Renderer([&] {
    const int p = phase.load();
    std::string masked;  // • dots (multi-byte UTF-8), input never echoed
    for (size_t i = 0; i < entry.size(); ++i) masked += "•";
    Element status;
    {
      std::lock_guard<std::mutex> lk(mu);
      switch (p) {
        case 1: status = text("  validating with FRED…") | color(Color::Yellow); break;
        case 2: status = text("  ✓ valid — saved to " + store.location()) | color(Color::Green) | bold; break;
        case 3: status = text("  ✗ invalid — " + detail) | color(Color::Red) | bold; break;
        default: status = text("  enter your key, ↵ to validate") | dim; break;
      }
    }
    return vbox({
               text(" datawire — set FRED API key ") | bold | inverted,
               text(""),
               hbox({text("  key: "), text(masked + (p == 1 ? "" : "▏"))}),
               text(""),
               status,
               text(""),
               text(p == 2 ? "  press any key to finish" : "  ↵ validate · ⌫ edit · esc cancel") | dim,
               text(p == 0 || p == 3 ? "  (free key: https://fredaccount.stlouisfed.org/apikeys)" : "") | dim,
           }) |
           border | size(WIDTH, EQUAL, 66) | center;
  });

  comp |= CatchEvent([&](Event e) {
    const int p = phase.load();
    if (p == 2) { screen.Exit(); return true; }  // saved: any key finishes
    if (p == 1) return true;                       // busy: swallow input
    if (e == Event::Escape) { screen.Exit(); return true; }
    if (e == Event::Return) { if (!entry.empty()) validateAsync(); return true; }
    if (e == Event::Backspace) {
      if (!entry.empty()) entry.pop_back();
      if (p == 3) phase = 0;  // editing clears the red state
      return true;
    }
    if (e.is_character()) {
      entry += e.character();
      if (p == 3) phase = 0;
      return true;
    }
    return false;
  });

  screen.Loop(comp);
  return phase.load() == 2 ? 0 : 1;
}

}  // namespace datawire
