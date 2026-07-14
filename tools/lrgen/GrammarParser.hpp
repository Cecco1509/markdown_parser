#pragma once

#include "GrammarSpec.hpp"

#include <string>

namespace lrgen {

// Parses the .grammar meta-syntax into a finalized Grammar (symbols resolved,
// ids assigned, augmented start + $end added, $k references validated).
// Throws std::runtime_error with a line-tagged message on any error.
Grammar parse_grammar(const std::string &text);

} // namespace lrgen
