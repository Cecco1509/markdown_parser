#include "markdown_parser/InlineParser.hpp"
#include "markdown_parser/SpineHandler.hpp"
#include <gtest/gtest.h>

using namespace markdown_parser;

TEST(SpineHandler, EmptyDocument) {
  InlineParser inline_parser;
  SpineHandler spine(inline_parser);
  spine.finalize();
  auto doc = spine.releaseDocument();
  ASSERT_NE(doc, nullptr);
  EXPECT_EQ(doc->type, NodeType::Document);
}
