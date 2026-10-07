#pragma once

#include "lol/application/repository.hpp"

namespace lol {

class SeriesService {
  public:
    explicit SeriesService(Repository& repository) : repository_(repository) {}

    Id create_series(const SeriesDefinition& definition);
    Id add_game(const GameDefinition& definition);

  private:
    Repository& repository_;
};

} // namespace lol
