// SPDX-License-Identifier: MIT
//
// End to end: the parse → match → extract → build flow through the public API, with no router.
//
// These are the paths an application takes when it only needs the pattern half of the engine —
// reading a topic, unpacking its parameters, and composing a topic to publish on. They are kept
// out of the router suite so that they still run in a build with the router switched off.

#include <gtest/gtest.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "topic_engine.hxx"

namespace ba7lya::topic_engine::e2e {
namespace {

/// Parses a filter, throwing when the test feeds it something invalid.
[[nodiscard]]
topic_pattern_path pattern_of(const std::string& text) {
    auto parsed = topic_pattern_path::parse(text);
    if (!parsed) { throw std::runtime_error("pattern should be valid: " + text + ": " + describe(parsed.error())); }
    return std::move(parsed).value();
}

/// Matches `topic` against `filter`, throwing when the topic is not admitted.
[[nodiscard]]
std::shared_ptr<const topic_match> matched_ok(const std::string& filter, const std::string& topic) {
    const auto matched = pattern_of(filter).match_str(topic);
    if (!matched) { throw std::runtime_error("topic should match: " + topic + ": " + describe(matched.error())); }
    return matched.value();
}

// --- reading a topic ---

TEST(topic_api_tests, a_topic_is_parsed_once_and_read_level_by_level) {
    const auto parsed = topic_path::parse("sensors/kitchen/temp001/data");
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value()->text(), "sensors/kitchen/temp001/data");
    EXPECT_EQ(parsed.value()->segments().size(), 4U);
    EXPECT_EQ(parsed.value()->segments().front(), "sensors");
    EXPECT_EQ(parsed.value()->segments().back(), "data");
}

TEST(topic_api_tests, positional_parameters_follow_filter_order) {
    const auto matched = matched_ok("sensors/+/+/data", "sensors/kitchen/temp001/data");

    EXPECT_EQ(matched->param_count(), 2U);
    EXPECT_EQ(matched->param(0), "kitchen");
    EXPECT_EQ(matched->param(1), "temp001");
    EXPECT_TRUE(matched->param(2).empty()) << "out of range reads as empty rather than throwing";
}

TEST(topic_api_tests, named_parameters_are_reachable_by_name) {
    const auto matched = matched_ok("sensors/{location}/{device}/data", "sensors/kitchen/temp001/data");

    EXPECT_EQ(matched->named_param("location").value(), "kitchen");
    EXPECT_EQ(matched->named_param("device").value(), "temp001");
    EXPECT_FALSE(matched->named_param("missing").has_value());
}

TEST(topic_api_tests, a_multi_level_parameter_captures_the_joined_remainder) {
    const auto matched = matched_ok("logs/{service}/{details:#}", "logs/api/v1/users/create");

    EXPECT_EQ(matched->named_param("service").value(), "api");
    EXPECT_EQ(matched->named_param("details").value(), "v1/users/create");
}

TEST(topic_api_tests, a_hash_capture_is_empty_when_nothing_follows) {
    const auto matched = matched_ok("logs/{service}/{details:#}", "logs/api");

    EXPECT_EQ(matched->named_param("service").value(), "api");
    EXPECT_EQ(matched->named_param("details").value(), "");
}

TEST(topic_api_tests, one_topic_is_read_against_several_filters) {
    // The shared topic path is what makes this cheap: it is parsed once and reused per filter.
    const auto topic = topic_path::parse("home/kitchen/temperature");
    ASSERT_TRUE(topic);

    const auto anonymous = pattern_of("home/+/temperature").match(topic.value());
    ASSERT_TRUE(anonymous);
    EXPECT_FALSE(anonymous.value()->named_param("room").has_value());

    const auto named = pattern_of("home/{room}/temperature").match(topic.value());
    ASSERT_TRUE(named);
    EXPECT_EQ(named.value()->named_param("room").value(), "kitchen");
}

// --- composing a topic to publish on ---

TEST(topic_api_tests, a_publish_topic_is_built_from_the_same_template) {
    const topic_pattern_path filter = pattern_of("devices/{type}/{id}/command");
    const auto built = filter.format_topic({ "sensor", "42" });
    ASSERT_TRUE(built);
    EXPECT_EQ(built.value(), "devices/sensor/42/command");
}

TEST(topic_api_tests, a_built_topic_round_trips_back_through_the_filter) {
    const topic_pattern_path filter = pattern_of("devices/{type}/{id}/command");
    const auto built = filter.format_topic({ "sensor", "42" });
    ASSERT_TRUE(built);

    const auto matched = matched_ok(filter.topic_pattern(), built.value());
    EXPECT_EQ(matched->named_param("type").value(), "sensor");
    EXPECT_EQ(matched->named_param("id").value(), "42");
}

TEST(topic_api_tests, a_value_count_mismatch_is_refused) {
    const topic_pattern_path filter = pattern_of("devices/{type}/{id}/command");
    const auto built = filter.format_topic({ "sensor" });

    ASSERT_FALSE(built);
    const auto* mismatch = std::get_if<parameter_count_mismatch_error>(&built.error());
    ASSERT_NE(mismatch, nullptr);
    EXPECT_EQ(mismatch->expected, 2U);
    EXPECT_EQ(mismatch->provided, 1U);
}

// --- validation happens before anything else does ---

TEST(topic_api_tests, a_malformed_template_is_rejected_up_front) {
    EXPECT_FALSE(topic_pattern_path::parse(""));
    EXPECT_FALSE(topic_pattern_path::parse("sensors/#/data"));
    EXPECT_FALSE(topic_pattern_path::parse("sensors/sen+ors/data"));
    EXPECT_FALSE(topic_pattern_path::parse(std::string("sensors/\0/data", 14)));

    EXPECT_TRUE(std::holds_alternative<empty_pattern_error>(topic_pattern_path::parse("").error()));
    EXPECT_TRUE(
        std::holds_alternative<null_char_pattern_error>(topic_pattern_path::parse(std::string("a\0b", 3)).error())
    );
    EXPECT_TRUE(std::holds_alternative<hash_position_error>(topic_pattern_path::parse("sensors/#/data").error()));
    EXPECT_TRUE(
        std::holds_alternative<wildcard_usage_error>(topic_pattern_path::parse("sensors/sen+ors/data").error())
    );
}

TEST(topic_api_tests, a_hash_only_filter_is_the_root_template) {
    // `{rest:#}` bound to nothing collapses to a bare `#`, which must not keep a phantom level.
    const auto bound = pattern_of("{rest:#}").bind_parameter("rest", "");
    ASSERT_TRUE(bound);
    EXPECT_EQ(bound.value().mqtt_pattern(), "#");
    EXPECT_TRUE(bound.value().contains_hash());
    EXPECT_EQ(bound.value().len(), 1U);
}

TEST(topic_api_tests, an_anonymous_wildcard_never_sees_a_dollar_topic) {
    // MQTT §4.7.2, reached through the public match call rather than the trie.
    const auto excluded = pattern_of("#").match_str("$SYS/broker/uptime");
    ASSERT_FALSE(excluded);
    EXPECT_TRUE(std::holds_alternative<dollar_topic_exclusion_error>(excluded.error()));

    // A literal `$` level is ordinary text, so a filter that spells it out is admitted.
    const auto literal = pattern_of("$SYS/#").match_str("$SYS/broker/uptime");
    ASSERT_TRUE(literal);
    EXPECT_EQ(literal.value()->topic_name(), "$SYS/broker/uptime");
}

TEST(topic_api_tests, binding_a_parameter_narrows_the_wire_pattern) {
    const auto filtered = pattern_of("sensors/{location}/{sensor_type}/{sensor_id}/data")
                              .bind_parameter("location", "kitchen")
                              .value()
                              .bind_parameter("sensor_type", "temperature")
                              .value();

    EXPECT_EQ(filtered.mqtt_pattern(), "sensors/kitchen/temperature/+/data");
    EXPECT_EQ(filtered.bound_value("location").value(), "kitchen");
    EXPECT_FALSE(filtered.bound_value("sensor_id").has_value());
    EXPECT_EQ(
        matched_ok(filtered.topic_pattern(), "sensors/kitchen/temperature/temp001/data")->topic_name(),
        "sensors/kitchen/temperature/temp001/data"
    );
}

TEST(topic_api_tests, a_bound_parameter_is_matched_as_a_literal) {
    const auto filtered = pattern_of("sensors/{location}/data").bind_parameter("location", "kitchen");

    ASSERT_TRUE(filtered);
    EXPECT_TRUE(filtered.value().match_str("sensors/kitchen/data"));
    EXPECT_FALSE(filtered.value().match_str("sensors/hall/data"));
}

TEST(topic_api_tests, compatibility_checks_the_structure_two_templates_share) {
    const topic_pattern_path template_path = pattern_of("a/{x}/c");
    const topic_pattern_path same_shape = pattern_of("a/{y}/c");
    const topic_pattern_path other_shape = pattern_of("a/{x}/c/d");

    EXPECT_TRUE(template_path.check_compatibility(same_shape));
    EXPECT_FALSE(template_path.check_compatibility(other_shape));
}

// --- the caching front end ---

TEST(topic_api_tests, a_cached_filter_answers_the_same_as_an_uncached_one) {
    const auto uncached = topic_pattern_path::parse("sensors/{room}/temperature", cache_strategy::no_cache());
    const auto cached = topic_pattern_path::parse("sensors/{room}/temperature", cache_strategy::lru(64));
    ASSERT_TRUE(uncached);
    ASSERT_TRUE(cached);
    EXPECT_FALSE(uncached.value().caching().is_lru());
#ifdef BA7LYA_TOPIC_ENGINE_NO_LRU_CACHE
    // With the cache compiled out every strategy degrades to `no_cache()`, so the two agree.
    EXPECT_FALSE(cached.value().caching().is_lru());
#else
    EXPECT_TRUE(cached.value().caching().is_lru());
#endif

    for (const char* topic : { "sensors/kitchen/temperature", "sensors/hall/temperature" }) {
        const auto from_cache = cached.value().match_str(topic);
        const auto direct = uncached.value().match_str(topic);
        ASSERT_TRUE(from_cache);
        ASSERT_TRUE(direct);
        EXPECT_EQ(from_cache.value()->named_param("room"), direct.value()->named_param("room"));
    }
}

TEST(topic_api_tests, re_matching_a_topic_returns_the_same_capture) {
    const auto filter = topic_pattern_path::parse("sensors/{room}/temperature", cache_strategy::lru(8));
    ASSERT_TRUE(filter);

    const auto first = filter.value().match_str("sensors/kitchen/temperature");
    const auto second = filter.value().match_str("sensors/kitchen/temperature");
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);

    // A cache hit must be indistinguishable from a fresh match, captures included.
    EXPECT_EQ(second.value()->topic_name(), first.value()->topic_name());
    EXPECT_EQ(second.value()->named_param("room"), first.value()->named_param("room"));
}

TEST(topic_api_tests, a_copy_of_a_filter_gets_its_own_cache) {
    const auto original = topic_pattern_path::parse("sensors/{room}/temperature", cache_strategy::lru(4));
    ASSERT_TRUE(original);

    topic_pattern_path copy = original.value();
    EXPECT_EQ(copy.caching(), original.value().caching());
    EXPECT_EQ(copy.topic_pattern(), original.value().topic_pattern());

    // The cache is per-instance, but the bindings travel with the copy.
    const auto bound = original.value().bind_parameter("room", "kitchen");
    ASSERT_TRUE(bound);
    EXPECT_FALSE(original.value().bound_value("room").has_value()) << "binding returns a new filter";
    EXPECT_EQ(bound.value().bound_value("room").value(), "kitchen");
}

} // namespace
} // namespace ba7lya::topic_engine::e2e
