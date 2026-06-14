#include "markdown_parser/JsonRenderer.hpp"
#include "markdown_parser/BlockNode.hpp"
#include "markdown_parser/InlineNode.hpp"
#include "markdown_parser/Types.hpp"

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

void JsonRenderer::emitInlineChildren(const InlineNode& node) {
    out_ += "\"children\":[";
    bool first = true;
    for (const auto& child : node.children) {
        if (!first) out_ += ',';
        first = false;
        visit(*child);
    }
    out_ += ']';
}

// Inline children stored on a BlockNode (paragraph, heading).
static void emitInlineChildrenOfBlock(JsonRenderer& v, std::string& out,
                                       const BlockNode& node) {
    out += "\"children\":[";
    bool first = true;
    for (const auto& child : node.inline_children) {
        if (!first) out += ',';
        first = false;
        v.visit(*child);
    }
    out += ']';
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
        emitInlineChildrenOfBlock(*this, out_, node);
        out_ += '}';
        break;

    case NodeType::Heading: {
        const auto& hd = std::get<HeadingData>(node.data);
        out_ += "{\"type\":\"heading\",\"depth\":" + std::to_string(hd.level) + ',';
        emitInlineChildrenOfBlock(*this, out_, node);
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
        if (ordered)
            out_ += ",\"start\":" + std::to_string(ld.start);
        out_ += ",\"spread\":" + std::string(ld.tight ? "false" : "true");
        out_ += ',';
        emitBlockChildren(node);
        out_ += '}';
        break;
    }

    case NodeType::Item: {
        // Determine spread: item is spread if any child is a paragraph with
        // surrounding blank lines — approximated here by !tight on the parent.
        // We don't have parent context here, so we conservatively use false;
        // the List node already carries the tight/spread information.
        out_ += "{\"type\":\"listItem\",\"spread\":false,";
        emitBlockChildren(node);
        out_ += '}';
        break;
    }

    case NodeType::CodeBlock: {
        const auto& cd = std::get<CodeBlockData>(node.data);
        out_ += "{\"type\":\"code\"";
        if (!cd.info_string.empty()) {
            // lang is first word; meta is the rest (may be absent).
            auto sep  = cd.info_string.find_first_of(" \t");
            std::string lang = cd.info_string.substr(0, sep);
            out_ += ",\"lang\":" + jsonStr(lang);
            if (sep != std::string::npos) {
                std::string meta = cd.info_string.substr(sep + 1);
                // Strip leading whitespace from meta.
                auto start = meta.find_first_not_of(" \t");
                if (start != std::string::npos)
                    out_ += ",\"meta\":" + jsonStr(meta.substr(start));
            }
        } else {
            out_ += ",\"lang\":null";
        }
        out_ += ",\"value\":" + jsonStr(node.string_content);
        out_ += '}';
        break;
    }

    case NodeType::HtmlBlock:
        out_ += "{\"type\":\"html\",\"value\":" + jsonStr(node.string_content) + '}';
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
        emitInlineChildren(node);
        out_ += '}';
        break;

    case InlineType::Strong:
        out_ += "{\"type\":\"strong\",";
        emitInlineChildren(node);
        out_ += '}';
        break;

    case InlineType::Link: {
        const auto& ld = std::get<LinkData>(node.data);
        out_ += "{\"type\":\"link\"";
        out_ += ",\"url\":" + jsonStr(ld.destination);
        out_ += ",\"title\":" + (ld.title ? jsonStr(*ld.title) : std::string("null"));
        out_ += ',';
        emitInlineChildren(node);
        out_ += '}';
        break;
    }

    case InlineType::Image: {
        const auto& ld = std::get<LinkData>(node.data);
        // Alt text: plain text concatenation of all descendant Text nodes.
        std::string alt;
        for (const auto& child : node.children)
            if (child->type == InlineType::Text)
                alt += child->literal;
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
