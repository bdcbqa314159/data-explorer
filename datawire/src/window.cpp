#include "window.hpp"

#include <string>

namespace datawire {

std::string windowCutoff(const std::string& latestDate, int years) {
  if (years >= 1000 || latestDate.size() < 4) return "";
  try {
    return std::to_string(std::stoi(latestDate.substr(0, 4)) - years) + latestDate.substr(4);
  } catch (...) {
    return "";
  }
}

std::vector<Observation> windowFilter(const std::vector<Observation>& obs, int years) {
  if (obs.empty()) return obs;
  const std::string cutoff = windowCutoff(obs.back().date, years);
  if (cutoff.empty()) return obs;  // MAX or unparseable -> keep all
  std::vector<Observation> out;
  for (const auto& o : obs)
    if (o.date >= cutoff) out.push_back(o);
  return out;
}

}  // namespace datawire
