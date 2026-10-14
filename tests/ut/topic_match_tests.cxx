///
/// @file topic_match_tests.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Unit tests for `topic_path` and the match result: MQTT section 4.7.3 well-formedness, the `/` split, and the
/// parameter spans a match captures.
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

// --- match error paths ---

TEST(topic_match_tests, match_str_rejects_malformed_topic) {
    const topic_pattern_path pattern = create_pattern("a/+");

    const auto empty = pattern.match_str("");
    ASSERT_FALSE(empty);
    const auto* empty_cause = std::get_if<invalid_topic_error>(&empty.error());
    ASSERT_NE(empty_cause, nullptr);
    EXPECT_TRUE(std::holds_alternative<empty_topic_path_error>(empty_cause->cause));

    const auto null_char = pattern.match_str(std::string("a/\0", 3));
    ASSERT_FALSE(null_char);
    const auto* null_cause = std::get_if<invalid_topic_error>(&null_char.error());
    ASSERT_NE(null_cause, nullptr);
    EXPECT_TRUE(std::holds_alternative<null_char_topic_path_error>(null_cause->cause));
}

TEST(topic_match_tests, rejects_too_few_topic_levels) {
    const auto matched = create_pattern("a/b").match_str("a");
    ASSERT_FALSE(matched);
    EXPECT_TRUE(std::holds_alternative<unexpected_end_of_topic_error>(matched.error()));
}

TEST(topic_match_tests, rejects_too_many_topic_levels) {
    const auto matched = create_pattern("a").match_str("a/b");
    ASSERT_FALSE(matched);
    EXPECT_TRUE(std::holds_alternative<unexpected_end_of_pattern_error>(matched.error()));
}

TEST(topic_match_tests, rejects_literal_mismatch) {
    const auto matched = create_pattern("a/b").match_str("a/c");
    ASSERT_FALSE(matched);
    const auto* mismatch = std::get_if<segment_mismatch_error>(&matched.error());
    ASSERT_NE(mismatch, nullptr);
    EXPECT_EQ(mismatch->expected, "b");
    EXPECT_EQ(mismatch->found, "c");
    EXPECT_EQ(mismatch->position, 1U);
}

// --- captured parameters ---

TEST(topic_match_tests, captures_positional_params) {
    const auto matched = create_pattern("sensors/+/data/+").match_str("sensors/kitchen/data/23.5");
    ASSERT_TRUE(matched);
    const auto result = std::move(matched).value();
    EXPECT_EQ(result->param_count(), 2U);
    EXPECT_EQ(result->param(0), "kitchen");
    EXPECT_EQ(result->param(1), "23.5");
    EXPECT_EQ(result->param(2), "");
    EXPECT_EQ(result->topic_name(), "sensors/kitchen/data/23.5");
}

TEST(topic_match_tests, captures_named_params) {
    const auto matched = create_pattern("sensors/{sensor_id}/data").match_str("sensors/kitchen/data");
    ASSERT_TRUE(matched);
    const auto result = std::move(matched).value();
    EXPECT_EQ(result->named_param("sensor_id"), std::optional<std::string_view> { "kitchen" });
    EXPECT_EQ(result->named_param("absent"), std::nullopt);
}

TEST(topic_match_tests, hash_capture_spans_remaining_levels) {
    const auto matched = create_pattern("logs/#").match_str("logs/auth/service/error");
    ASSERT_TRUE(matched);
    const auto result = std::move(matched).value();
    EXPECT_EQ(result->param_count(), 1U);
    EXPECT_EQ(result->param(0), "auth/service/error");
}

TEST(topic_match_tests, hash_matches_the_parent_level) {
    // `logs/#` matches `logs` itself; the capture is then empty.
    const auto matched = create_pattern("logs/#").match_str("logs");
    ASSERT_TRUE(matched);
    const auto result = std::move(matched).value();
    EXPECT_EQ(result->param_count(), 1U);
    EXPECT_EQ(result->param(0), "");
}

TEST(topic_match_tests, display) {
    const auto with_params = create_pattern("a/+").match_str("a/b");
    ASSERT_TRUE(with_params);
    EXPECT_EQ(with_params.value()->to_string(), "Match(a/b) with 1 params");

    const auto without = create_pattern("a/b").match_str("a/b");
    ASSERT_TRUE(without);
    EXPECT_EQ(without.value()->to_string(), "Match(a/b)");
    EXPECT_EQ(to_string(*without.value()), "Match(a/b)");
}

// --- error rendering ---

TEST(topic_match_tests, describes_errors) {
    const auto matched = create_pattern("a/b").match_str("a/c");
    ASSERT_FALSE(matched);
    // "Segment 'b' does not match 'c' at position 1"-shaped; assert on what the caller can act on.
    const std::string rendered = describe(matched.error());
    EXPECT_NE(rendered.find("b"), std::string::npos);
    EXPECT_NE(rendered.find("c"), std::string::npos);
}

} // namespace
} // namespace ba7lya::topic
