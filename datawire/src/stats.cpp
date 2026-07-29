#include "stats.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace datawire::analysis {

std::vector<double> values(const std::vector<Observation>& obs) {
  std::vector<double> v;
  v.reserve(obs.size());
  for (const auto& o : obs) v.push_back(o.value);
  return v;
}

double mean(const std::vector<double>& x) {
  // Welford running mean — avoids overflow and cancels less than a naive sum.
  double m = 0.0;
  int k = 0;
  for (double v : x) m += (v - m) / ++k;
  return m;
}

double variance(const std::vector<double>& x) {
  const int n = static_cast<int>(x.size());
  if (n < 2) return 0.0;
  // Welford: track mean and M2 (sum of squared deviations) in one pass.
  double m = 0.0, m2 = 0.0;
  int k = 0;
  for (double v : x) {
    const double d = v - m;
    m += d / ++k;
    m2 += d * (v - m);
  }
  return m2 / (n - 1);
}

double stdev(const std::vector<double>& x) { return std::sqrt(variance(x)); }

double quantile(std::vector<double> x, double q) {
  if (x.empty()) return 0.0;
  std::sort(x.begin(), x.end());
  if (x.size() == 1) return x[0];
  q = std::clamp(q, 0.0, 1.0);
  const double pos = q * (x.size() - 1);
  const int lo = static_cast<int>(pos);
  const double frac = pos - lo;
  if (lo + 1 >= static_cast<int>(x.size())) return x[lo];
  return x[lo] + frac * (x[lo + 1] - x[lo]);
}

double median(std::vector<double> x) { return quantile(std::move(x), 0.5); }

double mad(std::vector<double> x) {
  if (x.empty()) return 0.0;
  const double med = median(x);  // copies internally
  for (double& v : x) v = std::abs(v - med);
  return median(std::move(x)) * 1.4826;
}

Summary summarize(const std::vector<double>& x) {
  Summary s;
  s.n = static_cast<int>(x.size());
  if (s.n == 0) return s;
  s.mean = mean(x);
  s.stdev = stdev(x);
  s.median = median(x);
  s.mad = mad(x);
  s.q25 = quantile(x, 0.25);
  s.q75 = quantile(x, 0.75);
  s.min = *std::min_element(x.begin(), x.end());
  s.max = *std::max_element(x.begin(), x.end());
  s.last = x.back();
  return s;
}

std::vector<Observation> simpleReturns(const std::vector<Observation>& obs) {
  std::vector<Observation> r;
  for (size_t i = 1; i < obs.size(); ++i)
    if (obs[i - 1].value != 0.0)
      r.push_back({obs[i].date, obs[i].value / obs[i - 1].value - 1.0});
  return r;
}

std::vector<Observation> logReturns(const std::vector<Observation>& obs) {
  std::vector<Observation> r;
  for (size_t i = 1; i < obs.size(); ++i)
    if (obs[i - 1].value > 0.0 && obs[i].value > 0.0)
      r.push_back({obs[i].date, std::log(obs[i].value / obs[i - 1].value)});
  return r;
}

double volatility(const std::vector<Observation>& obs, int periodsPerYear) {
  const auto r = simpleReturns(obs);
  if (r.size() < 2) return 0.0;
  return stdev(values(r)) * std::sqrt(static_cast<double>(std::max(1, periodsPerYear)));
}

std::vector<Observation> rollingMean(const std::vector<Observation>& obs, int w) {
  std::vector<Observation> out;
  if (w < 1) return out;
  const int n = static_cast<int>(obs.size());
  double sum = 0.0;  // running window sum
  for (int i = 0; i < n; ++i) {
    sum += obs[i].value;
    if (i >= w) sum -= obs[i - w].value;
    if (i >= w - 1) out.push_back({obs[i].date, sum / w});
  }
  return out;
}

std::vector<Observation> rollingStdev(const std::vector<Observation>& obs, int w) {
  std::vector<Observation> out;
  if (w < 2) return out;
  const int n = static_cast<int>(obs.size());
  // ponytail: O(n·w) recompute per window — fine at TUI sizes; swap for a
  // sliding Welford if window stats ever land on a hot path.
  for (int i = w - 1; i < n; ++i) {
    std::vector<double> win;
    win.reserve(w);
    for (int k = i - w + 1; k <= i; ++k) win.push_back(obs[k].value);
    out.push_back({obs[i].date, stdev(win)});
  }
  return out;
}

}  // namespace datawire::analysis
