// Robust descriptive statistics. No network, no UI.
#include "stats.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace datawire;
using namespace datawire::analysis;

static bool near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) < tol; }

int main() {
  // Classic set: mean 5, sample stdev sqrt(32/7) ≈ 2.13809.
  {
    std::vector<double> x = {2, 4, 4, 4, 5, 5, 7, 9};
    assert(near(mean(x), 5.0));
    assert(near(variance(x), 32.0 / 7.0));
    assert(near(stdev(x), std::sqrt(32.0 / 7.0)));
  }

  // Degenerate inputs return 0, never NaN.
  assert(near(mean({}), 0.0));
  assert(near(variance({}), 0.0) && near(variance({3.0}), 0.0));
  assert(near(median({}), 0.0) && near(mad({}), 0.0));

  // Quantile (type-7 linear interpolation).
  {
    std::vector<double> x = {1, 2, 3, 4};
    assert(near(quantile(x, 0.0), 1.0));
    assert(near(quantile(x, 1.0), 4.0));
    assert(near(quantile(x, 0.5), 2.5));   // median of even set
    assert(near(quantile(x, 0.25), 1.75));
    assert(near(median({1, 2, 3}), 2.0));  // odd set
  }

  // MAD: median 3, |dev| = {2,1,0,1,2} -> median 1, *1.4826.
  {
    std::vector<double> x = {1, 2, 3, 4, 5};
    assert(near(mad(x), 1.4826));
  }

  // MAD is outlier-resistant where stdev is not: one wild point barely moves MAD.
  {
    std::vector<double> clean = {10, 11, 12, 13, 14};
    std::vector<double> spiked = {10, 11, 12, 13, 1000};
    assert(stdev(spiked) > 10.0 * stdev(clean));   // stdev blows up
    assert(mad(spiked) < 2.0 * mad(clean));        // MAD stays sane
  }

  // Summary block.
  {
    std::vector<double> x = {1, 2, 3, 4, 5};
    auto s = summarize(x);
    assert(s.n == 5 && near(s.mean, 3.0) && near(s.median, 3.0));
    assert(near(s.min, 1.0) && near(s.max, 5.0) && near(s.last, 5.0));
    assert(near(s.q25, 2.0) && near(s.q75, 4.0));
  }

  // Returns: 100 -> 110 -> 121 is +10%, +10%.
  {
    std::vector<Observation> obs = {{"d1", 100}, {"d2", 110}, {"d3", 121}};
    auto sr = simpleReturns(obs);
    assert(sr.size() == 2 && near(sr[0].value, 0.1) && near(sr[1].value, 0.1));
    auto lr = logReturns({{"d1", 1.0}, {"d2", std::exp(1.0)}});
    assert(lr.size() == 1 && near(lr[0].value, 1.0));
  }

  // Volatility = stdev(returns) · √periodsPerYear; scales with the sqrt.
  {
    std::vector<Observation> obs = {{"d1", 100}, {"d2", 110}, {"d3", 100}, {"d4", 110}};
    const double base = stdev(values(simpleReturns(obs)));
    assert(near(volatility(obs, 1), base));
    assert(near(volatility(obs, 4), base * 2.0));  // √4 = 2
  }

  // Rolling mean/stdev: one point per full window, dated at the window end.
  {
    std::vector<Observation> obs = {{"d1", 1}, {"d2", 2}, {"d3", 3}, {"d4", 4}, {"d5", 5}};
    auto rm = rollingMean(obs, 3);
    assert(rm.size() == 3);
    assert(rm[0].date == "d3" && near(rm[0].value, 2.0));  // mean(1,2,3)
    assert(near(rm[2].value, 4.0));                        // mean(3,4,5)
    auto rs = rollingStdev(obs, 3);
    assert(rs.size() == 3 && near(rs[0].value, 1.0));      // stdev(1,2,3)=1
    assert(rollingStdev(obs, 1).empty());                  // w<2 -> nothing
  }

  // Outliers via robust z: one wild point flagged, clean data flags nothing.
  {
    std::vector<double> spiked = {10, 11, 12, 13, 14, 1000};
    auto idx = outliers(spiked);
    assert(idx.size() == 1 && idx[0] == 5);
    assert(std::fabs(robustZ(spiked)[5]) > 3.5);
    assert(outliers({10, 11, 12, 13, 14}).empty());
    assert(outliers({}).empty());
  }

  std::puts("stats_test OK");
  return 0;
}
