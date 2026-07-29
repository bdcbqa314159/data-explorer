// Pure analysis: transforms + comparison metrics. No network, no UI.
#include "analysis.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace datawire;
using namespace datawire::analysis;

static std::string ym(int y, int m) {
  char b[11];
  std::snprintf(b, sizeof b, "%04d-%02d-01", y, m);
  return b;
}

static bool near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) < tol; }

int main() {
  // --- YoY: 14 monthly points growing 1%/month; YoY = 1.01^12 - 1 = 12.68% ---
  {
    std::vector<Observation> obs;
    int k = 0;
    for (int m = 1; m <= 12; ++m) obs.push_back({ym(2025, m), std::pow(1.01, k++)});
    for (int m = 1; m <= 2; ++m) obs.push_back({ym(2026, m), std::pow(1.01, k++)});
    auto yoy = applyTransform(obs, Transform::YoY, "Monthly");
    assert(!yoy.empty());
    assert(near(yoy.back().value, 12.6825, 1e-2));  // (1.01^12 - 1)*100
  }

  // --- Pct change: 100 -> 110 is +10% ---
  {
    std::vector<Observation> obs = {{ym(2025, 1), 100.0}, {ym(2025, 2), 110.0}};
    auto p = applyTransform(obs, Transform::Pct, "Monthly");
    assert(p.size() == 1 && near(p[0].value, 10.0));
  }

  // --- sparkline: exactly `width` glyphs of output, blank when empty ---
  {
    std::vector<Observation> obs = {{"2025-01-01", 1}, {"2025-02-01", 2}, {"2025-03-01", 3}};
    assert(!sparkline(obs, 8).empty());
    assert(sparkline({}, 5) == "     ");
  }

  // --- recentMovePct: last point +25% over prior ---
  {
    std::vector<Observation> obs = {{"a", 4.0}, {"b", 5.0}};
    assert(near(recentMovePct(obs), 0.25));
    assert(near(recentMovePct({}), 0.0));
  }

  // --- align: inner-join on common dates only ---
  {
    std::vector<Observation> a = {{"2025-01-01", 1}, {"2025-02-01", 2}, {"2025-03-01", 3}};
    std::vector<Observation> b = {{"2025-02-01", 20}, {"2025-03-01", 30}, {"2025-04-01", 40}};
    auto al = align(a, b);
    assert(al.n() == 2);
    assert(al.dates[0] == "2025-02-01" && al.a[0] == 2 && al.b[0] == 20);
  }

  // --- cross-frequency align: monthly A vs intra-month B, joined by month ---
  {
    std::vector<Observation> A;
    for (int m = 1; m <= 6; ++m) A.push_back({ym(2025, m), (double)m});  // monthly
    std::vector<Observation> B = {{"2025-01-03", 10.0}, {"2025-01-28", 11.0},  // two in Jan
                                  {"2025-02-15", 20.0}, {"2025-03-09", 30.0}};
    auto al = align(A, B);
    assert(al.n() == 3);                    // Jan, Feb, Mar overlap
    assert(al.dates[0] == "2025-01-01");    // representative date from monthly A
    assert(near(al.b[0], 11.0));            // last observation in January wins
    assert(near(al.a[2], 3.0) && near(al.b[2], 30.0));
  }

  // --- compareStats / spread / ratio on B = 2A (perfect positive) ---
  {
    std::vector<Observation> A, B;
    for (int i = 1; i <= 4; ++i) { A.push_back({ym(2025, i), (double)i}); B.push_back({ym(2025, i), 2.0 * i}); }
    auto al = align(A, B);
    auto s = compareStats(al);
    assert(s.n == 4);
    assert(near(s.correlation, 1.0));  // A and 2A move together
    assert(near(s.beta, 0.5));         // A ≈ 0.5·B
    assert(near(s.alpha, 0.0, 1e-9));
    auto sp = spread(al);  // A - B = -A
    assert(sp.size() == 4 && near(sp[3].value, 4.0 - 8.0));
    auto rt = ratio(al);   // A / B = 0.5
    assert(rt.size() == 4 && near(rt[0].value, 0.5));
  }

  // --- anti-correlation gives -1 ---
  {
    std::vector<Observation> A, B;
    for (int i = 1; i <= 4; ++i) { A.push_back({ym(2025, i), (double)(5 - i)}); B.push_back({ym(2025, i), 2.0 * i}); }
    auto s = compareStats(align(A, B));
    assert(near(s.correlation, -1.0));
  }

  // --- transform cycle + labels ---
  assert(nextTransform(Transform::None) == Transform::YoY);
  assert(std::string(transformLabel(Transform::YoY)) == "YoY %");
  assert(std::string(transformLabel(Transform::None)).empty());

  std::puts("analysis_test OK");
  return 0;
}
