#include "markdown_parser/handlers/HandlerRegistry.hpp"

namespace markdown_parser {

const std::vector<std::string> HandlerRegistry::empty_;

HandlerRegistry &HandlerRegistry::instance() {
  static HandlerRegistry registry;
  return registry;
}

bool HandlerRegistry::add(const std::string &group,
                          std::initializer_list<std::string> aliases,
                          HandlerFn fn) {
  auto &self = instance();
  for (const auto &alias : aliases)
    self.lang_map_[alias] = fn;
  self.group_map_[group] = std::vector<std::string>(aliases);
  self.group_fn_[group]  = fn;
  return true;
}

HandlerFn HandlerRegistry::get(const std::string &lang) {
  auto &self = instance();
  auto it = self.lang_map_.find(lang);
  if (it != self.lang_map_.end())
    return it->second;
  return nullptr;
}

const std::vector<std::string> &HandlerRegistry::getGroup(const std::string &group) {
  auto &self = instance();
  auto it = self.group_map_.find(group);
  if (it != self.group_map_.end())
    return it->second;
  return empty_;
}

HandlerFn HandlerRegistry::getGroupFn(const std::string &group) {
  auto &self = instance();
  auto it = self.group_fn_.find(group);
  if (it != self.group_fn_.end())
    return it->second;
  return nullptr;
}

} // namespace markdown_parser
