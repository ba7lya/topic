// SPDX-License-Identifier: MIT
//
// `topic_pattern_path`: parsing a filter into levels, binding its named parameters, and the two
// spellings a filter renders as.

#include <cstddef>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "test_support.hxx"
#include "topic_engine.hxx"

namespace ba7lya::topic_engine {
namespace {

using test::create_pattern;
using test::matches;
using test::with_parameters;

/// A parsed filter is a precondition of every check below, so unwrap it once.
[[nodiscard]]
topic_pattern_path parse_ok(const std::string& text) {
    auto parsed = topic_pattern_path::parse(text);
    if (!parsed) { throw std::runtime_error("pattern should be valid: " + text + ": " + describe(parsed.error())); }
    return std::move(parsed).value();
}

/// `usage` out of a rejected filter, or the empty string when the error is something else.
[[nodiscard]]
std::string usage_of(const topic_pattern_error& error) {
    const auto* usage = std::get_if<wildcard_usage_error>(&error);
    return usage == nullptr ? std::string {} : usage->usage;
}

// --- parsing ---

TEST(topic_pattern_path_parsing_tests, simple_string_pattern) {
    const topic_pattern_path pattern = parse_ok("simple/path");
    ASSERT_EQ(pattern.segments().size(), 2U);
    EXPECT_EQ(pattern.segments()[0], topic_pattern_item::make_str("simple"));
    EXPECT_EQ(pattern.segments()[1], topic_pattern_item::make_str("path"));
    EXPECT_FALSE(pattern.contains_hash());
    EXPECT_EQ(pattern.len(), 2U);
}

TEST(topic_pattern_path_parsing_tests, pattern_with_plus) {
    const topic_pattern_path pattern = parse_ok("devices/+/status");
    ASSERT_EQ(pattern.segments().size(), 3U);
    EXPECT_EQ(pattern.segments()[1], topic_pattern_item::make_plus());
    EXPECT_FALSE(pattern.contains_hash());
}

TEST(topic_pattern_path_parsing_tests, pattern_with_hash) {
    const topic_pattern_path pattern = parse_ok("sensors/#");
    ASSERT_EQ(pattern.segments().size(), 2U);
    EXPECT_EQ(pattern.segments()[1], topic_pattern_item::make_hash());
    EXPECT_TRUE(pattern.contains_hash());
}

TEST(topic_pattern_path_parsing_tests, pattern_with_both_wildcards) {
    const topic_pattern_path pattern = parse_ok("home/+/device/#");
    ASSERT_EQ(pattern.segments().size(), 4U);
    EXPECT_EQ(pattern.segments()[1], topic_pattern_item::make_plus());
    EXPECT_EQ(pattern.segments()[3], topic_pattern_item::make_hash());
}

TEST(topic_pattern_path_parsing_tests, empty_pattern_is_rejected) {
    const auto parsed = topic_pattern_path::parse("");
    ASSERT_FALSE(parsed);
    EXPECT_TRUE(std::holds_alternative<empty_pattern_error>(parsed.error()));
}

TEST(topic_pattern_path_parsing_tests, null_char_is_rejected) {
    const auto parsed = topic_pattern_path::parse(std::string("a/\0/b", 5));
    ASSERT_FALSE(parsed);
    EXPECT_TRUE(std::holds_alternative<null_char_pattern_error>(parsed.error()));
}

TEST(topic_pattern_path_parsing_tests, only_wildcards) {
    const topic_pattern_path plus = parse_ok("+");
    ASSERT_EQ(plus.segments().size(), 1U);
    EXPECT_EQ(plus.segments()[0], topic_pattern_item::make_plus());

    const topic_pattern_path hash = parse_ok("#");
    ASSERT_EQ(hash.segments().size(), 1U);
    EXPECT_EQ(hash.segments()[0], topic_pattern_item::make_hash());
}

TEST(topic_pattern_path_parsing_tests, keeps_empty_levels) {
    // A `//` is a genuine empty level, not a separator to be collapsed away.
    const topic_pattern_path consecutive = parse_ok("topic//subtopic");
    ASSERT_EQ(consecutive.segments().size(), 3U);
    EXPECT_EQ(consecutive.segments()[1], topic_pattern_item::make_str(""));

    const topic_pattern_path leading = parse_ok("/start");
    ASSERT_EQ(leading.segments().size(), 2U);
    EXPECT_EQ(leading.segments()[0], topic_pattern_item::make_str(""));

    const topic_pattern_path trailing = parse_ok("end/");
    ASSERT_EQ(trailing.segments().size(), 2U);
    EXPECT_EQ(trailing.segments()[1], topic_pattern_item::make_str(""));

    EXPECT_FALSE(parse_ok("/").is_empty());
    EXPECT_EQ(parse_ok("/").len(), 2U);
}

TEST(topic_pattern_path_parsing_tests, hash_must_be_the_last_level) {
    const auto parsed = topic_pattern_path::parse("invalid/#/pattern");
    ASSERT_FALSE(parsed);
    const auto* misplaced = std::get_if<hash_position_error>(&parsed.error());
    ASSERT_NE(misplaced, nullptr);
    EXPECT_EQ(misplaced->pattern, "invalid/#/pattern");
}

TEST(topic_pattern_path_parsing_tests, long_and_unicode_patterns) {
    const topic_pattern_path long_pattern = parse_ok(
        "segment1/segment2/segment3/segment4/segment5/"
        "segment6/segment7/segment8/segment9/segment10"
    );
    EXPECT_EQ(long_pattern.len(), 10U);

    const topic_pattern_path unicode = parse_ok("пристрої/+/статус");
    ASSERT_EQ(unicode.segments().size(), 3U);
    EXPECT_EQ(unicode.segments()[0], topic_pattern_item::make_str("пристрої"));
    EXPECT_EQ(unicode.segments()[1], topic_pattern_item::make_plus());
    EXPECT_EQ(unicode.segments()[2], topic_pattern_item::make_str("статус"));

    const topic_pattern_path special = parse_ok("device-123/status@home");
    EXPECT_EQ(special.segments()[0], topic_pattern_item::make_str("device-123"));
    EXPECT_EQ(special.segments()[1], topic_pattern_item::make_str("status@home"));
}

TEST(topic_pattern_path_parsing_tests, utf8_spaces_and_control_chars_are_accepted) {
    // §4.7.3 forbids exactly two things: emptiness and U+0000.
    EXPECT_TRUE(topic_pattern_path::parse("наприклад/日本語/😀"));
    EXPECT_TRUE(topic_pattern_path::parse(std::string("a/\u0001/b")));
    EXPECT_TRUE(topic_pattern_path::parse("   "));
}

TEST(topic_pattern_path_parsing_tests, stray_wildcards_are_usage_errors) {
    for (const std::string& text : { std::string("topic/++/subtopic"), std::string("topic/##"),
                                     std::string("topic/a+b/subtopic"), std::string("topic/a#b") }) {
        const auto parsed = topic_pattern_path::parse(text);
        ASSERT_FALSE(parsed) << text;
        ASSERT_TRUE(std::holds_alternative<wildcard_usage_error>(parsed.error())) << text;
    }
}

// --- rendering ---

TEST(topic_pattern_path_tests, renders_its_template) {
    EXPECT_EQ(parse_ok("simple/path").to_string(), "simple/path");
    EXPECT_EQ(parse_ok("devices/+/status").to_string(), "devices/+/status");
    EXPECT_EQ(parse_ok("sensors/#").to_string(), "sensors/#");
    EXPECT_EQ(parse_ok("/").to_string(), "/");
    EXPECT_EQ(parse_ok("device/").to_string(), "device/");
    EXPECT_EQ(parse_ok("topic//subtopic").to_string(), "topic//subtopic");
    EXPECT_EQ(to_string(parse_ok("a/{b}/c")), "a/{b}/c");
}

TEST(topic_pattern_path_tests, mqtt_pattern_anonymises_named_wildcards) {
    EXPECT_EQ(parse_ok("sensors/{sensor_id}/data").mqtt_pattern(), "sensors/+/data");
    EXPECT_EQ(parse_ok("logs/{service}/#").mqtt_pattern(), "logs/+/#");
    EXPECT_EQ(parse_ok("events/{category}/{details:#}").mqtt_pattern(), "events/+/#");
    EXPECT_EQ(parse_ok("plain/topic").mqtt_pattern(), "plain/topic");
}

// --- binding parameters ---

TEST(topic_pattern_path_tests, binds_a_single_parameter) {
    const auto bound = with_parameters(
        create_pattern("sensors/{sensor_id}/data"), {
                                                        { "sensor_id", "123" }
    }
    );
    ASSERT_TRUE(bound);
    // The template is the caller's spelling and never changes; the wire form does.
    EXPECT_EQ(bound.value().topic_pattern(), "sensors/{sensor_id}/data");
    EXPECT_EQ(bound.value().mqtt_pattern(), "sensors/123/data");
}

TEST(topic_pattern_path_tests, binds_several_parameters) {
    const auto bound = with_parameters(
        create_pattern("buildings/{building}/floors/{floor}/rooms/{room}"), {
                                                                                { "building", "A"       },
                                                                                { "floor",    "2"       },
                                                                                { "room",     "kitchen" }
    }
    );
    ASSERT_TRUE(bound);
    EXPECT_EQ(bound.value().mqtt_pattern(), "buildings/A/floors/2/rooms/kitchen");
}

TEST(topic_pattern_path_tests, leaves_unbound_parameters_as_wildcards) {
    const auto bound = with_parameters(
        create_pattern("sensors/{location}/{sensor_type}/{sensor_id}/data"), {
                                                                                 { "sensor_type", "temperature" }
    }
    );
    ASSERT_TRUE(bound);
    EXPECT_EQ(bound.value().topic_pattern(), "sensors/{location}/{sensor_type}/{sensor_id}/data");
    EXPECT_EQ(bound.value().mqtt_pattern(), "sensors/+/temperature/+/data");
}

TEST(topic_pattern_path_tests, anonymous_wildcards_are_not_substituted) {
    const auto bound = with_parameters(
        create_pattern("sensors/+/data/{measurement}"), {
                                                            { "measurement", "temperature" }
    }
    );
    ASSERT_TRUE(bound);
    EXPECT_EQ(bound.value().mqtt_pattern(), "sensors/+/data/temperature");
}

TEST(topic_pattern_path_tests, mixed_named_and_anonymous_wildcards) {
    const auto bound = with_parameters(
        create_pattern("devices/{device_type}/+/status/{status_type}"), {
                                                                            { "device_type", "sensor" },
                                                                            { "status_type", "online" }
    }
    );
    ASSERT_TRUE(bound);
    EXPECT_EQ(bound.value().topic_pattern(), "devices/{device_type}/+/status/{status_type}");
    EXPECT_EQ(bound.value().mqtt_pattern(), "devices/sensor/+/status/online");
}

TEST(topic_pattern_path_tests, hash_wildcards_survive_binding) {
    const auto plain_hash = with_parameters(
        create_pattern("logs/{service}/#"), {
                                                { "service", "auth" }
    }
    );
    ASSERT_TRUE(plain_hash);
    EXPECT_EQ(plain_hash.value().mqtt_pattern(), "logs/auth/#");

    // A named `#` keeps its template spelling but renders as a bare `#` on the wire.
    const auto named_hash = with_parameters(
        create_pattern("events/{category}/{details:#}"), {
                                                             { "category", "alerts" }
    }
    );
    ASSERT_TRUE(named_hash);
    EXPECT_EQ(named_hash.value().topic_pattern(), "events/{category}/{details:#}");
    EXPECT_EQ(named_hash.value().mqtt_pattern(), "events/alerts/#");
}

TEST(topic_pattern_path_tests, binding_is_order_independent) {
    const topic_pattern_path pattern = create_pattern("a/{first}/b/{second}/c");

    const auto forwards = with_parameters(
        pattern, {
                     { "first",  "1" },
                     { "second", "2" }
    }
    );
    const auto backwards = with_parameters(
        pattern, {
                     { "second", "2" },
                     { "first",  "1" }
    }
    );
    ASSERT_TRUE(forwards);
    ASSERT_TRUE(backwards);
    EXPECT_EQ(forwards.value().mqtt_pattern(), "a/1/b/2/c");
    EXPECT_EQ(backwards.value().mqtt_pattern(), forwards.value().mqtt_pattern());
}

TEST(topic_pattern_path_tests, rebinding_replaces_the_value) {
    const auto once = with_parameters(
        create_pattern("sensors/{id}/data"), {
                                                 { "id", "first" }
    }
    );
    ASSERT_TRUE(once);
    const auto twice = with_parameters(
        once.value(), {
                          { "id", "second" }
    }
    );
    ASSERT_TRUE(twice);
    EXPECT_EQ(twice.value().mqtt_pattern(), "sensors/second/data");
    EXPECT_EQ(twice.value().parameter_bindings()->size(), 1U);
}

TEST(topic_pattern_path_tests, binding_no_parameters_returns_the_same_filter) {
    const topic_pattern_path pattern = create_pattern("sensors/{sensor_id}/data");
    const auto unchanged = with_parameters(pattern, {});
    ASSERT_TRUE(unchanged);
    EXPECT_EQ(unchanged.value().topic_pattern(), pattern.topic_pattern());
    EXPECT_EQ(unchanged.value().mqtt_pattern(), pattern.mqtt_pattern());
    EXPECT_EQ(unchanged.value().parameter_bindings(), std::nullopt);
}

TEST(topic_pattern_path_tests, unknown_parameter_is_a_usage_error) {
    const auto bound = with_parameters(
        create_pattern("sensors/{sensor_id}/data"), {
                                                        { "nonexistent", "value" }
    }
    );
    ASSERT_FALSE(bound);
    EXPECT_NE(usage_of(bound.error()).find("nonexistent"), std::string::npos);
}

TEST(topic_pattern_path_tests, a_wildcard_free_pattern_binds_nothing) {
    const auto bound = with_parameters(
        create_pattern("static/topic/path"), {
                                                 { "nonexistent", "value" }
    }
    );
    ASSERT_FALSE(bound);
    ASSERT_TRUE(std::holds_alternative<wildcard_usage_error>(bound.error()));
}

TEST(topic_pattern_path_tests, mixed_valid_and_invalid_parameters_fail_on_the_invalid_one) {
    const auto bound = with_parameters(
        create_pattern("sensors/{sensor_id}/data/{type}"), {
                                                               { "sensor_id",     "123"   },
                                                               { "invalid_param", "value" }
    }
    );
    ASSERT_FALSE(bound);
    EXPECT_NE(usage_of(bound.error()).find("invalid_param"), std::string::npos);
}

TEST(topic_pattern_path_tests, a_single_level_parameter_pattern) {
    const auto bound = with_parameters(
        create_pattern("{param}"), {
                                       { "param", "value" }
    }
    );
    ASSERT_TRUE(bound);
    EXPECT_EQ(bound.value().topic_pattern(), "{param}");
    EXPECT_EQ(bound.value().mqtt_pattern(), "value");
}

TEST(topic_pattern_path_tests, values_may_be_empty_or_awkward) {
    const auto empty = with_parameters(
        create_pattern("prefix/{param}/suffix"), {
                                                     { "param", "" }
    }
    );
    ASSERT_TRUE(empty);
    EXPECT_EQ(empty.value().mqtt_pattern(), "prefix//suffix");

    const auto special = with_parameters(
        create_pattern("devices/{device_id}/data"), {
                                                        { "device_id", "temp-sensor@home.local" }
    }
    );
    ASSERT_TRUE(special);
    EXPECT_EQ(special.value().mqtt_pattern(), "devices/temp-sensor@home.local/data");

    const auto unicode = with_parameters(
        create_pattern("пристрої/{тип}/статус"), {
                                                     { "тип", "сенсор" }
    }
    );
    ASSERT_TRUE(unicode);
    EXPECT_EQ(unicode.value().mqtt_pattern(), "пристрої/сенсор/статус");
}

TEST(topic_pattern_path_tests, bindings_are_readable_back) {
    const auto bound = with_parameters(
        create_pattern("a/{first}/b/{second}"), {
                                                    { "first",  "1" },
                                                    { "second", "2" }
    }
    );
    ASSERT_TRUE(bound);

    const auto bindings = bound.value().parameter_bindings();
    ASSERT_TRUE(bindings.has_value());
    ASSERT_EQ(bindings->size(), 2U);
    EXPECT_EQ((*bindings)[0], std::make_pair(std::string("first"), std::string("1")));
    EXPECT_EQ((*bindings)[1], std::make_pair(std::string("second"), std::string("2")));
    EXPECT_EQ(bound.value().bound_value("first"), std::optional<std::string> { "1" });
    EXPECT_EQ(bound.value().bound_value("second"), std::optional<std::string> { "2" });
    EXPECT_EQ(bound.value().bound_value("absent"), std::nullopt);
}

TEST(topic_pattern_path_tests, resolved_segments_carry_the_bound_values) {
    const auto bound = with_parameters(
        create_pattern("sensors/{id}/+"), {
                                              { "id", "kitchen" }
    }
    );
    ASSERT_TRUE(bound);

    const std::vector<topic_pattern_item> resolved = bound.value().resolve_bound_segments();
    ASSERT_EQ(resolved.size(), 3U);
    EXPECT_EQ(resolved[0], topic_pattern_item::make_str("sensors"));
    EXPECT_EQ(resolved[1], topic_pattern_item::make_str("kitchen"));
    EXPECT_EQ(resolved[2], topic_pattern_item::make_plus());
}

TEST(topic_pattern_path_tests, cache_strategy_survives_binding) {
    const auto parsed = topic_pattern_path::parse("sensors/{id}/data", cache_strategy::lru(50));
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value().caching().capacity(), std::optional<std::size_t> { 50 });

