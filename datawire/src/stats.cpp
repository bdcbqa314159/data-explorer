#include "stats.hpp"

#include <algorithm>
#include <cmath>

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

}  // namespace datawire::analysis
