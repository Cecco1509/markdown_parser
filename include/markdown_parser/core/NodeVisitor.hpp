#pragma once

namespace markdown_parser {

struct BlockNode;
struct InlineNode;

class NodeVisitor {
public:
    virtual ~NodeVisitor() = default;
    virtual void visit(const BlockNode&)  = 0;
    virtual void visit(const InlineNode&) = 0;
};

} // namespace markdown_parser
