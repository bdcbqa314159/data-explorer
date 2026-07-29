#pragma once
#include "series.hpp"

#include <string>
#include <vector>

// Pure, storage/UI-agnostic time-series analysis — the SDK's number-crunching.
// No ftxui, no I/O: everything here is testable in isolation.
namespace datawire::analysis {

// --- analyst lens applied to a charted series ---------------------------
enum class Transform { None, YoY, Pct, MA };
Transform nextTransform(Transform t);
const char* transformLabel(Transform t);  // "" for None

int periodsPerYear(const std::string& freq);   // frequency word -> periods/yr
std::string decYear(const std::string& date);  // "YYYY-..." one year earlier

// Transform the FULL series (windowing happens after). Same dates, transformed
// values; points without enough history are dropped.
std::vector<Observation> applyTransform(const std::vector<Observation>& obs, Transform t,
                                        const std::string& freq);

// --- compact block-char sparkline (bucket-averaged, 8 levels) -----------
std::string sparkline(const std::vector<Observation>& obs, int width);

// --- movers ranking key -------------------------------------------------
double recentMovePct(const std::vector<Observation>& obs);  // |Δ%| of last pt; 0 if <2 pts

// --- comparison of two series -------------------------------------------
// Collapse to one point per calendar month (last observation in the month),
// so series of different frequencies (weekly, daily, quarterly…) can be
// compared on a common monthly grid. Input is ascending by date.
std::vector<Observation> resampleMonthly(const std::vector<Observation>& obs);

struct Aligned {  // inner-join of two monthly-resampled series, ascending
  std::vector<std::string> dates;
  std::vector<double> a, b;
  int n() const { return static_cast<int>(dates.size()); }
};
// Resamples both to monthly, then inner-joins on year-month. Matching by
// month (not exact date) is what lets a weekly series compare to a monthly one.
Aligned align(const std::vector<Observation>& a, const std::vector<Observation>& b);

struct CompareStats {
  int n = 0;
  double correlation = 0.0;  // Pearson of levels (0 if n<2 or zero variance)
  double beta = 0.0;         // OLS slope of a on b   (a ≈ alpha + beta·b)
  double alpha = 0.0;
};
CompareStats compareStats(const Aligned& al);

std::vector<Observation> spread(const Aligned& al);  // a - b, per common date
std::vector<Observation> ratio(const Aligned& al);   // a / b, per common date (skips b==0)

// Rolling window over the aligned pair (one point per full window, dated at its
// end). w ≥ 2. Correlation ∈ [-1,1]; beta = slope of a on b in the window.
std::vector<Observation> rollingCorrelation(const Aligned& al, int w);
std::vector<Observation> rollingBeta(const Aligned& al, int w);

}  // namespace datawire::analysis
