#pragma once

#include "GrammarSpec.hpp"
#include "Lr.hpp"

#include <string>

namespace lrgen {

// Emits <out_base>.hpp and <out_base>.cpp — a complete, self-contained
// canonical LR(1) parser exposing parse(). `header_name` is the include path
// the .cpp should use to reach its own header.
void emit(const Grammar &g, const std::vector<State> &states, const Tables &t,
          const std::string &out_base, const std::string &header_name);

} // namespace lrgen
