#pragma once
#include <string>

// Shared failure-report formatting so the HTML (CommonMarkSpecTest) and JSON
// (JsonMdastTest) suites print with an identical structure. Both build the same
// box; only the section labels and payloads differ.

namespace commonmark::testing {

inline std::string sectionRule(const std::string &label) {
  std::string s = "├─ " + label + " ";
  for (int i = 0; i < 40; ++i)
    s += "─";
  return s + "\n";
}

// Appends `body`, guaranteeing a trailing newline so the next rule starts on
// its own line even when the payload does not end in '\n'.
inline void appendBody(std::string &out, const std::string &body) {
  out += body;
  if (body.empty() || body.back() != '\n')
    out += "\n";
}

// Renders the shared failure box. `lines` may be empty to omit the spec-lines
// row. `expected` is the reference output (spec HTML / remark mdast), `actual`
// is ours.
inline std::string caseReport(const std::string &section, int example,
                              const std::string &lines,
                              const std::string &input,
                              const std::string &expected_label,
                              const std::string &expected,
                              const std::string &actual_label,
                              const std::string &actual) {
  std::string out = "\n";
  out += "┌─ Section    : " + section + "\n";
  out += "│  Example #  : " + std::to_string(example) + "\n";
  if (!lines.empty())
    out += "│  Spec lines : " + lines + "\n";
  out += sectionRule("Markdown input");
  appendBody(out, input);
  out += sectionRule(expected_label);
  appendBody(out, expected);
  out += sectionRule(actual_label);
  appendBody(out, actual);
  out += "└";
  for (int i = 0; i < 58; ++i)
    out += "─";
  return out + "\n";
}

} // namespace commonmark::testing
