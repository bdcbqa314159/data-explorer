#pragma once
#include "source.hpp"

#include <string>

namespace datawire {

// FRED implementation of Source (St. Louis Fed). Stateless apart from the key,
// so its methods are safe to call from multiple worker threads.
class FredSource : public Source {
public:
  explicit FredSource(std::string apiKey);
  Series fetchSeries(const std::string& id) override;
  std::vector<SearchResult> search(const std::string& text) override;

private:
  std::string apiKey_;
};

}  // namespace datawire
