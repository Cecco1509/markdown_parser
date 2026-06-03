#include <gtest/gtest.h>
#include <set>
#include "commonmark_spec_case.hpp"
#include "markdown_parser/parser.hpp"

using commonmark::testing::SpecCase;
using commonmark::testing::loadSpec;

static const std::vector<SpecCase> kAllCases = loadSpec(SPEC_FILE_PATH);

class CommonMarkSpecTest : public ::testing::TestWithParam<SpecCase> {};

TEST_P(CommonMarkSpecTest, MatchesExpectedHtml) {
    const SpecCase& tc = GetParam();

    static const std::set<int> kKnownFailures = {
        // example numbers go here, e.g.: 42, 87, 103
    };
    if (kKnownFailures.count(tc.example)) {
        GTEST_SKIP() << "Example #" << tc.example
                     << " not yet implemented"
                     << " (section: " << tc.section << ","
                     << " lines " << tc.start_line << "-" << tc.end_line << ")";
    }

    const std::string actual = markdown_parser::parse(tc.markdown);

    EXPECT_EQ(tc.html, actual)
        << "\n"
        << "┌─ Section    : " << tc.section                             << "\n"
        << "│  Example #  : " << tc.example                             << "\n"
        << "│  Spec lines : " << tc.start_line << "-" << tc.end_line    << "\n"
        << "├─ Markdown input ──────────────────────────────────────────\n"
        << tc.markdown
        << "├─ Expected HTML ───────────────────────────────────────────\n"
        << tc.html
        << "├─ Actual HTML ─────────────────────────────────────────────\n"
        << actual
        << "└───────────────────────────────────────────────────────────\n";
}

INSTANTIATE_TEST_SUITE_P(
    Spec,
    CommonMarkSpecTest,
    ::testing::ValuesIn(kAllCases),
    [](const ::testing::TestParamInfo<SpecCase>& info) {
        std::string num = std::to_string(info.param.example);
        num = std::string(3 - std::min<int>(3, num.size()), '0') + num;

        std::string section = info.param.section.substr(0, 32);
        for (char& c : section)
            if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';

        return num + "_" + section;
    }
);
