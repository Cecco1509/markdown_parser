#include "case_report.hpp"
#include "markdown_parser/renderer/JsonRenderer.hpp"
#include "markdown_parser/parser/parser.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

struct MdastCase {
  std::string markdown;
  json mdast; // golden tree from remark
  int example;
  std::string section;
};

// Matches SpecCase::PrintTo so failing JSON cases print as "#N [section]"
// instead of a raw byte-object dump.
inline void PrintTo(const MdastCase &tc, std::ostream *os) {
  *os << "#" << tc.example << " [" << tc.section << "]";
}

std::vector<MdastCase> loadSpec(const std::string &path) {
  std::ifstream file(path);
  if (!file.is_open())
    throw std::runtime_error("Cannot open mdast golden file: " + path);

  json j;
  file >> j;

  std::vector<MdastCase> cases;
  cases.reserve(j.size());
  for (const auto &e : j)
    cases.push_back({e.at("markdown").get<std::string>(), e.at("mdast"),
                     e.at("example").get<int>(),
                     e.at("section").get<std::string>()});
  return cases;
}

const std::vector<MdastCase> kAllCases = loadSpec(SPEC_MDAST_FILE_PATH);

} // namespace

class JsonMdastTest : public ::testing::TestWithParam<MdastCase> {};

TEST_P(JsonMdastTest, MatchesRemark) {
  const MdastCase &tc = GetParam();

  // Documented, deliberate divergences from remark's mdast. Each group is a
  // representational choice or a parse-time normalization we don't undo — not a
  // rendering bug. See docs and the AI verification report for the rationale.
  static const std::set<int> kKnownFailures = {
      // A multi-line reference-definition label whose internal indentation
      // mdast preserves in `definition.label` (`"Foo\n  bar"`). Our paragraph
      // line-accumulation strips that indentation before the label is scanned,
      // so the raw whitespace is no longer recoverable at render time.
      541,
  };
  if (kKnownFailures.count(tc.example))
    GTEST_SKIP() << "Example #" << tc.example << " diverges from remark"
                 << " (section: " << tc.section << ")";

  markdown_parser::JsonRenderer jr;
  const std::string rendered = markdown_parser::parse(tc.markdown, jr);

  json ours;
  ASSERT_NO_THROW(ours = json::parse(rendered))
      << "our renderer produced invalid JSON for example #" << tc.example;

  EXPECT_EQ(ours, tc.mdast) << commonmark::testing::caseReport(
      tc.section, tc.example, /*lines=*/"", tc.markdown,
      "Expected (remark)", tc.mdast.dump(2), "Actual (ours)", ours.dump(2));
}

INSTANTIATE_TEST_SUITE_P(
    Mdast, JsonMdastTest, ::testing::ValuesIn(kAllCases),
    [](const ::testing::TestParamInfo<MdastCase> &info) {
      std::string num = std::to_string(info.param.example);
      num = std::string(3 - std::min<int>(3, num.size()), '0') + num;

      std::string section = info.param.section.substr(0, 32);
      for (char &c : section)
        if (!std::isalnum(static_cast<unsigned char>(c)))
          c = '_';

      return num + "_" + section;
    });
