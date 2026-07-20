#pragma once

#include "markdown_parser/core/BlockNode.hpp"
#include "markdown_parser/core/Types.hpp"
#include "markdown_parser/parser/ScannedLine.hpp"
#include <cstddef>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace markdown_parser {

class InlineParser;

struct SpineMatchResult {
  std::size_t deepest_matched;
  std::size_t first_unmatched;
  bool swallow_line = false;
};

class SpineHandler {
public:
  explicit SpineHandler(InlineParser &inline_parser, bool debug = false);

  void processLine(std::string_view raw);
  void finalize();
  std::unique_ptr<BlockNode> releaseDocument();

private:
  std::vector<std::unique_ptr<BlockNode>> spine_;
  std::unique_ptr<BlockNode> document_;
  std::unordered_map<std::string, LinkDef> ref_map_;

  bool debug_ = false;
  int line_number_ = 0;

  InlineParser &inline_parser_;

  struct OpenBlockResult {
    bool any_opened;
    bool swallow;
  };

  // step1 mutates line (consumes container markers), returns match info.
  SpineMatchResult step1WalkSpine(ScannedLine &line);
  // step2 mutates cur (consumes new-block markers), returns whether to swallow.
  bool step2NewBlocks(ScannedLine &cur, const SpineMatchResult &match);
  void step3AppendText(const ScannedLine &cur, const SpineMatchResult &match,
                       bool swallow);

  BlockNode &openBlock(NodeType type, BlockData data);
  void closeBlock();
  void closeUnmatched(std::size_t from_index);
  void appendText(const ScannedLine &cur, std::size_t from_byte);
  BlockNode &tip() const noexcept;

  bool
  incorporatesLazyContinuation(const ScannedLine &cur,
                               const SpineMatchResult &match) const noexcept;
  // tryOpenNewBlock mutates cur (consumes opener markers).
  OpenBlockResult tryOpenNewBlock(ScannedLine &cur,
                                  const SpineMatchResult &match);
  bool tryPromoteSetextHeading(const ScannedLine &cur,
                               const SpineMatchResult &match);
  void checkHtmlBlockEnd(std::string_view orig_content);

  // Scans leading link reference definitions out of `node.string_content`
  // (trimming them), appending one DefinitionData per definition (in source
  // order) to `out` for the caller to splice into the tree as `definition`
  // nodes. Also populates the resolver map (first-definition-wins).
  void maybeScanLinkRefDefs(BlockNode &node, std::vector<DefinitionData> &out);
  bool tryScanOneLinkRefDef(std::string_view content, std::size_t &pos,
                            std::vector<DefinitionData> &out);
  void stripTrailingBlankLines(std::string &content);
  void parseInlineContent(BlockNode &node);
  void printSpineStatus() const;
};

} // namespace markdown_parser
