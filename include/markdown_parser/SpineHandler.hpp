#pragma once

#include "BlockNode.hpp"
#include "ScannedLine.hpp"
#include "Types.hpp"
#include <cstddef>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

class PreScanner;
class InlineParser;

struct SpineMatchResult {
  std::size_t deepest_matched;
  std::size_t first_unmatched;
  // Set by step1 when a fenced-code closing fence is detected; tells step3
  // to swallow the line instead of appending it.
  bool swallow_line = false;
};

class SpineHandler {
public:
  explicit SpineHandler(PreScanner &scanner, InlineParser &inline_parser,
                        bool debug = false);

  void processLine(std::string_view raw);
  void finalize();
  std::unique_ptr<BlockNode> releaseDocument();

private:
  std::vector<std::unique_ptr<BlockNode>> spine_;
  std::unique_ptr<BlockNode> document_;
  std::unordered_map<std::string, LinkDef> ref_map_;

  bool debug_ = false;
  int line_number_ = 0;
  int last_line_length_ = 0;

  PreScanner &scanner_;
  InlineParser &inline_parser_;

  struct Step2Result { ScannedLine remaining; bool swallow; };
  struct OpenBlockResult { ScannedLine remaining; bool any_opened; bool swallow; };

  std::pair<SpineMatchResult, ScannedLine> step1WalkSpine(const ScannedLine &line);
  Step2Result     step2NewBlocks(const ScannedLine &cur, const SpineMatchResult &match);
  void            step3AppendText(const ScannedLine &cur, const SpineMatchResult &match, bool swallow);

  BlockNode *openBlock(NodeType type, BlockData data);
  void closeBlock();
  void closeUnmatched(std::size_t from_index);
  void appendText(const ScannedLine &cur, std::size_t from_byte);
  BlockNode *tip() const noexcept;

  bool
  incorporatesLazyContinuation(const ScannedLine &cur,
                               const SpineMatchResult &match) const noexcept;
  OpenBlockResult tryOpenNewBlock(const ScannedLine &cur, const SpineMatchResult &match);
  bool tryPromoteSetextHeading(const ScannedLine &cur,
                               const SpineMatchResult &match);
  void checkHtmlBlockEnd(const ScannedLine &line);

  ScannedLine consumeColumns(const ScannedLine &cur, std::size_t n_cols);

  void maybeScanLinkRefDefs(BlockNode *node);
  bool tryScanOneLinkRefDef(std::string_view content, std::size_t &pos);
  void stripTrailingBlankLines(std::string &content);
  void parseInlineContent(BlockNode *node);
  void printSpineStatus() const;
};
