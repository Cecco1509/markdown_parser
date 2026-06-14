#pragma once

#include "markdown_parser/core/BlockNode.hpp"
#include "markdown_parser/core/InlineNode.hpp"
#include "markdown_parser/core/Types.hpp"
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace markdown_parser {

class InlineParser {
public:
  void parse(BlockNode &block,
             const std::unordered_map<std::string, LinkDef> &ref_map);

private:
  std::string_view input_;
  std::size_t pos_ = 0;
  std::vector<Delimiter> delimiters_;
  std::vector<BracketEntry> brackets_;

  std::unique_ptr<InlineNode>
  parseInline(const std::unordered_map<std::string, LinkDef> &ref_map);
  std::unique_ptr<InlineNode> parseBacktickString();
  std::unique_ptr<InlineNode> parseAutolink();
  std::unique_ptr<InlineNode> parseHtmlInline();

  void handleBracketOpener(bool is_image);
  std::unique_ptr<InlineNode>
  handleBracketCloser(const std::unordered_map<std::string, LinkDef> &ref_map);

  std::optional<std::string> scanLinkDestination();
  std::optional<std::string> scanLinkTitle();

  void handleEmphasis(char delim_char, std::size_t run_len);
  void processEmphasis(std::optional<std::size_t> stack_bottom);

  std::unique_ptr<InlineNode> makeNode(InlineType type);

  // Owned node list built during parse(); kept as a member so processEmphasis
  // and handleBracketCloser can splice and reorder it without extra parameters.
  std::vector<std::unique_ptr<InlineNode>> nodes_;

public:
  // Shared with SpineHandler for phase-1 ref_map_ key insertion.
  // Collapses interior whitespace runs to a single space, trims
  // leading/trailing whitespace, and applies Unicode simple case folding.
  static std::string normaliseLabel(std::string_view label);
};

} // namespace markdown_parser
