#include "markdown_parser/parser/ScannedLine.hpp"
#include <gtest/gtest.h>

using namespace markdown_parser;

TEST(PreScanner, BlankLine) {
  auto sl = ScannedLine::from("   \n");
  EXPECT_TRUE(sl.is_blank());
}

TEST(PreScanner, SimpleIndent) {
  auto sl = ScannedLine::from("    hello\n");
  EXPECT_EQ(sl.indent(), 4u);
  EXPECT_FALSE(sl.is_blank());
}
