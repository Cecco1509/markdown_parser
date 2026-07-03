#include "mermaid/Lexer.hpp"

#include <gtest/gtest.h>

#include <cctype>
#include <string>
#include <vector>

using namespace mermaid;

namespace {

std::string trim_copy(const std::string &s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace((unsigned char)s[b])) ++b;
  while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
  return s.substr(b, e - b);
}

// Render the token stream (minus the trailing End) as space-joined to_string().
std::string dump(std::string_view src) {
  LexResult r = lex(src);
  std::string out;
  for (const Token &t : r.tokens) {
    if (t.is(Punct::End)) break;
    if (!out.empty()) out += ' ';
    out += to_string(t);
  }
  return out;
}

} // namespace

TEST(MermaidLexer, HeaderAndDirection) {
  EXPECT_EQ(dump("flowchart TB\n"), "KwGraph Dir(TB) Newline");
  EXPECT_EQ(dump("graph TD\n"), "KwGraph Dir(TB) Newline"); // TD aliases TB
  EXPECT_EQ(dump("graph LR\n"), "KwGraph Dir(LR) Newline");
}

TEST(MermaidLexer, ShapeZoo) {
  EXPECT_EQ(dump("A[Rectangle]"), "Id(A) Shape[Rect,\"Rectangle\"]");
  EXPECT_EQ(dump("A(Round)"), "Id(A) Shape[RoundEdges,\"Round\"]");
  EXPECT_EQ(dump("A([Stadium])"), "Id(A) Shape[Stadium,\"Stadium\"]");
  EXPECT_EQ(dump("A[[Sub]]"), "Id(A) Shape[Subroutine,\"Sub\"]");
  EXPECT_EQ(dump("A[(Cyl)]"), "Id(A) Shape[Cylinder,\"Cyl\"]");
  EXPECT_EQ(dump("A((Circle))"), "Id(A) Shape[Circle,\"Circle\"]");
  EXPECT_EQ(dump("A>Asym]"), "Id(A) Shape[Asymmetric,\"Asym\"]");
  EXPECT_EQ(dump("A{Diamond}"), "Id(A) Shape[Rhombus,\"Diamond\"]");
  EXPECT_EQ(dump("A{{Hex}}"), "Id(A) Shape[Hexagon,\"Hex\"]");
  EXPECT_EQ(dump("A[/Lean right/]"), "Id(A) Shape[LeanRight,\"Lean right\"]");
  EXPECT_EQ(dump("A[\\Lean left\\]"), "Id(A) Shape[LeanLeft,\"Lean left\"]");
  EXPECT_EQ(dump("A[/Trap\\]"), "Id(A) Shape[Trapezoid,\"Trap\"]");
  EXPECT_EQ(dump("A[\\TrapAlt/]"), "Id(A) Shape[TrapezoidAlt,\"TrapAlt\"]");
  EXPECT_EQ(dump("A(((Double)))"), "Id(A) Shape[DoubleCircle,\"Double\"]");
}

TEST(MermaidLexer, EdgeHeadsAndStrokes) {
  EXPECT_EQ(dump("A --> B"), "Id(A) Link{normal,arrow,len=1} Id(B)");
  EXPECT_EQ(dump("A --- B"), "Id(A) Link{normal,none,len=1} Id(B)");
  EXPECT_EQ(dump("A --o B"), "Id(A) Link{normal,circle,len=1} Id(B)");
  EXPECT_EQ(dump("A --x B"), "Id(A) Link{normal,cross,len=1} Id(B)");
  EXPECT_EQ(dump("A <--> B"), "Id(A) Link{normal,arrow,bi,len=1} Id(B)");
  EXPECT_EQ(dump("A -.-> B"), "Id(A) Link{dotted,arrow,len=1} Id(B)");
  EXPECT_EQ(dump("A ==> B"), "Id(A) Link{thick,arrow,len=1} Id(B)");
  EXPECT_EQ(dump("A === B"), "Id(A) Link{thick,none,len=1} Id(B)");
  EXPECT_EQ(dump("A ~~~ B"), "Id(A) Link{invisible,none,len=1} Id(B)");
}

TEST(MermaidLexer, EdgeLength) {
  EXPECT_EQ(dump("A --> B"), "Id(A) Link{normal,arrow,len=1} Id(B)");
  EXPECT_EQ(dump("A ---> B"), "Id(A) Link{normal,arrow,len=2} Id(B)");
  EXPECT_EQ(dump("A ----> B"), "Id(A) Link{normal,arrow,len=3} Id(B)");
  EXPECT_EQ(dump("E -..-> G"), "Id(E) Link{dotted,arrow,len=2} Id(G)");
  EXPECT_EQ(dump("H ===> J"), "Id(H) Link{thick,arrow,len=2} Id(J)");
}

TEST(MermaidLexer, InlineAndPipeLabels) {
  EXPECT_EQ(dump("A -- yes --> B"),
            "Id(A) Link{normal,arrow,len=1,label=\"yes\"} Id(B)");
  EXPECT_EQ(dump("K -- mid label ---> L"),
            "Id(K) Link{normal,arrow,len=2,label=\"mid label\"} Id(L)");
  EXPECT_EQ(dump("A -->|pipe label| B"),
            "Id(A) Link{normal,arrow,len=1} Pipe Str(\"pipe label\") Pipe Id(B)");
}

TEST(MermaidLexer, ClassAttachAndAmp) {
  EXPECT_EQ(dump("A[Start]:::primary"),
            "Id(A) Shape[Rect,\"Start\"] TripleColon Id(primary)");
  EXPECT_EQ(dump("E & F --> G"),
            "Id(E) Ampersand Id(F) Link{normal,arrow,len=1} Id(G)");
}

TEST(MermaidLexer, QuotedLabelWithSpecials) {
  EXPECT_EQ(dump("A[\"a > b\"]"), "Id(A) Shape[Rect,\"a > b\"]");
}

TEST(MermaidLexer, StylingLines) {
  EXPECT_EQ(dump("style A fill:#ff9,stroke:#333"),
            "KwStyle Id(A) StyleBody(\"fill:#ff9,stroke:#333\")");
  EXPECT_EQ(dump("classDef default fill:#eee,stroke:#999"),
            "KwClassDef KwDefault StyleBody(\"fill:#eee,stroke:#999\")");
  EXPECT_EQ(dump("linkStyle 2,3 stroke:#00f"),
            "KwLinkStyle Num(2) Comma Num(3) StyleBody(\"stroke:#00f\")");
  EXPECT_EQ(dump("class D,B danger"),
            "KwClass Id(D) Comma Id(B) Id(danger)");
}

TEST(MermaidLexer, CommentsAndBlankLines) {
  // A line-leading %% is a comment (dropped); the surrounding newlines remain.
  EXPECT_EQ(dump("%% a comment\nA --> B\n"),
            "Newline Id(A) Link{normal,arrow,len=1} Id(B) Newline");
}

TEST(MermaidLexer, FrontMatterLifted) {
  LexResult r = lex("---\ntitle: Demo\n---\nflowchart LR\n");
  EXPECT_EQ(trim_copy(r.front_matter), std::string("title: Demo"));
  // First real token is the header keyword, not front-matter noise.
  ASSERT_FALSE(r.tokens.empty());
  EXPECT_TRUE(r.tokens.front().is(Punct::KwGraph));
}
