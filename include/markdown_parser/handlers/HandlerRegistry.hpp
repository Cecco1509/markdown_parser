#pragma once

#include <functional>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <vector>

namespace markdown_parser {

using HandlerFn = std::function<std::string(const std::string &)>;

class HandlerRegistry {
public:
  static HandlerRegistry &instance();

  // Register a handler function under a group name with one or more lang aliases.
  static bool add(const std::string &group,
                  std::initializer_list<std::string> aliases,
                  HandlerFn fn);

  // Look up a handler by exact lang tag (e.g. "latex").
  static HandlerFn get(const std::string &lang);

  // Return all lang aliases registered under a group name (e.g. "math").
  static const std::vector<std::string> &getGroup(const std::string &group);

  // Return the handler function registered for a group name. Unlike get(),
  // this is keyed by group, so distinct groups may share a lang alias (e.g.
  // both "mermaid" and "mermaidjs" handle the ```mermaid``` tag).
  static HandlerFn getGroupFn(const std::string &group);

private:
  HandlerRegistry() = default;

  std::unordered_map<std::string, HandlerFn>             lang_map_;
  std::unordered_map<std::string, std::vector<std::string>> group_map_;
  std::unordered_map<std::string, HandlerFn>             group_fn_;

  static const std::vector<std::string> empty_;
};

} // namespace markdown_parser
