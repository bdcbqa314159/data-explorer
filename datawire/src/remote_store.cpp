#include "remote_store.hpp"

#include "http_client.hpp"

#include <nlohmann/json.hpp>

#include <utility>

namespace datawire {
namespace {

nlohmann::json toJson(const Series& s) {
  nlohmann::json meta = {
      {"id", s.meta.id},         {"title", s.meta.title},
      {"unit", s.meta.unit},     {"frequency", s.meta.frequency},
      {"seasonalAdj", s.meta.seasonalAdj}, {"asOf", s.meta.asOf},
      {"sourceUrl", s.meta.sourceUrl},     {"source", s.meta.source},
  };
  auto obs = nlohmann::json::array();
  for (const auto& o : s.observations) obs.push_back({{"date", o.date}, {"value", o.value}});
  return {{"meta", std::move(meta)}, {"observations", std::move(obs)}};
}

Series fromJson(const nlohmann::json& j) {
  Series s;
  const auto& m = j.at("meta");
  s.meta.id = m.value("id", "");
  s.meta.title = m.value("title", "");
  s.meta.unit = m.value("unit", "");
  s.meta.frequency = m.value("frequency", "");
  s.meta.seasonalAdj = m.value("seasonalAdj", "");
  s.meta.asOf = m.value("asOf", "");
  s.meta.sourceUrl = m.value("sourceUrl", "");
  s.meta.source = m.value("source", "FRED");
  if (j.contains("observations"))
    for (const auto& o : j.at("observations"))
      s.observations.push_back({o.value("date", ""), o.value("value", 0.0)});
  return s;
}

}  // namespace

RemoteStore::RemoteStore(std::string baseUrl, std::string caPath)
    : baseUrl_(std::move(baseUrl)), caPath_(std::move(caPath)) {
  while (!baseUrl_.empty() && baseUrl_.back() == '/') baseUrl_.pop_back();
}

std::optional<StoredSeries> RemoteStore::get(const std::string& id) {
  try {
    const std::string body = httpGet(baseUrl_ + "/series/" + id, 20, caPath_);
    Series s = fromJson(nlohmann::json::parse(body));
    return StoredSeries{std::move(s), 0};  // server doesn't expose age; treat as fresh
  } catch (...) {
    return std::nullopt;  // 404 (not on server) or offline
  }
}

void RemoteStore::put(const std::string& id, const Series& series) {
  (void)id;  // the id travels inside the series JSON (meta.id)
  httpPost(baseUrl_ + "/series", toJson(series).dump(), "application/json", 20, caPath_);
}

std::vector<std::string> RemoteStore::listIds() {
  const std::string body = httpGet(baseUrl_ + "/series", 20, caPath_);
  std::vector<std::string> out;
  for (const auto& e : nlohmann::json::parse(body)) out.push_back(e.get<std::string>());
  return out;
}

}  // namespace datawire
