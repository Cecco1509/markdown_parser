#include "markdown_parser/SpineHandler.hpp"
#include "markdown_parser/PreScanner.hpp"
#include "markdown_parser/InlineParser.hpp"

SpineHandler::SpineHandler(PreScanner& scanner, InlineParser& inline_parser)
    : scanner_(scanner), inline_parser_(inline_parser)
{
    openBlock(NodeType::Document, std::monostate{});
}

void SpineHandler::processLine(std::string_view raw) {
    partial_tab_remaining_ = 0;
    current_col_           = 0;
    current_byte_          = 0;
    ++line_number_;

    ScannedLine line = scanner_.scan(raw);

    SpineMatchResult match = step1WalkSpine(line);
    step2NewBlocks(line, match);
    step3AppendText(line, match);
}

void SpineHandler::finalize() {
    while (!spine_.empty()) closeBlock();
    parseInlineContent(document_.get());
}

std::unique_ptr<BlockNode> SpineHandler::releaseDocument() {
    return std::move(document_);
}

SpineMatchResult SpineHandler::step1WalkSpine(const ScannedLine& line) {
    SpineMatchResult result;
    result.deepest_matched = 0;
    result.first_unmatched = spine_.size();

    for (std::size_t i = 1; i < spine_.size(); ++i) {
        if (continuationMatches(spine_[i].get(), line)) {
            result.deepest_matched = i;
        } else {
            result.first_unmatched = i;
            break;
        }
    }
    return result;
}

void SpineHandler::step2NewBlocks(const ScannedLine& line, const SpineMatchResult& match) {
    const bool new_block_found = tryOpenNewBlock(line, match);

    if (new_block_found) {
        closeUnmatched(match.first_unmatched);
    } else if (incorporatesLazyContinuation(line, match)) {
        // lazy continuation: leave unmatched blocks open
    } else {
        closeUnmatched(match.first_unmatched);
    }
}

void SpineHandler::step3AppendText(const ScannedLine& line, const SpineMatchResult& match) {
    if (line.is_blank) return;
    if (tryPromoteSetextHeading(line)) return;
    appendText(line.content, current_byte_);
}

BlockNode* SpineHandler::openBlock(NodeType type, BlockData data) {
    auto node_ptr   = std::make_unique<BlockNode>();
    BlockNode* node = node_ptr.get();
    node->type       = type;
    node->data       = std::move(data);
    node->start_line = line_number_;
    node->is_open    = true;
    spine_.push_back(std::move(node_ptr));
    return node;
}

void SpineHandler::closeBlock() {
    auto node = std::move(spine_.back());
    spine_.pop_back();

    if (node->type == NodeType::Paragraph) {
        maybeScanLinkRefDefs(node.get());
    } else if (node->type == NodeType::CodeBlock) {
        const auto& cbd = std::get<CodeBlockData>(node->data);
        if (!cbd.fenced) stripTrailingBlankLines(node->string_content);
    }

    node->end_line = line_number_;
    node->is_open  = false;

    if (!spine_.empty()) {
        spine_.back()->children.push_back(std::move(node));
    } else {
        document_ = std::move(node);
    }
}

void SpineHandler::closeUnmatched(std::size_t from_index) {
    while (spine_.size() > from_index) closeBlock();
}

void SpineHandler::appendText(std::string_view line, std::size_t from_byte) {
    BlockNode* t = tip();
    if (partial_tab_remaining_ > 0) {
        t->string_content.append(partial_tab_remaining_, ' ');
        partial_tab_remaining_ = 0;
        ++from_byte;
    }
    t->string_content += line.substr(from_byte);
    t->string_content += '\n';
}

BlockNode* SpineHandler::tip() const noexcept {
    return spine_.back().get();
}

bool SpineHandler::incorporatesLazyContinuation(
    const ScannedLine& line, const SpineMatchResult& match) const noexcept
{
    return !line.is_blank
        && match.first_unmatched < spine_.size()
        && tip()->type == NodeType::Paragraph;
}

std::size_t SpineHandler::consumeColumns(
    std::string_view line, std::size_t byte_offset, std::size_t n_cols)
{
    std::size_t cols_needed = n_cols;

    if (partial_tab_remaining_ > 0) {
        const std::size_t take = std::min(partial_tab_remaining_, cols_needed);
        partial_tab_remaining_ -= take;
        cols_needed            -= take;
        current_col_           += take;
    }

    while (cols_needed > 0 && byte_offset < line.size()) {
        const unsigned char byte = static_cast<unsigned char>(line[byte_offset]);
        if (byte == ' ') {
            ++byte_offset; --cols_needed; ++current_col_;
        } else if (byte == '\t') {
            const std::size_t tab_w = (current_col_ / 4 + 1) * 4 - current_col_;
            if (tab_w <= cols_needed) {
                ++byte_offset; cols_needed -= tab_w; current_col_ += tab_w;
            } else {
                partial_tab_remaining_ = tab_w - cols_needed;
                current_col_ += cols_needed;
                cols_needed   = 0;
            }
        } else {
            break;
        }
    }
    current_byte_ = byte_offset;
    return byte_offset;
}

void SpineHandler::parseInlineContent(BlockNode* node) {
    const bool needs_inline =
        node->type == NodeType::Paragraph ||
        node->type == NodeType::Heading;

    const bool is_container =
        node->type == NodeType::Document   ||
        node->type == NodeType::BlockQuote ||
        node->type == NodeType::List       ||
        node->type == NodeType::Item;

    if (needs_inline) {
        inline_parser_.parse(node, ref_map_);
    } else if (is_container) {
        for (const auto& child : node->children)
            parseInlineContent(child.get());
    }
}

// ── stubs for methods not yet implemented ─────────────────────────────────────

bool SpineHandler::continuationMatches(BlockNode*, const ScannedLine&) { return true; }
bool SpineHandler::tryOpenNewBlock(const ScannedLine&, const SpineMatchResult&) { return false; }
bool SpineHandler::tryPromoteSetextHeading(const ScannedLine&) { return false; }
void SpineHandler::maybeScanLinkRefDefs(BlockNode*) {}
void SpineHandler::stripTrailingBlankLines(std::string&) {}
