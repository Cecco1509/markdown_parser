#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace commonmark::testing {

struct SpecCase {
    std::string markdown;
    std::string html;
    int         example;
    int         start_line;
    int         end_line;
    std::string section;
};

inline void from_json(const nlohmann::json& j, SpecCase& c) {
    j.at("markdown")  .get_to(c.markdown);
    j.at("html")      .get_to(c.html);
    j.at("example")   .get_to(c.example);
    j.at("start_line").get_to(c.start_line);
    j.at("end_line")  .get_to(c.end_line);
    j.at("section")   .get_to(c.section);
}

inline std::vector<SpecCase> loadSpec(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Cannot open spec file: " + path);

    nlohmann::json j;
    file >> j;
    return j.get<std::vector<SpecCase>>();
}

} // namespace commonmark::testing
