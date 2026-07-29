#include "analysis.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace datawire::analysis {

Transform nextTransform(Transform t) {
  switch (t) {
    case Transform::None: return Transform::YoY;
    case Transform::YoY: return Transform::Pct;
    case Transform::Pct: return Transform::MA;
    default: return Transform::None;
  }
}

const char* transformLabel(Transform t) {
  switch (t) {
    case Transform::YoY: return "YoY %";
    case Transform::Pct: return "% chg";
    case Transform::MA: return "1y MA";
    default: return "";
  }
}

int periodsPerYear(const std::string& freq) {
  auto has = [&](const char* s) { return freq.find(s) != std::string::npos; };
  if (has("Dai")) return 252;
  if (has("Week")) return 52;
  if (has("Month")) return 12;
  if (has("Quart")) return 4;
  if (has("Semi")) return 2;
  if (has("Ann")) return 1;
  return 12;
}

std::string decYear(const std::string& date) {
  if (date.size() < 4) return date;
  try {
    return std::to_string(std::stoi(date.substr(0, 4)) - 1) + date.substr(4);
  } catch (...) {
    return date;
  }
}

std::vector<Observation> applyTransform(const std::vector<Observation>& obs, Transform t,
                                        const std::string& freq) {
  const int n = static_cast<int>(obs.size());
  if (t == Transform::None || n == 0) return obs;
  std::vector<Observation> out;
  if (t == Transform::Pct) {
    for (int i = 1; i < n; ++i)
      if (obs[i - 1].value != 0.0)
        out.push_back({obs[i].date, (obs[i].value / obs[i - 1].value - 1.0) * 100.0});
  } else if (t == Transform::YoY) {
    for (int i = 0; i < n; ++i) {
      const std::string target = decYear(obs[i].date);
      int a = 0, b = i - 1, j = -1;  // last point on/before one year earlier
      while (a <= b) {
        const int mid = (a + b) / 2;
        if (obs[mid].date <= target) { j = mid; a = mid + 1; } else b = mid - 1;
      }
      if (j >= 0 && obs[j].value != 0.0)
        out.push_back({obs[i].date, (obs[i].value / obs[j].value - 1.0) * 100.0});
    }
  } else {  // MA: trailing N-period moving average (N = one year)
    const int N = std::max(2, periodsPerYear(freq));
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
      sum += obs[i].value;
      if (i >= N) sum -= obs[i - N].value;
      if (i >= N - 1) out.push_back({obs[i].date, sum / N});
    }
  }
  return out;
}

std::string sparkline(const std::vector<Observation>& obs, int width) {
  static const char* blocks[8] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
  if (obs.empty() || width <= 0) return std::string(std::max(0, width), ' ');
  const int n = static_cast<int>(obs.size());
  const int span = std::min(n, 90);
  const int start = n - span;
  double mn = obs[start].value, mx = obs[start].value;
  for (int i = start; i < n; ++i) {
    mn = std::min(mn, obs[i].value);
    mx = std::max(mx, obs[i].value);
  }
  const double range = (mx - mn) > 0 ? (mx - mn) : 1.0;
  std::string out;
  for (int c = 0; c < width; ++c) {
    int a = start + static_cast<int>(static_cast<double>(c) / width * span);
    int b = start + static_cast<int>(static_cast<double>(c + 1) / width * span);
    b = std::clamp(std::max(b, a + 1), a + 1, n);
    double sum = 0.0;
    for (int i = a; i < b; ++i) sum += obs[i].value;
    const double avg = sum / (b - a);
    const int lvl = std::clamp(static_cast<int>(std::lround((avg - mn) / range * 7)), 0, 7);
    out += blocks[lvl];
  }
  return out;
}

double recentMovePct(const std::vector<Observation>& obs) {
  if (obs.size() < 2) return 0.0;
  const double prev = obs[obs.size() - 2].value;
  const double last = obs.back().value;
  return prev != 0.0 ? std::abs((last - prev) / prev) : 0.0;
}

Aligned align(const std::vector<Observation>& a, const std::vector<Observation>& b) {
  // Both series are ascending by ISO date, so string order == chronological.
  Aligned r;
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (a[i].date == b[j].date) {
      r.dates.push_back(a[i].date);
      r.a.push_back(a[i].value);
      r.b.push_back(b[j].value);
      ++i; ++j;
    } else if (a[i].date < b[j].date) {
      ++i;
    } else {
      ++j;
    }
  }
  return r;
}

CompareStats compareStats(const Aligned& al) {
  CompareStats s;
  s.n = al.n();
  if (s.n < 2) return s;
  const int n = s.n;
  double sa = 0.0, sb = 0.0;
  for (int i = 0; i < n; ++i) { sa += al.a[i]; sb += al.b[i]; }
  const double ma = sa / n, mb = sb / n;
  double saa = 0.0, sbb = 0.0, sab = 0.0;
  for (int i = 0; i < n; ++i) {
    const double da = al.a[i] - ma, db = al.b[i] - mb;
    saa += da * da; sbb += db * db; sab += da * db;
  }
  if (saa > 0.0 && sbb > 0.0) s.correlation = sab / std::sqrt(saa * sbb);
  if (sbb > 0.0) { s.beta = sab / sbb; s.alpha = ma - s.beta * mb; }
  return s;
}

std::vector<Observation> spread(const Aligned& al) {
  std::vector<Observation> o;
  o.reserve(al.n());
  for (int i = 0; i < al.n(); ++i) o.push_back({al.dates[i], al.a[i] - al.b[i]});
  return o;
}

std::vector<Observation> ratio(const Aligned& al) {
  std::vector<Observation> o;
  for (int i = 0; i < al.n(); ++i)
    if (al.b[i] != 0.0) o.push_back({al.dates[i], al.a[i] / al.b[i]});
  return o;
}

}  // namespace datawire::analysis
