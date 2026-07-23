// Plain-assert tests for the window math. Run with: ctest --preset default
#include "window.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace datawire;

int main() {
  // cutoff = latest minus N years; empty for MAX / unparseable.
  assert(windowCutoff("2026-06-01", 1) == "2025-06-01");
  assert(windowCutoff("2026-06-01", 5) == "2021-06-01");
  assert(windowCutoff("2026-06-01", 1000) == "");  // MAX
  assert(windowCutoff("", 1) == "");

  // filter keeps observations on/after the cutoff.
  std::vector<Observation> obs = {{"2019-01-01", 1}, {"2024-01-01", 2}, {"2026-01-01", 3}};
  assert(windowFilter(obs, 1000).size() == 3);  // MAX -> all
  assert(windowFilter(obs, 5).size() == 2);     // cutoff 2021 -> 2024, 2026
  assert(windowFilter(obs, 1).size() == 1);     // cutoff 2025 -> 2026
  assert(windowFilter({}, 5).empty());

  std::cout << "all window tests passed\n";
  return 0;
}
