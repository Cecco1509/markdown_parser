#pragma once

#include "BlockNode.hpp"
#include "ScannedLine.hpp"
#include "Types.hpp"
#include <string_view>
#include <vector>
#include <memory>
#include <unordered_map>
#include <cstddef>

class PreScanner;
class InlineParser;

struct SpineMatchResult {
    std::size_t deepest_matched;
    std::size_t first_unmatched;
};

class SpineHandler {
public:
    explicit SpineHandler(PreScanner& scanner, InlineParser& inline_parser);

    void processLine(std::string_view raw);
    void finalize();
    std::unique_ptr<BlockNode> releaseDocument();

private:
    std::vector<std::unique_ptr<BlockNode>> spine_;
    std::unique_ptr<BlockNode>              document_;
    std::unordered_map<std::string, LinkDef> ref_map_;

    int         line_number_      = 0;
    int         last_line_length_ = 0;
    std::size_t current_byte_     = 0;
    std::size_t partial_tab_remaining_ = 0;
    std::size_t current_col_          = 0;

    PreScanner&   scanner_;
    InlineParser& inline_parser_;

    SpineMatchResult step1WalkSpine(const ScannedLine& line);
    void             step2NewBlocks(const ScannedLine& line, const SpineMatchResult& match);
    void             step3AppendText(const ScannedLine& line, const SpineMatchResult& match);

    BlockNode* openBlock(NodeType type, BlockData data);
    void       closeBlock();
    void       closeUnmatched(std::size_t from_index);
    void       appendText(std::string_view line, std::size_t from_byte);
    BlockNode* tip() const noexcept;

    bool incorporatesLazyContinuation(const ScannedLine& line,
                                      const SpineMatchResult& match) const noexcept;
    bool tryOpenNewBlock(const ScannedLine& line, const SpineMatchResult& match);
    bool continuationMatches(BlockNode* node, const ScannedLine& line);
    bool tryPromoteSetextHeading(const ScannedLine& line);

    std::size_t consumeColumns(std::string_view line,
                               std::size_t      byte_offset,
                               std::size_t      n_cols);

    void maybeScanLinkRefDefs(BlockNode* node);
    void stripTrailingBlankLines(std::string& content);
    void parseInlineContent(BlockNode* node);
};
