#pragma once

#include "NodeVisitor.hpp"
#include <string>

class HtmlRenderer : public NodeVisitor {
public:
    // Render the Document BlockNode to a complete HTML string.
    std::string render(const BlockNode& root);

    void visit(const BlockNode&)  override;
    void visit(const InlineNode&) override;

private:
    std::string out_;
    bool        tight_ = false; // true while inside a tight list

    static std::string escapeHtml(const std::string& s);
    static std::string escapeUrl(const std::string& s);
};
