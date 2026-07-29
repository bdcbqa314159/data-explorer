#pragma once
#include "series.hpp"

#include <vector>

// Robust, numerically-stable descriptive statistics — the foundation of the
// time-series toolkit. Pure functions on value vectors; no I/O, no UI.
// Convention: empty/degenerate input returns 0 (documented per function) rather
// than NaN, so callers can display without special-casing.
namespace datawire::analysis {

// Pull the values out of a series (dates dropped) — the usual toolkit input.
std::vector<double> values(const std::vector<Observation>& obs);

// --- location & scale ---------------------------------------------------
double mean(const std::vector<double>& x);      // Welford accumulation; 0 if empty
double variance(const std::vector<double>& x);  // sample variance (÷ n-1); 0 if n<2
double stdev(const std::vector<double>& x);      // sqrt(variance)

// q in [0,1], linear interpolation between order statistics ("type 7").
// Takes a copy and sorts it. 0 if empty.
double quantile(std::vector<double> x, double q);
double median(std::vector<double> x);  // quantile(x, 0.5)

// Median Absolute Deviation, scaled by 1.4826 so it estimates σ for normal
// data. Outlier-resistant scale. 0 if empty.
double mad(std::vector<double> x);

// --- one-shot descriptive block (for a readout pane) --------------------
struct Summary {
  int n = 0;
  double mean = 0, median = 0, stdev = 0, mad = 0;
  double min = 0, max = 0, q25 = 0, q75 = 0, last = 0;
};
Summary summarize(const std::vector<double>& x);

// --- returns ------------------------------------------------------------
std::vector<Observation> simpleReturns(const std::vector<Observation>& obs);  // p_t/p_{t-1} − 1
std::vector<Observation> logReturns(const std::vector<Observation>& obs);      // ln(p_t/p_{t-1})

// Annualised volatility: stdev of simple returns × √periodsPerYear. 0 if <2
// returns. (periodsPerYear is passed in — the caller derives it from frequency.)
double volatility(const std::vector<Observation>& obs, int periodsPerYear);

// --- rolling window statistics (one point per full window, dated at its end) --
std::vector<Observation> rollingMean(const std::vector<Observation>& obs, int w);   // w ≥ 1
std::vector<Observation> rollingStdev(const std::vector<Observation>& obs, int w);  // w ≥ 2

}  // namespace datawire::analysis
