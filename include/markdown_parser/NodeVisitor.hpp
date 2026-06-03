#pragma once

struct BlockNode;
struct InlineNode;

class NodeVisitor {
public:
    virtual ~NodeVisitor() = default;
    virtual void visit(const BlockNode&)  = 0;
    virtual void visit(const InlineNode&) = 0;
};
