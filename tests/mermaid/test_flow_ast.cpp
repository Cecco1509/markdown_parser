#include "flow_fixture.hpp"

#include "mermaid/FlowParse.hpp"
#include "mermaid/Lower.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using mermaid::fixture::canonical;
using mermaid::fixture::canonical_extract;
using json = nlohmann::json;

namespace {

// In-scope fixtures (basename under FIXTURE_DIR). Deferred/out-of-scope fixtures
// (F-11 click, F-12/13 front-matter, F-15 edge ids, F-16 entities) are omitted
// on purpose — see docs/mermaid/deferred.md.
//
// To ADD a fixture (create .mmd, generate its golden .ast.json, register it
// here), follow "Generating fixtures for the C++ verification suite" in
// mermaid-utils/README.md.
const std::vector<std::string> kFixtures = {
    "f01_directions",   "f02_shapes",   "f03_labels",    "f04_edges",
    "f04b_chainlabels", "f05_length",   "f06_chaining",  "f07_subgraphs",
    "f07b_nested",      "f08_style",    "f09_class",     "f10_linkstyle",
    "f14_comments",     "f18_composite", "f19_pipeline",
    "simple-mermaid",
};

std::string read_file(const std::string &path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("cannot open " + path);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

} // namespace

class FlowAstFixture : public ::testing::TestWithParam<std::string> {};

TEST_P(FlowAstFixture, MatchesExtractAst) {
  const std::string base = std::string(FIXTURE_DIR) + "/" + GetParam();
  const std::string src = read_file(base + ".mmd");

  mermaid::FlowParseResult pr = mermaid::parse_flowchart(src);
  ASSERT_TRUE(pr.ok) << "parse failed: "
                     << (pr.errors.empty() ? "" : pr.errors.front());
  ASSERT_TRUE(pr.document.has_value());

  const json ours = canonical(mermaid::lower(*pr.document));

  json raw = json::parse(read_file(base + ".ast.json"));
  const json theirs = canonical_extract(raw);

  EXPECT_EQ(ours, theirs)
      << "\n┌─ Fixture: " << GetParam() << "\n"
      << "├─ Ours ────────────────────────────────────────────────────\n"
      << ours.dump(2) << "\n"
      << "├─ Extract (mermaid) ───────────────────────────────────────\n"
      << theirs.dump(2) << "\n"
      << "└───────────────────────────────────────────────────────────\n";
}

INSTANTIATE_TEST_SUITE_P(
    Fixtures, FlowAstFixture, ::testing::ValuesIn(kFixtures),
    [](const ::testing::TestParamInfo<std::string> &info) {
      std::string name = info.param;
      for (char &c : name)
        if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
      return name;
    });