    const auto bound = with_parameters(
        parsed.value(), {
                            { "id", "123" }
    }
    );
    ASSERT_TRUE(bound);
    EXPECT_EQ(bound.value().caching().capacity(), std::optional<std::size_t> { 50 });
}

TEST(topic_pattern_path_tests, cache_strategy_can_be_replaced) {
    const topic_pattern_path plain = create_pattern("sensors/{id}/data");
    const topic_pattern_path cached = plain.with_cache_strategy(cache_strategy::lru(8));
    EXPECT_EQ(cached.caching().capacity(), std::optional<std::size_t> { 8 });
    EXPECT_EQ(cached.topic_pattern(), plain.topic_pattern());
}

// --- formatting a topic for publishing ---

TEST(topic_pattern_path_tests, formats_a_topic_from_values) {
    const auto formatted = parse_ok("sensors/+/data/+").format_topic({ "kitchen", "23.5" });
    ASSERT_TRUE(formatted);
    EXPECT_EQ(formatted.value(), "sensors/kitchen/data/23.5");
}

TEST(topic_pattern_path_tests, rejects_formatting_a_hash_pattern) {
    const auto formatted = parse_ok("logs/#").format_topic({ "a" });
    ASSERT_FALSE(formatted);
    EXPECT_TRUE(std::holds_alternative<hash_wildcard_not_supported_error>(formatted.error()));
}

TEST(topic_pattern_path_tests, rejects_a_value_count_mismatch) {
    const auto too_few = parse_ok("sensors/+/data/+").format_topic({ "kitchen" });
    ASSERT_FALSE(too_few);
    const auto* mismatch = std::get_if<parameter_count_mismatch_error>(&too_few.error());
    ASSERT_NE(mismatch, nullptr);
    EXPECT_EQ(mismatch->expected, 2U);
    EXPECT_EQ(mismatch->provided, 1U);

    const auto too_many = parse_ok("sensors/+/data").format_topic({ "a", "b" });
    ASSERT_FALSE(too_many);
    EXPECT_TRUE(std::holds_alternative<parameter_count_mismatch_error>(too_many.error()));
}

// --- compatibility ---

TEST(topic_pattern_path_tests, compatible_patterns_differ_only_in_their_literals) {
    const topic_pattern_path filter = parse_ok("sensors/+/data");
    const auto compatible = filter.check_compatibility(parse_ok("sensors/kitchen/data"));
    ASSERT_TRUE(compatible);
    EXPECT_EQ(compatible.value().topic_pattern(), "sensors/kitchen/data");
}

TEST(topic_pattern_path_tests, incompatible_patterns_differ_in_their_wildcards) {
    const topic_pattern_path filter = parse_ok("sensors/+/data");
    EXPECT_FALSE(filter.check_compatibility(parse_ok("sensors/#")));
    EXPECT_FALSE(filter.check_compatibility(parse_ok("sensors/+/data/extra")));
    EXPECT_FALSE(filter.check_compatibility(parse_ok("sensors/data")));
}

// --- integration: patterns from the Rust suite's real-world shapes ---

TEST(topic_pattern_path_integration_tests, sensor_reading_pattern) {
    const auto bound = with_parameters(
        create_pattern("typed/{room}/pl/{sensor_id}/some/{temp}"), {
                                                                       { "room",      "kitchen"      },
                                                                       { "sensor_id", "floor_sensor" },
                                                                       { "temp",      "23.5"         }
    }
    );
    ASSERT_TRUE(bound);
    EXPECT_EQ(bound.value().mqtt_pattern(), "typed/kitchen/pl/floor_sensor/some/23.5");
}

TEST(topic_pattern_path_integration_tests, iot_telemetry_pattern) {
    const auto bound = with_parameters(
        create_pattern("iot/{building}/{floor}/devices/{device_type}/{device_id}/telemetry/{metric}"),
        {
            { "building",    "office-a"    },
            { "floor",       "3"           },
            { "device_type", "temperature" },
            { "device_id",   "temp-001"    },
            { "metric",      "celsius"     }
    }
    );
    ASSERT_TRUE(bound);
    EXPECT_EQ(bound.value().mqtt_pattern(), "iot/office-a/3/devices/temperature/temp-001/telemetry/celsius");
}

TEST(topic_pattern_path_integration_tests, log_aggregation_pattern) {
    const auto bound = with_parameters(
        create_pattern("logs/{service}/{level}/{details:#}"), {
                                                                  { "service", "auth-service" },
                                                                  { "level",   "error"        }
    }
    );
    ASSERT_TRUE(bound);
    EXPECT_EQ(bound.value().topic_pattern(), "logs/{service}/{level}/{details:#}");
    EXPECT_EQ(bound.value().mqtt_pattern(), "logs/auth-service/error/#");
}

// --- MQTT §4.7.2: a leading wildcard must not match a `$`-topic ---

TEST(topic_pattern_path_dollar_tests, leading_wildcards_do_not_match_dollar_topics) {
    EXPECT_FALSE(matches("#", "$SYS/broker/uptime"));
    EXPECT_FALSE(matches("+/monitor", "$SYS/monitor"));
    EXPECT_FALSE(matches("+", "$SYS"));
    // A named wildcard is the same level as `+`/`#` and is excluded too.
    EXPECT_FALSE(matches("{id}", "$SYS"));
    EXPECT_FALSE(matches("{rest:#}", "$SYS/broker/uptime"));
}

TEST(topic_pattern_path_dollar_tests, a_literal_first_level_still_matches_dollar_topics) {
    EXPECT_TRUE(matches("$SYS/#", "$SYS/broker/uptime"));
    EXPECT_TRUE(matches("$SYS/+", "$SYS/monitor"));
    EXPECT_TRUE(matches("$SYS/#", "$SYS")); // `#` matches the parent level too
}

TEST(topic_pattern_path_dollar_tests, a_dollar_below_the_first_level_is_ordinary_literal) {
    EXPECT_TRUE(matches("sport/#", "sport/$inner/x"));
    EXPECT_TRUE(matches("sport/+", "sport/$inner"));
}

TEST(topic_pattern_path_dollar_tests, a_parameter_bound_to_a_dollar_value_is_a_literal) {
    // Binding the first parameter resolves the level, so the trie sees an exact child and the
    // exclusion no longer applies.
    const auto bound = create_pattern("{svc}/data").bind_parameter("svc", "$SYS");
    ASSERT_TRUE(bound);
    EXPECT_TRUE(bound.value().match_str("$SYS/data"));

    EXPECT_FALSE(matches("{svc}/data", "$SYS/data"));
}

TEST(topic_pattern_path_dollar_tests, the_exclusion_is_a_named_error) {
    const auto matched = create_pattern("#").match_str("$SYS/broker/uptime");
    ASSERT_FALSE(matched);
    EXPECT_TRUE(std::holds_alternative<dollar_topic_exclusion_error>(matched.error()));
    EXPECT_NE(describe(matched.error()).find("$SYS/broker/uptime"), std::string::npos);
}

// --- matching, end to end through the filter ---

TEST(topic_pattern_path_tests, matches_capture_parameters) {
    const auto matched = create_pattern("sensors/{sensor_id}/data/+").match_str("sensors/kitchen/data/23.5");
    ASSERT_TRUE(matched);
    const auto result = std::move(matched).value();
    EXPECT_EQ(result->named_param("sensor_id"), std::optional<std::string_view> { "kitchen" });
    EXPECT_EQ(result->param(1), "23.5");
}

TEST(topic_pattern_path_tests, hash_matches_the_parent_and_everything_below) {
    EXPECT_TRUE(create_pattern("logs/#").match_str("logs"));
    EXPECT_TRUE(create_pattern("logs/#").match_str("logs/auth/error"));
    EXPECT_FALSE(create_pattern("logs/#").match_str("metrics/error"));
}

TEST(topic_pattern_path_tests, one_topic_matched_against_a_shared_topic_path) {
    const auto topic = topic_path::parse("sensors/kitchen/data");
    ASSERT_TRUE(topic);
    const std::shared_ptr<const topic_path> shared = std::move(topic).value();

    EXPECT_TRUE(create_pattern("sensors/+/data").match(shared));
    EXPECT_TRUE(create_pattern("sensors/#").match(shared));
    EXPECT_FALSE(create_pattern("sensors/+/reading").match(shared));
}

TEST(topic_pattern_path_tests, copies_share_bindings_but_not_the_cache) {
    const auto bound = with_parameters(
        create_pattern("sensors/{id}/data"), {
                                                 { "id", "kitchen" }
    }
    );
    ASSERT_TRUE(bound);

    const topic_pattern_path copy = bound.value();
    EXPECT_EQ(copy.mqtt_pattern(), "sensors/kitchen/data");
    EXPECT_EQ(copy.bound_value("id"), std::optional<std::string> { "kitchen" });
}

} // namespace
} // namespace ba7lya::topic_engine
