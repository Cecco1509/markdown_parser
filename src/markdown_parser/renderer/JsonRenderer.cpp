#include "markdown_parser/renderer/JsonRenderer.hpp"
#include "markdown_parser/core/BlockNode.hpp"
#include "markdown_parser/core/InlineNode.hpp"
#include "markdown_parser/core/Types.hpp"

#include <variant>

namespace markdown_parser {

// ── helpers ───────────────────────────────────────────────────────────────────

// Produce a JSON-encoded string literal (with surrounding quotes).
std::string JsonRenderer::jsonStr(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    // Control characters: \uXXXX
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
    return out;
}

void JsonRenderer::emitBlockChildren(const BlockNode& node) {
    out_ += "\"children\":[";
    bool first = true;
    for (const auto& child : node.children) {
        if (!first) out_ += ',';
        first = false;
        visit(*child);
    }
    out_ += ']';
}

// Emits a phrasing-content "children" array, normalizing to mdast shape:
//   * soft line breaks fold into a literal "\n" (mdast has no softBreak node),
//   * adjacent text runs coalesce into a single text node,
//   * empty text nodes (left behind by delimiter splitting) are dropped.
void JsonRenderer::emitPhrasing(
    const std::vector<std::unique_ptr<InlineNode>>& children) {
    out_ += "\"children\":[";
    std::string text_run;
    bool emitted = false; // any node already written to this array

    auto flush_text = [&]() {
        if (text_run.empty()) return;
        if (emitted) out_ += ',';
        out_ += "{\"type\":\"text\",\"value\":" + jsonStr(text_run) + '}';
        emitted = true;
        text_run.clear();
    };

    for (const auto& child : children) {
        switch (child->type) {
        case InlineType::Text:      text_run += child->literal; break;
        case InlineType::SoftBreak: text_run += '\n';           break;
        default:
            flush_text();
            if (emitted) out_ += ',';
            visit(*child);
            emitted = true;
        }
    }
    flush_text();
    out_ += ']';
}

// Drop the single trailing newline mdast omits from code/html block values.
static std::string stripTrailingNewline(std::string s) {
    if (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

// Concatenates the literal text of a node and all its descendants (used for
// image alt text, mirroring mdast-util-to-string).
static void collectDescendantText(const InlineNode& node, std::string& out) {
    if (node.type == InlineType::Text || node.type == InlineType::Code)
        out += node.literal;
    for (const auto& child : node.children)
        collectDescendantText(*child, out);
}

// ── public API ────────────────────────────────────────────────────────────────

std::string JsonRenderer::render(const BlockNode& root) {
    out_.clear();
    visit(root);
    return out_;
}

// ── BlockNode ─────────────────────────────────────────────────────────────────

void JsonRenderer::visit(const BlockNode& node) {
    switch (node.type) {

    case NodeType::Document:
        out_ += "{\"type\":\"root\",";
        emitBlockChildren(node);
        out_ += '}';
        break;

    case NodeType::Paragraph:
        out_ += "{\"type\":\"paragraph\",";
        emitPhrasing(node.inline_children);
        out_ += '}';
        break;

    case NodeType::Heading: {
        const auto& hd = std::get<HeadingData>(node.data);
        out_ += "{\"type\":\"heading\",\"depth\":" + std::to_string(hd.level) + ',';
        emitPhrasing(node.inline_children);
        out_ += '}';
        break;
    }

    case NodeType::ThematicBreak:
        out_ += "{\"type\":\"thematicBreak\"}";
        break;

    case NodeType::BlockQuote:
        out_ += "{\"type\":\"blockquote\",";
        emitBlockChildren(node);
        out_ += '}';
        break;

    case NodeType::List: {
        const auto& ld = std::get<ListData>(node.data);
        bool ordered   = (ld.list_type == ListType::Ordered);
        out_ += "{\"type\":\"list\"";
        out_ += ",\"ordered\":" + std::string(ordered ? "true" : "false");
        // mdast always carries `start` (null for bullet lists).
        out_ += ",\"start\":" + (ordered ? std::to_string(ld.start)
                                          : std::string("null"));
        out_ += ",\"spread\":" + std::string(ld.spread ? "true" : "false");
        out_ += ',';
        emitBlockChildren(node);
        out_ += '}';
        break;
    }

    case NodeType::Item: {
        // mdast item-level spread: a blank line between this item's own
        // children (computed by the spine handler). `checked` is null unless
        // this is a GFM task-list item (unsupported).
        const auto& id = std::get<ItemData>(node.data);
        out_ += "{\"type\":\"listItem\",\"spread\":"
                + std::string(id.spread ? "true" : "false")
                + ",\"checked\":null,";
        emitBlockChildren(node);
        out_ += '}';
        break;
    }

    case NodeType::CodeBlock: {
        const auto& cd = std::get<CodeBlockData>(node.data);
        out_ += "{\"type\":\"code\"";
        // mdast always carries `lang` and `meta` (null when absent).
        if (!cd.info_string.empty()) {
            // lang is first word; meta is the rest (may be absent).
            auto sep  = cd.info_string.find_first_of(" \t");
            std::string lang = cd.info_string.substr(0, sep);
            out_ += ",\"lang\":" + jsonStr(lang);
            std::string meta;
            if (sep != std::string::npos) {
                std::string rest = cd.info_string.substr(sep + 1);
                auto start = rest.find_first_not_of(" \t");
                if (start != std::string::npos) meta = rest.substr(start);
            }
            out_ += ",\"meta\":" + (meta.empty() ? std::string("null")
                                                 : jsonStr(meta));
        } else {
            out_ += ",\"lang\":null,\"meta\":null";
        }
        out_ += ",\"value\":" + jsonStr(stripTrailingNewline(node.string_content));
        out_ += '}';
        break;
    }

    case NodeType::HtmlBlock:
        out_ += "{\"type\":\"html\",\"value\":"
                + jsonStr(stripTrailingNewline(node.string_content)) + '}';
        break;
    }
}

// ── InlineNode ────────────────────────────────────────────────────────────────

void JsonRenderer::visit(const InlineNode& node) {
    switch (node.type) {

    case InlineType::Text:
        out_ += "{\"type\":\"text\",\"value\":" + jsonStr(node.literal) + '}';
        break;

    case InlineType::SoftBreak:
        out_ += "{\"type\":\"softBreak\"}";
        break;

    case InlineType::LineBreak:
        out_ += "{\"type\":\"break\"}";
        break;

    case InlineType::Code:
        out_ += "{\"type\":\"inlineCode\",\"value\":" + jsonStr(node.literal) + '}';
        break;

    case InlineType::HtmlInline:
        out_ += "{\"type\":\"html\",\"value\":" + jsonStr(node.literal) + '}';
        break;

    case InlineType::Emph:
        out_ += "{\"type\":\"emphasis\",";
        emitPhrasing(node.children);
        out_ += '}';
        break;

    case InlineType::Strong:
        out_ += "{\"type\":\"strong\",";
        emitPhrasing(node.children);
        out_ += '}';
        break;

    case InlineType::Link: {
        const auto& ld = std::get<LinkData>(node.data);
        out_ += "{\"type\":\"link\"";
        out_ += ",\"url\":" + jsonStr(ld.destination);
        out_ += ",\"title\":" + (ld.title ? jsonStr(*ld.title) : std::string("null"));
        out_ += ',';
        emitPhrasing(node.children);
        out_ += '}';
        break;
    }

    case InlineType::Image: {
        const auto& ld = std::get<LinkData>(node.data);
        // Alt text is the concatenated text of ALL descendants (mdast computes
        // it like mdast-util-to-string), not just the direct Text children.
        std::string alt;
        collectDescendantText(node, alt);
        out_ += "{\"type\":\"image\"";
        out_ += ",\"url\":" + jsonStr(ld.destination);
        out_ += ",\"title\":" + (ld.title ? jsonStr(*ld.title) : std::string("null"));
        out_ += ",\"alt\":" + jsonStr(alt);
        out_ += '}';
        break;
    }
    }
}

} // namespace markdown_parser
