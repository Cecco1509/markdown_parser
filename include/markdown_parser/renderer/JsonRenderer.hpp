#pragma once

#include "markdown_parser/core/NodeVisitor.hpp"
#include <memory>
#include <string>
#include <vector>

namespace markdown_parser {

class InlineNode;

// Serialises the AST to mdast-compatible JSON (https://github.com/syntax-tree/mdast).
class JsonRenderer : public NodeVisitor {
public:
    std::string render(const BlockNode& root);

    void visit(const BlockNode&)  override;
    void visit(const InlineNode&) override;

private:
    std::string out_;

    static std::string jsonStr(const std::string& s);

    // Helpers that emit children arrays (used by both visitors).
    void emitBlockChildren(const BlockNode& node);

    // Emits a phrasing-content "children" array with mdast normalization:
    // soft breaks fold into literal "\n", adjacent text nodes coalesce, and
    // empty text nodes are dropped. Shared by inline containers (which store
    // children on the InlineNode) and by paragraph/heading blocks (which store
    // them as inline_children on the BlockNode).
    void emitPhrasing(const std::vector<std::unique_ptr<InlineNode>>& children);
};

} // namespace markdown_parser
