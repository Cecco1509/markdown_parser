#pragma once
#include <string>
#include <string_view>

namespace unicode_fold {

// Apply Unicode case folding to s (UTF-8 input/output).
// Handles both simple (one-to-one) and full (one-to-many) folds.
std::string foldString(std::string_view s);

} // namespace unicode_fold
