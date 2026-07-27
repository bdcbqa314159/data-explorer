#include "cache.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace datawire {

namespace {

long long nowSec() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string cacheBase() {
#if defined(_WIN32)
  if (const char* p = std::getenv("LOCALAPPDATA")) return std::string(p) + "\\datawire\\cache";
  if (const char* u = std::getenv("USERPROFILE")) return std::string(u) + "\\datawire\\cache";
#else
  if (const char* h = std::getenv("HOME")) return std::string(h) + "/.cache/datawire";
#endif
  return ".cache";
}

std::string safeName(const std::string& id) {
  std::string out;
  for (unsigned char c : id)
    out += (std::isalnum(c) || c == '.' || c == '_' || c == '-') ? static_cast<char>(c) : '_';
  return out;
}

}  // namespace

SeriesCache::SeriesCache() {
  dir_ = cacheBase() + "/series";
  std::error_code ec;
  fs::create_directories(dir_, ec);  // ignore: get/put degrade gracefully
}

std::string SeriesCache::pathFor(const std::string& id) const {
  return dir_ + "/" + safeName(id) + ".json";
}

std::optional<CachedSeries> SeriesCache::get(const std::string& id) const {
  std::ifstream in(pathFor(id));
  if (!in) return std::nullopt;
  try {
    nlohmann::json j;
    in >> j;
    Series s;
    const auto& m = j.at("meta");
    s.meta.id = m.value("id", id);
    s.meta.title = m.value("title", "");
    s.meta.unit = m.value("unit", "");
    s.meta.frequency = m.value("frequency", "");
    s.meta.seasonalAdj = m.value("seasonalAdj", "");
    s.meta.asOf = m.value("asOf", "");
    s.meta.sourceUrl = m.value("sourceUrl", "");
    s.meta.source = m.value("source", "FRED");
    for (const auto& o : j.at("observations"))
      s.observations.push_back({o.at("date").get<std::string>(), o.at("value").get<double>()});
    const long long fetchedAt = j.value("fetchedAt", 0LL);
    return CachedSeries{std::move(s), nowSec() - fetchedAt};
  } catch (...) {
    return std::nullopt;
  }
}

void SeriesCache::put(const std::string& id, const Series& s) const {
  nlohmann::json j;
  j["fetchedAt"] = nowSec();
  j["meta"] = {
      {"id", s.meta.id},         {"title", s.meta.title},
      {"unit", s.meta.unit},     {"frequency", s.meta.frequency},
      {"seasonalAdj", s.meta.seasonalAdj}, {"asOf", s.meta.asOf},
      {"sourceUrl", s.meta.sourceUrl},     {"source", s.meta.source},
  };
  auto obs = nlohmann::json::array();
  for (const auto& o : s.observations) obs.push_back({{"date", o.date}, {"value", o.value}});
  j["observations"] = std::move(obs);

  std::ofstream out(pathFor(id), std::ios::trunc);
  if (out) out << j.dump();
}

}  // namespace datawire
