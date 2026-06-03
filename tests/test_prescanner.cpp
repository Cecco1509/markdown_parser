#include <gtest/gtest.h>
#include "markdown_parser/PreScanner.hpp"

TEST(PreScanner, BlankLine) {
    PreScanner ps;
    auto sl = ps.scan("   \n");
    EXPECT_TRUE(sl.is_blank);
}

TEST(PreScanner, SimpleIndent) {
    PreScanner ps;
    auto sl = ps.scan("    hello\n");
    EXPECT_EQ(sl.virtual_indent, 4u);
    EXPECT_FALSE(sl.is_blank);
}
