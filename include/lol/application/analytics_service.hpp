#pragma once

#include "lol/application/repository.hpp"

namespace lol {

class AnalyticsService {
  public:
    explicit AnalyticsService(const Repository& repository) : repository_(repository) {}

    [[nodiscard]] AnalysisResult analyze(const AnalysisFilter& filter) const;

  private:
    const Repository& repository_;
};

} // namespace lol
