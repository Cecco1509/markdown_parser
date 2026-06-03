#include "markdown_parser/InlineParser.hpp"

void InlineParser::parse(BlockNode* block,
                         const std::unordered_map<std::string, LinkDef>& ref_map)
{
    input_   = block->string_content;
    // Strip the trailing newline that SpineHandler appends to every line;
    // the final line-ending of a paragraph is not a soft break.
    while (!input_.empty() && input_.back() == '\n')
        input_.remove_suffix(1);
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
    if (input_[pos_] == '\n') {
        ++pos_;
        // skip leading spaces/tabs on the next line (spec: line-beginning whitespace stripped)
        while (pos_ < input_.size() && (input_[pos_] == ' ' || input_[pos_] == '\t'))
            ++pos_;
        return makeNode(InlineType::SoftBreak);
    }
    // accumulate text until the next newline, stripping trailing spaces/tabs
    std::size_t start = pos_;
    while (pos_ < input_.size() && input_[pos_] != '\n')
        ++pos_;
    std::size_t end = pos_;
    while (end > start && (input_[end - 1] == ' ' || input_[end - 1] == '\t'))
        --end;
    auto node     = makeNode(InlineType::Text);
    node->literal = std::string(input_.substr(start, end - start));
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
