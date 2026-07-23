#pragma once
#include "series.hpp"

#include <string>
#include <vector>

namespace datawire {

// Cutoff date (YYYY-MM-DD) = latestDate minus `years`. Empty for the MAX window
// (years >= 1000) or an unparseable date.
std::string windowCutoff(const std::string& latestDate, int years);

// Observations within `years` of the latest one (all of them for MAX).
std::vector<Observation> windowFilter(const std::vector<Observation>& obs, int years);

}  // namespace datawire
