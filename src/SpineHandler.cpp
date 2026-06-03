#include "markdown_parser/SpineHandler.hpp"
#include "markdown_parser/PreScanner.hpp"
#include "markdown_parser/InlineParser.hpp"
#include "markdown_parser/block_rules.hpp"

SpineHandler::SpineHandler(PreScanner& scanner, InlineParser& inline_parser)
    : scanner_(scanner), inline_parser_(inline_parser)
{
    openBlock(NodeType::Document, std::monostate{});
}

void SpineHandler::processLine(std::string_view raw) {
    partial_tab_remaining_ = 0;
    current_col_           = 0;
    current_byte_          = 0;
    swallow_current_line_  = false;
    ++line_number_;

    ScannedLine line = scanner_.scan(raw);

    SpineMatchResult match = step1WalkSpine(line);
    step2NewBlocks(line, match);
    step3AppendText(line, match);
    checkHtmlBlockEnd(line);

    // Track blank lines for loose-list detection.
    if (!spine_.empty())
        tip()->last_line_blank = line.is_blank;
}

void SpineHandler::finalize() {
    while (!spine_.empty()) closeBlock();
    parseInlineContent(document_.get());
}

std::unique_ptr<BlockNode> SpineHandler::releaseDocument() {
    return std::move(document_);
}

// ── Step 1 ────────────────────────────────────────────────────────────────────

SpineMatchResult SpineHandler::step1WalkSpine(const ScannedLine& line) {
    SpineMatchResult result;
    result.deepest_matched = 0;
    result.first_unmatched = spine_.size();

    for (std::size_t i = 1; i < spine_.size(); ++i) {
        auto cr = block_rules::continuationMatches(*spine_[i], line);
        if (cr.matched) {
            if (cr.cols_to_consume > 0)
                consumeColumns(line.content, current_byte_, cr.cols_to_consume);
            result.deepest_matched = i;
        } else {
            result.first_unmatched = i;
            if (cr.swallow_line) result.swallow_line = true;
            break;
        }
    }
    return result;
}

// ── Step 2 ────────────────────────────────────────────────────────────────────

bool SpineHandler::tryOpenNewBlock(const ScannedLine& line, const SpineMatchResult& match) {
    bool any_opened = false;
    ScannedLine cur = scanner_.scanWithOffset(line.content.substr(current_byte_),
                                              current_col_);

    while (true) {
        const bool tip_para = (tip()->type == NodeType::Paragraph);

        // Detect list-item blank-line continuation to suppress indented code.
        bool inside_list_blank = false;
        for (auto it = spine_.rbegin(); it != spine_.rend(); ++it) {
            const NodeType t = (*it)->type;
            if (t == NodeType::Item) { inside_list_blank = (*it)->last_line_blank; break; }
            if (t != NodeType::List) break;
        }

        auto result = block_rules::tryOpen(cur, tip_para, inside_list_blank);
        if (!result) break;

        if (!any_opened) {
            closeUnmatched(match.first_unmatched);
            any_opened = true;
        }

        // For Item openers: open or reuse a containing List.
        if (result->type == NodeType::Item && result->list_data.has_value()) {
            const auto& new_ld = *result->list_data;
            bool need_new_list = (tip()->type != NodeType::List);
            if (!need_new_list) {
                const auto& cur_ld = std::get<ListData>(tip()->data);
                need_new_list = cur_ld.list_type  != new_ld.list_type
                             || cur_ld.delimiter   != new_ld.delimiter
                             || (new_ld.list_type == ListType::Bullet
                                 && cur_ld.bullet_char != new_ld.bullet_char);
            }
            if (need_new_list) openBlock(NodeType::List, new_ld);
        }

        BlockNode* new_node = openBlock(result->type, result->data);
        if (!result->extracted_content.empty())
            new_node->string_content = result->extracted_content;
        if (result->cols_consumed > 0)
            consumeColumns(line.content, current_byte_, result->cols_consumed);
        if (result->swallow_line) {
            swallow_current_line_ = true;
            break;
        }

        // Only container blocks continue the loop.
        const bool is_container = result->type == NodeType::BlockQuote
                                || result->type == NodeType::List
                                || result->type == NodeType::Item;
        if (!is_container) break;

        cur = scanner_.scanWithOffset(line.content.substr(current_byte_), current_col_);
    }

    return any_opened;
}

void SpineHandler::step2NewBlocks(const ScannedLine& line, const SpineMatchResult& match) {
    const bool new_block_found = tryOpenNewBlock(line, match);

    if (new_block_found) {
        // closeUnmatched already called inside tryOpenNewBlock
    } else if (incorporatesLazyContinuation(line, match)) {
        // lazy continuation: leave unmatched blocks open
    } else {
        closeUnmatched(match.first_unmatched);
    }
}

// ── Step 3 ────────────────────────────────────────────────────────────────────

void SpineHandler::step3AppendText(const ScannedLine& line, const SpineMatchResult& match) {
    if (match.swallow_line || swallow_current_line_) return;
    if (line.is_blank) return;
    if (tryPromoteSetextHeading(line)) return;

    // If the tip is a container, open a Paragraph to receive the text.
    {
        const NodeType tt = tip()->type;
        if (tt == NodeType::Document || tt == NodeType::BlockQuote
                || tt == NodeType::List || tt == NodeType::Item)
            openBlock(NodeType::Paragraph, std::monostate{});
    }

    appendText(line.content, current_byte_);
}

// ── Tree mutation primitives ──────────────────────────────────────────────────

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

    if (node->type == NodeType::Paragraph)
        maybeScanLinkRefDefs(node.get());

    block_rules::onClose(*node);

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

// ── Helpers ───────────────────────────────────────────────────────────────────

bool SpineHandler::incorporatesLazyContinuation(
    const ScannedLine& line, const SpineMatchResult& match) const noexcept
{
    if (line.is_blank) return false;
    if (match.first_unmatched >= spine_.size()) return false;
    if (tip()->type != NodeType::Paragraph) return false;
    // A setext underline is never a valid lazy continuation (spec §5.1).
    if (block_rules::isSetextUnderline(line)) return false;
    return true;
}

bool SpineHandler::tryPromoteSetextHeading(const ScannedLine& line) {
    if (!block_rules::isSetextUnderline(line)) return false;
    BlockNode* t = tip();
    if (t->type != NodeType::Paragraph) return false;
    const char c = line.content[line.next_non_space];
    t->type = NodeType::Heading;
    t->data = HeadingData{(c == '=') ? 1 : 2, /*setext=*/true};
    closeBlock();
    return true;
}

void SpineHandler::checkHtmlBlockEnd(const ScannedLine& line) {
    if (spine_.empty()) return;
    BlockNode* t = tip();
    if (t->type != NodeType::HtmlBlock) return;
    const int html_type = std::get<HtmlBlockData>(t->data).html_type;
    if (html_type >= 1 && html_type <= 5
            && block_rules::htmlBlockEndMet(*t, line.content))
        closeBlock();
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

void SpineHandler::maybeScanLinkRefDefs(BlockNode*) {}

void SpineHandler::stripTrailingBlankLines(std::string&) {}
