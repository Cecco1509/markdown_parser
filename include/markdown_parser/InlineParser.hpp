#pragma once

#include "BlockNode.hpp"
#include "InlineNode.hpp"
#include "Types.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <unordered_map>

class InlineParser {
public:
    void parse(BlockNode*                                       block,
               const std::unordered_map<std::string, LinkDef>& ref_map);

private:
    std::string_view          input_;
    std::size_t               pos_ = 0;
    std::vector<Delimiter>    delimiters_;
    std::vector<BracketEntry> brackets_;
    const std::unordered_map<std::string, LinkDef>* ref_map_ = nullptr;

    std::unique_ptr<InlineNode> parseInline();
    std::unique_ptr<InlineNode> parseBacktickString();
    std::unique_ptr<InlineNode> parseAutolink();
    std::unique_ptr<InlineNode> parseHtmlInline();

    void                        handleBracketOpener(bool is_image);
    std::unique_ptr<InlineNode> handleBracketCloser();

    std::optional<std::string>  scanLinkDestination();
    std::optional<std::string>  scanLinkTitle();

    void handleEmphasis(char delim_char, std::size_t run_len);
    void processEmphasis(std::optional<std::size_t> stack_bottom);

    static std::string          normaliseLabel(std::string_view label);
    std::unique_ptr<InlineNode> makeNode(InlineType type);
};
