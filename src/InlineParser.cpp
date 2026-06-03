#include "markdown_parser/InlineParser.hpp"

void InlineParser::parse(BlockNode* block,
                         const std::unordered_map<std::string, LinkDef>& ref_map)
{
    input_   = block->string_content;
    pos_     = 0;
    ref_map_ = &ref_map;
    delimiters_.clear();
    brackets_.clear();

    std::vector<std::unique_ptr<InlineNode>> nodes;
    while (pos_ < input_.size()) {
        auto node = parseInline();
        if (node) nodes.push_back(std::move(node));
    }

    processEmphasis(std::nullopt);
    block->inline_children = std::move(nodes);
}

std::unique_ptr<InlineNode> InlineParser::parseInline() {
    // stub: emit every character as a Text node
    auto node     = makeNode(InlineType::Text);
    node->literal = input_[pos_++];
    return node;
}

std::unique_ptr<InlineNode> InlineParser::parseBacktickString() { return nullptr; }
std::unique_ptr<InlineNode> InlineParser::parseAutolink()        { return nullptr; }
std::unique_ptr<InlineNode> InlineParser::parseHtmlInline()      { return nullptr; }

void InlineParser::handleBracketOpener(bool) {}

std::unique_ptr<InlineNode> InlineParser::handleBracketCloser() {
    auto node     = makeNode(InlineType::Text);
    node->literal = "]";
    return node;
}

std::optional<std::string> InlineParser::scanLinkDestination() { return std::nullopt; }
std::optional<std::string> InlineParser::scanLinkTitle()       { return std::nullopt; }

void InlineParser::handleEmphasis(char, std::size_t) {}
void InlineParser::processEmphasis(std::optional<std::size_t>) {}

std::string InlineParser::normaliseLabel(std::string_view label) {
    return std::string(label);
}

std::unique_ptr<InlineNode> InlineParser::makeNode(InlineType type) {
    auto node  = std::make_unique<InlineNode>();
    node->type = type;
    return node;
}
