#pragma once

#include "lol/application/repository.hpp"

namespace lol {

class AssignmentService {
  public:
    explicit AssignmentService(Repository& repository) : repository_(repository) {}

    void replace_assignments(const GameAssignments& assignments);

  private:
    Repository& repository_;
};

} // namespace lol
