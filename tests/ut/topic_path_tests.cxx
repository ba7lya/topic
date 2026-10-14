///
/// @file topic_path_tests.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Unit tests for `topic_path`: MQTT section 4.7.3 well-formedness and the `/` split into levels.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "test_support.hxx"
#include "topic.hxx"

namespace ba7lya::topic {
namespace {

using test::create_pattern;

// --- topic_path well-formedness (MQTT §4.7.3) ---

TEST(topic_path_tests, rejects_empty_topic) {
    const auto parsed = topic_path::parse("");
    ASSERT_FALSE(parsed);
    EXPECT_TRUE(std::holds_alternative<empty_topic_path_error>(parsed.error()));
}

TEST(topic_path_tests, rejects_null_char) {
    const auto parsed = topic_path::parse(std::string("a/\0/b", 5));
    ASSERT_FALSE(parsed);
    EXPECT_TRUE(std::holds_alternative<null_char_topic_path_error>(parsed.error()));
}

TEST(topic_path_tests, accepts_utf8_spaces_and_control_chars) {
    // U+0001..U+001F are discouraged by the spec but legal, and only U+0000 is forbidden.
    EXPECT_TRUE(topic_path::parse("наприклад/日本語/😀"));
    EXPECT_TRUE(topic_path::parse(std::string("a/\u0001/b")));
    EXPECT_TRUE(topic_path::parse("   "));
}

TEST(topic_path_tests, splits_into_segments) {
    const auto parsed = topic_path::parse("a/b/c");
    ASSERT_TRUE(parsed);
    const auto topic = std::move(parsed).value();
    EXPECT_EQ(topic->segment_count(), 3U);
    EXPECT_EQ(topic->text(), "a/b/c");
    ASSERT_EQ(topic->segments().size(), 3U);
    EXPECT_EQ(topic->segments()[0], "a");
    EXPECT_EQ(topic->segments()[1], "b");
    EXPECT_EQ(topic->segments()[2], "c");
}

TEST(topic_path_tests, keeps_empty_levels) {
    const auto parsed = topic_path::parse("a//b");
    ASSERT_TRUE(parsed);
    const auto topic = std::move(parsed).value();
    ASSERT_EQ(topic->segments().size(), 3U);
    EXPECT_EQ(topic->segments()[1], "");
}

} // namespace
} // namespace ba7lya::topic
