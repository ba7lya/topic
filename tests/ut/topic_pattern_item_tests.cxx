// SPDX-License-Identifier: MIT
//
// `topic_pattern_item`: parsing one filter level into a literal, `+` or `#`, and the two spellings
// the rest of the library renders from it.

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <variant>

#include "topic_engine.hxx"

namespace ba7lya::topic_engine {
namespace {

[[nodiscard]]
std::string describe_error(const topic_pattern_error& error) {
    return describe(error);
}

TEST(topic_pattern_item_tests, literal_string_item) {
    const auto parsed = topic_pattern_item::parse("sensors");
    ASSERT_TRUE(parsed);
    const topic_pattern_item& item = parsed.value();

    EXPECT_EQ(item, topic_pattern_item::make_str("sensors"));
    EXPECT_EQ(item.as_str(), "sensors");
    EXPECT_EQ(item.as_wildcard(), "sensors");
    EXPECT_EQ(item.param_name(), std::nullopt);
    EXPECT_FALSE(item.is_wildcard());
}

TEST(topic_pattern_item_tests, anonymous_plus_wildcard) {
    const auto parsed = topic_pattern_item::parse("+");
    ASSERT_TRUE(parsed);
    const topic_pattern_item& item = parsed.value();

    EXPECT_EQ(item, topic_pattern_item::make_plus());
    EXPECT_EQ(item.as_str(), "+");
    EXPECT_EQ(item.as_wildcard(), "+");
    EXPECT_EQ(item.param_name(), std::nullopt);
    EXPECT_TRUE(item.is_wildcard());
    EXPECT_TRUE(item.is_plus());
    EXPECT_FALSE(item.is_hash());
}

TEST(topic_pattern_item_tests, anonymous_hash_wildcard) {
    const auto parsed = topic_pattern_item::parse("#");
    ASSERT_TRUE(parsed);
    const topic_pattern_item& item = parsed.value();

    EXPECT_EQ(item, topic_pattern_item::make_hash());
    EXPECT_EQ(item.as_str(), "#");
    EXPECT_EQ(item.as_wildcard(), "#");
    EXPECT_EQ(item.param_name(), std::nullopt);
    EXPECT_TRUE(item.is_wildcard());
    EXPECT_TRUE(item.is_hash());
    EXPECT_FALSE(item.is_plus());
}

TEST(topic_pattern_item_tests, named_plus_wildcard) {
    const auto parsed = topic_pattern_item::parse("{sensor_id}");
    ASSERT_TRUE(parsed);
    const topic_pattern_item& item = parsed.value();

    ASSERT_TRUE(item.is_plus());
    EXPECT_EQ(item.param_name(), std::optional<std::string> { "sensor_id" });
    EXPECT_EQ(item.as_str(), "+");
    EXPECT_EQ(item.as_wildcard(), "{sensor_id}");
    EXPECT_TRUE(item.is_wildcard());
}

TEST(topic_pattern_item_tests, named_hash_wildcard) {
    const auto parsed = topic_pattern_item::parse("{details:#}");
    ASSERT_TRUE(parsed);
    const topic_pattern_item& item = parsed.value();

    ASSERT_TRUE(item.is_hash());
    EXPECT_EQ(item.param_name(), std::optional<std::string> { "details" });
    EXPECT_EQ(item.as_str(), "#");
    EXPECT_EQ(item.as_wildcard(), "{details:#}");
    EXPECT_TRUE(item.is_wildcard());
}

// --- levels that look bracket-ish but are literals ---

TEST(topic_pattern_item_tests, malformed_brackets_are_literals) {
    // A brace without its partner is not a parameter, so the level stays literal byte for byte.
    for (const std::string_view text : { "{param", "param}", "{{param}}" }) {
        const auto parsed = topic_pattern_item::parse(text);
        ASSERT_TRUE(parsed) << text;
        EXPECT_FALSE(parsed.value().is_wildcard()) << text;
        EXPECT_EQ(parsed.value().as_str(), text) << text;
    }
}

TEST(topic_pattern_item_tests, empty_string_segment) {
    const auto parsed = topic_pattern_item::parse("");
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value(), topic_pattern_item::make_str(""));
    EXPECT_EQ(parsed.value().as_str(), "");
    EXPECT_FALSE(parsed.value().is_wildcard());
}

TEST(topic_pattern_item_tests, unicode_and_special_characters) {
    const auto unicode = topic_pattern_item::parse("сенсори");
    ASSERT_TRUE(unicode);
    EXPECT_EQ(unicode.value().as_str(), "сенсори");
    EXPECT_FALSE(unicode.value().is_wildcard());

    const auto special = topic_pattern_item::parse("device-123@home.local");
    ASSERT_TRUE(special);
    EXPECT_EQ(special.value().as_str(), "device-123@home.local");
    EXPECT_FALSE(special.value().is_wildcard());
}

// --- rejected levels ---

TEST(topic_pattern_item_tests, empty_named_wildcards_are_usage_errors) {
    for (const std::string_view text : { "{}", "{:#}" }) {
        const auto parsed = topic_pattern_item::parse(text);
        ASSERT_FALSE(parsed) << text;
        const auto* usage = std::get_if<wildcard_usage_error>(&parsed.error());
        ASSERT_NE(usage, nullptr) << text;
        EXPECT_EQ(usage->usage, text);
    }
}

TEST(topic_pattern_item_tests, stray_wildcards_are_usage_errors) {
    // A `+` or `#` glued to text is neither a wildcard level nor a literal one.
    for (const std::string_view text : { "text+more", "text#more", "++", "##", "a+b", "a#b" }) {
        const auto parsed = topic_pattern_item::parse(text);
        ASSERT_FALSE(parsed) << text;
        const auto* usage = std::get_if<wildcard_usage_error>(&parsed.error());
        ASSERT_NE(usage, nullptr) << text;
        EXPECT_EQ(usage->usage, text);
    }
}

// --- value semantics ---

TEST(topic_pattern_item_tests, equality_ignores_parameter_names) {
    const auto named = topic_pattern_item::parse("{id}");
    const auto anonymous = topic_pattern_item::parse("+");
    ASSERT_TRUE(named);
    ASSERT_TRUE(anonymous);

    // `{id}` and `+` are the same level for compatibility purposes, but differ in their naming.
    EXPECT_EQ(named.value(), anonymous.value());
    EXPECT_NE(named.value().param_name(), anonymous.value().param_name());
    EXPECT_NE(named.value(), topic_pattern_item::make_hash());
    EXPECT_NE(topic_pattern_item::make_str("a"), topic_pattern_item::make_str("b"));
}

TEST(topic_pattern_item_tests, clone_and_equality) {
    const topic_pattern_item original = topic_pattern_item::make_named_plus("test");
    const topic_pattern_item copy = original;

    EXPECT_EQ(original, copy);
    EXPECT_EQ(original.param_name(), copy.param_name());
}

TEST(topic_pattern_item_tests, named_wildcards_accept_underscores_and_digits) {
    for (const std::string& name : { std::string("sensor_id_123"), std::string("param123") }) {
        const auto parsed = topic_pattern_item::parse("{" + name + "}");
        ASSERT_TRUE(parsed) << name;
        EXPECT_EQ(parsed.value().param_name(), std::optional<std::string> { name });
        EXPECT_EQ(parsed.value().as_wildcard(), "{" + name + "}");
    }
}

TEST(topic_pattern_item_tests, describes_errors) {
    const auto parsed = topic_pattern_item::parse("a+b");
    ASSERT_FALSE(parsed);
    EXPECT_NE(describe_error(parsed.error()).find("a+b"), std::string::npos);
}

} // namespace
} // namespace ba7lya::topic_engine
