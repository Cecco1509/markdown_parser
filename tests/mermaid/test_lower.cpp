#include "mermaid/FlowParse.hpp"
#include "mermaid/Lower.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>

using namespace mermaid;

namespace {

FlowDb lower_src(const char *src) {
  FlowParseResult r = parse_flowchart(src);
  EXPECT_TRUE(r.ok) << (r.errors.empty() ? "" : r.errors.front());
  EXPECT_TRUE(r.document.has_value());
  return lower(*r.document);
}

const Vertex *find_vertex(const FlowDb &db, const std::string &id) {
  for (const Vertex &v : db.vertices)
    if (v.id == id) return &v;
  return nullptr;
}

std::set<std::string> node_set(const Subgraph &sg) {
  return {sg.nodes.begin(), sg.nodes.end()};
}

} // namespace

TEST(MermaidLower, LabelReuseLastWins) {
  FlowDb db = lower_src("graph TD\n  id1[first label]\n  id1[second label]\n  id1 --> A\n");
  ASSERT_EQ(db.vertices.size(), 2u); // id1, A
  const Vertex *id1 = find_vertex(db, "id1");
  ASSERT_NE(id1, nullptr);
  EXPECT_EQ(id1->label, "second label"); // last definition wins
}

TEST(MermaidLower, CrossProductOrder) {
  FlowDb db = lower_src("graph LR\n  A & B --> C & D\n");
  ASSERT_EQ(db.edges.size(), 4u);
  EXPECT_EQ(db.edges[0].start + "->" + db.edges[0].end, "A->C");
  EXPECT_EQ(db.edges[1].start + "->" + db.edges[1].end, "A->D");
  EXPECT_EQ(db.edges[2].start + "->" + db.edges[2].end, "B->C");
  EXPECT_EQ(db.edges[3].start + "->" + db.edges[3].end, "B->D");
}

TEST(MermaidLower, ShapesLabelsDirection) {
  FlowDb db = lower_src("flowchart LR\n  A[Start] --> B{Is it working?}\n");
  EXPECT_EQ(db.direction, Direction::LR);
  const Vertex *a = find_vertex(db, "A");
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->shape, ShapeKind::Rect);
  EXPECT_EQ(a->label, "Start");
  const Vertex *b = find_vertex(db, "B");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->shape, ShapeKind::Rhombus);
  EXPECT_EQ(b->label, "Is it working?");
  ASSERT_EQ(db.edges.size(), 1u);
  EXPECT_EQ(db.edges[0].head_end, ArrowHead::Arrow);
}

TEST(MermaidLower, DefaultDirectionTB) {
  FlowDb db = lower_src("flowchart\n  A --> B\n");
  EXPECT_EQ(db.direction, Direction::TB);
}

TEST(MermaidLower, SubgraphMembershipAndIds) {
  FlowDb db = lower_src(
      "graph TD\n"
      "  subgraph named[Title Here]\n    A --> B\n  end\n"
      "  subgraph \"Anon Title\"\n    C --> D\n  end\n");
  ASSERT_EQ(db.subgraphs.size(), 2u);

  const Subgraph &named = db.subgraphs[0];
  EXPECT_EQ(named.id, "named");
  EXPECT_EQ(named.label, "Title Here");
  EXPECT_EQ(node_set(named), (std::set<std::string>{"A", "B"}));

  const Subgraph &anon = db.subgraphs[1];
  EXPECT_EQ(anon.id, "subGraph1"); // counter includes the named subgraph
  EXPECT_EQ(anon.label, "Anon Title");
  EXPECT_EQ(node_set(anon), (std::set<std::string>{"C", "D"}));
}

TEST(MermaidLower, SubgraphIdBecomesVertexWhenUsedInEdge) {
  FlowDb db = lower_src(
      "graph TD\n  subgraph sg1[Group]\n    A --> B\n  end\n  sg1 --> C\n");
  EXPECT_NE(find_vertex(db, "sg1"), nullptr); // referenced in an edge -> vertex
  ASSERT_EQ(db.edges.size(), 2u);
  EXPECT_EQ(db.edges[1].start, "sg1");
  EXPECT_EQ(db.edges[1].end, "C");
}

TEST(MermaidLower, PerSubgraphDirection) {
  FlowDb db = lower_src(
      "flowchart TB\n  subgraph s[S]\n    direction LR\n    H --> I\n  end\n");
  ASSERT_EQ(db.subgraphs.size(), 1u);
  ASSERT_TRUE(db.subgraphs[0].dir.has_value());
  EXPECT_EQ(*db.subgraphs[0].dir, Direction::LR);
}

TEST(MermaidLower, ClassesAndStyles) {
  FlowDb db = lower_src(
      "flowchart TD\n"
      "  A[x]:::hot --> B\n"
      "  classDef hot fill:#f00,color:#fff\n"
      "  class B cool\n"
      "  style A stroke:#333\n");
  const Vertex *a = find_vertex(db, "A");
  ASSERT_NE(a, nullptr);
  EXPECT_NE(std::find(a->classes.begin(), a->classes.end(), "hot"), a->classes.end());
  EXPECT_NE(std::find(a->styles.begin(), a->styles.end(), "stroke:#333"), a->styles.end());

  const Vertex *b = find_vertex(db, "B");
  ASSERT_NE(b, nullptr);
  EXPECT_NE(std::find(b->classes.begin(), b->classes.end(), "cool"), b->classes.end());

  ASSERT_TRUE(db.classes.count("hot"));
  EXPECT_EQ(db.classes.at("hot").styles,
            (std::vector<std::string>{"fill:#f00", "color:#fff"}));
}
