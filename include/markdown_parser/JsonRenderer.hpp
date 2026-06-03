#pragma once

#include "NodeVisitor.hpp"
#include <string>

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
    void emitInlineChildren(const InlineNode& node);
};
