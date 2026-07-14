#include "mermaid/FlowParse.hpp"

#include <gtest/gtest.h>

#include <variant>

using namespace mermaid;
using namespace mermaid::ast;

TEST(MermaidParse, SimpleEdge) {
  FlowParseResult r = parse_flowchart("flowchart LR\nA --> B\n");
  ASSERT_TRUE(r.ok) << (r.errors.empty() ? "" : r.errors.front());
  ASSERT_TRUE(r.document.has_value());

  const Document &d = *r.document;
  ASSERT_TRUE(d.direction.has_value());
  EXPECT_EQ(*d.direction, Direction::LR);
  ASSERT_EQ(d.stmts.size(), 1u);

  const EdgeStmt *e = std::get_if<EdgeStmt>(&d.stmts[0]);
  ASSERT_NE(e, nullptr);
  ASSERT_EQ(e->groups.size(), 2u);
  ASSERT_EQ(e->ops.size(), 1u);
  ASSERT_EQ(e->groups[0].size(), 1u);
  ASSERT_EQ(e->groups[1].size(), 1u);
  EXPECT_EQ(e->groups[0][0].id, "A");
  EXPECT_EQ(e->groups[1][0].id, "B");
  EXPECT_EQ(e->ops[0].link.head_end, ArrowHead::Arrow);
}

TEST(MermaidParse, SimpleMermaidFixture) {
  const char *src =
      "graph TD\n"
      "  A[Start] --> B{Is it working?}\n"
      "  B -- Yes --> C[Great!]\n"
      "  B -- No  --> D[Debug]\n"
      "  D --> A\n";
  FlowParseResult r = parse_flowchart(src);
  ASSERT_TRUE(r.ok) << (r.errors.empty() ? "" : r.errors.front());
  ASSERT_TRUE(r.document.has_value());

  const Document &d = *r.document;
  ASSERT_TRUE(d.direction.has_value());
  EXPECT_EQ(*d.direction, Direction::TB); // TD aliases TB
  ASSERT_EQ(d.stmts.size(), 4u);          // one edge statement per line

  const EdgeStmt *first = std::get_if<EdgeStmt>(&d.stmts[0]);
  ASSERT_NE(first, nullptr);
  const Node &a = first->groups[0][0];
  EXPECT_EQ(a.id, "A");
  ASSERT_TRUE(a.shape.has_value());
  EXPECT_EQ(a.shape->shape, ShapeKind::Rect);
  EXPECT_EQ(a.shape->label, "Start");
  EXPECT_EQ(first->ops[0].link.label, "");

  // the "B -- Yes --> C" line carries an inline edge label
  const EdgeStmt *second = std::get_if<EdgeStmt>(&d.stmts[1]);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(second->ops[0].link.label, "Yes");
}

TEST(MermaidParse, AmpersandAndSubgraph) {
  const char *src =
      "flowchart TB\n"
      "  E & F --> G\n"
      "  subgraph box[Group]\n"
      "    H --> I\n"
      "  end\n";
  FlowParseResult r = parse_flowchart(src);
  ASSERT_TRUE(r.ok) << (r.errors.empty() ? "" : r.errors.front());
  ASSERT_TRUE(r.document.has_value());

  const Document &d = *r.document;
  ASSERT_EQ(d.stmts.size(), 2u);

  const EdgeStmt *fan = std::get_if<EdgeStmt>(&d.stmts[0]);
  ASSERT_NE(fan, nullptr);
  ASSERT_EQ(fan->groups.size(), 2u);
  EXPECT_EQ(fan->groups[0].size(), 2u); // E & F
  EXPECT_EQ(fan->groups[1].size(), 1u); // G

  const SubgraphPtr *sg = std::get_if<SubgraphPtr>(&d.stmts[1]);
  ASSERT_NE(sg, nullptr);
  ASSERT_TRUE((*sg)->head.id.has_value());
  EXPECT_EQ(*(*sg)->head.id, "box");
  ASSERT_TRUE((*sg)->head.title.has_value());
  EXPECT_EQ(*(*sg)->head.title, "Group");
  EXPECT_EQ((*sg)->body.size(), 1u);
}
