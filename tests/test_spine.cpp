#include <gtest/gtest.h>
#include "markdown_parser/SpineHandler.hpp"
#include "markdown_parser/InlineParser.hpp"

TEST(SpineHandler, EmptyDocument) {
    InlineParser inline_parser;
    SpineHandler spine(inline_parser);
    spine.finalize();
    auto doc = spine.releaseDocument();
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->type, NodeType::Document);
}
