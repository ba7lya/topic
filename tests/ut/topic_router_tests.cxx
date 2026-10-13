// SPDX-License-Identifier: MIT
//
// `topic_router`: registration bookkeeping — ids, the broker-subscription decisions, and the
// payloads a delivered topic resolves to.

#include <algorithm>
#include <cstddef>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "test_support.hxx"
#include "topic_engine.hxx"

namespace ba7lya::topic_engine {
namespace {

using test::create_pattern;

using router = topic_router<std::string>;

/// Looks a filter up by id, asserting it is registered.
[[nodiscard]]
const router::subscription_record& record_of(const router& routed, subscription_id id) {
    const auto found = routed.get_topic_by_id(id);
    if (!found) { throw std::runtime_error("subscription should exist: " + describe(found.error())); }
    return *found.value();
}

/// The ids of every subscriber `topic` resolves to, in ascending order.
[[nodiscard]]
std::vector<std::size_t> subscriber_ids(const router& routed, const std::string& topic) {
    const auto parsed = topic_path::parse(topic);
    if (!parsed) { throw std::runtime_error("topic should be valid: " + topic); }

    std::vector<std::size_t> ids;
    for (const router::subscriber& subscriber : routed.get_subscribers(*parsed.value())) {
        ids.push_back(subscriber.id.value);
    }
    std::ranges::sort(ids);
    return ids;
}

/// The ids the registry holds, in ascending order.
[[nodiscard]]
std::vector<std::size_t> active_ids(const router& routed) {
    std::vector<std::size_t> ids;
    for (const auto& [id, record] : routed.get_active_subscriptions()) { ids.push_back(id.value); }
    std::ranges::sort(ids);
    return ids;
}

// --- registration ---

TEST(topic_router_tests, the_first_subscription_to_a_pattern_needs_a_broker_subscribe) {
    router routed;
    const auto [needs_subscribe, id]
        = routed.add_subscription(create_pattern("sensors/temperature"), qos::AT_LEAST_ONCE, "first");

    EXPECT_TRUE(needs_subscribe);
    EXPECT_EQ(id.value, 0U);
    EXPECT_EQ(record_of(routed, id).first.mqtt_pattern(), "sensors/temperature");
    EXPECT_EQ(record_of(routed, id).second, qos::AT_LEAST_ONCE);
}

TEST(topic_router_tests, ids_are_handed_out_in_order) {
    router routed;
    const auto first = routed.add_subscription(create_pattern("a"), qos::AT_MOST_ONCE, "a");
    const auto second = routed.add_subscription(create_pattern("b"), qos::AT_MOST_ONCE, "b");
    const auto third = routed.add_subscription(create_pattern("c"), qos::AT_MOST_ONCE, "c");

    EXPECT_EQ(first.second.value, 0U);
    EXPECT_EQ(second.second.value, 1U);
    EXPECT_EQ(third.second.value, 2U);
    // Every id here is a distinct broker subscription, so each registration needs one.
    EXPECT_TRUE(first.first);
    EXPECT_TRUE(second.first);
    EXPECT_TRUE(third.first);
}

TEST(topic_router_tests, a_second_subscription_at_the_same_qos_needs_no_resubscribe) {
    router routed;
    (void)routed.add_subscription(create_pattern("sensors/temperature"), qos::AT_LEAST_ONCE, "first");
    const auto [needs_subscribe, id]
        = routed.add_subscription(create_pattern("sensors/temperature"), qos::AT_LEAST_ONCE, "second");

    EXPECT_FALSE(needs_subscribe);
    EXPECT_EQ(id.value, 1U);
    EXPECT_EQ(active_ids(routed), std::vector<std::size_t>({ 0U, 1U }));
}

TEST(topic_router_tests, a_lower_qos_needs_no_resubscribe_and_a_higher_one_does) {
    router routed;
    (void)routed.add_subscription(create_pattern("sensors/temperature"), qos::AT_LEAST_ONCE, "qos1");

    const auto lower = routed.add_subscription(create_pattern("sensors/temperature"), qos::AT_MOST_ONCE, "qos0");
    EXPECT_FALSE(lower.first) << "the broker is already delivering at least this well";

    const auto higher = routed.add_subscription(create_pattern("sensors/temperature"), qos::EXACTLY_ONCE, "qos2");
    EXPECT_TRUE(higher.first) << "the effective QoS rose, so the broker has to be told";
}

TEST(topic_router_tests, two_templates_that_differ_only_in_a_parameter_name_share_a_subscription) {
    router routed;
    (void)routed.add_subscription(create_pattern("sensors/{id}/data"), qos::AT_LEAST_ONCE, "named");
    const auto anonymous = routed.add_subscription(create_pattern("sensors/+/data"), qos::AT_LEAST_ONCE, "plus");

    // Both resolve to the same wire pattern, so the broker already has it.
    EXPECT_FALSE(anonymous.first);
}

TEST(topic_router_tests, a_bound_parameter_registers_as_its_value) {
    const auto bound = create_pattern("sensors/{id}/data").bind_parameter("id", "kitchen");
    ASSERT_TRUE(bound);

    router routed;
    const auto [needs_subscribe, id] = routed.add_subscription(bound.value(), qos::AT_MOST_ONCE, "kitchen");
    EXPECT_TRUE(needs_subscribe);
    EXPECT_EQ(record_of(routed, id).first.mqtt_pattern(), "sensors/kitchen/data");

    EXPECT_EQ(subscriber_ids(routed, "sensors/kitchen/data"), std::vector<std::size_t>({ 0U }));
    EXPECT_TRUE(subscriber_ids(routed, "sensors/other/data").empty());
}

// --- looking up by id ---

TEST(topic_router_tests, an_unknown_id_is_reported_by_id) {
    router routed;
    const auto found = routed.get_topic_by_id(subscription_id { 42 });
    ASSERT_FALSE(found);
    const auto* missing = std::get_if<subscription_not_found_error>(&found.error());
    ASSERT_NE(missing, nullptr);
    EXPECT_EQ(missing->id.value, 42U);
    EXPECT_NE(describe(found.error()).find("42"), std::string::npos);
}

// --- routing a delivered topic ---

TEST(topic_router_tests, a_topic_resolves_to_every_matching_subscription) {
    router routed;
    const auto kitchen
        = routed.add_subscription(create_pattern("home/kitchen/temperature"), qos::AT_MOST_ONCE, "exact");
    const auto any_room = routed.add_subscription(create_pattern("home/+/temperature"), qos::AT_LEAST_ONCE, "any room");
    const auto home = routed.add_subscription(create_pattern("home/#"), qos::EXACTLY_ONCE, "all of home");

    EXPECT_EQ(subscriber_ids(routed, "home/kitchen/temperature"), std::vector<std::size_t>({ 0U, 1U, 2U }));
    EXPECT_EQ(subscriber_ids(routed, "home/hall/temperature"), std::vector<std::size_t>({ 1U, 2U }));
    EXPECT_TRUE(subscriber_ids(routed, "garden/hall/temperature").empty());

    // The payload and the recorded QoS travel with the subscriber.
    const auto parsed = topic_path::parse("home/kitchen/temperature");
    ASSERT_TRUE(parsed);
    const std::vector<router::subscriber> matched = routed.get_subscribers(*parsed.value());
    ASSERT_EQ(matched.size(), 3U);
    for (const router::subscriber& subscriber : matched) {
        if (subscriber.id == kitchen.second) {
            EXPECT_EQ(*subscriber.payload, "exact");
            EXPECT_EQ(subscriber.effective_qos, qos::AT_MOST_ONCE);
        }
        if (subscriber.id == any_room.second) {
            EXPECT_EQ(*subscriber.payload, "any room");
            EXPECT_EQ(subscriber.effective_qos, qos::AT_LEAST_ONCE);
        }
        if (subscriber.id == home.second) {
            EXPECT_EQ(*subscriber.payload, "all of home");
            EXPECT_EQ(subscriber.effective_qos, qos::EXACTLY_ONCE);
        }
        ASSERT_NE(subscriber.pattern, nullptr);
    }
}

TEST(topic_router_tests, a_leading_wildcard_does_not_route_dollar_topics) {
    router routed;
    (void)routed.add_subscription(create_pattern("#"), qos::AT_MOST_ONCE, "everything");
    (void)routed.add_subscription(create_pattern("$SYS/#"), qos::AT_MOST_ONCE, "sys");

    EXPECT_EQ(subscriber_ids(routed, "$SYS/broker/uptime"), std::vector<std::size_t>({ 1U }));
    EXPECT_EQ(subscriber_ids(routed, "sensors/temperature"), std::vector<std::size_t>({ 0U }));
}

// --- removal ---

TEST(topic_router_tests, unsubscribing_reports_the_pattern_it_emptied) {
    router routed;
    const auto first = routed.add_subscription(create_pattern("sensors/temperature"), qos::AT_MOST_ONCE, "one");
    (void)routed.add_subscription(create_pattern("sensors/temperature"), qos::AT_MOST_ONCE, "two");

    const auto removed = routed.unsubscribe(first.second);
    ASSERT_TRUE(removed);
    EXPECT_FALSE(removed.value().first) << "a subscriber is left, so the broker subscription stays";
    EXPECT_EQ(removed.value().second.mqtt_pattern(), "sensors/temperature");
    EXPECT_EQ(active_ids(routed), std::vector<std::size_t>({ 1U }));
}

TEST(topic_router_tests, unsubscribing_the_last_subscriber_empties_the_pattern) {
    router routed;
    const auto only = routed.add_subscription(create_pattern("sensors/temperature"), qos::AT_MOST_ONCE, "one");

    const auto removed = routed.unsubscribe(only.second);
    ASSERT_TRUE(removed);
    EXPECT_TRUE(removed.value().first) << "this is the caller's signal to unsubscribe on the broker";
    EXPECT_TRUE(active_ids(routed).empty());
    EXPECT_TRUE(subscriber_ids(routed, "sensors/temperature").empty());
}

TEST(topic_router_tests, unsubscribing_an_unknown_id_fails) {
    router routed;
    (void)routed.add_subscription(create_pattern("sensors/temperature"), qos::AT_MOST_ONCE, "one");

    const auto removed = routed.unsubscribe(subscription_id { 7 });
    ASSERT_FALSE(removed);
    EXPECT_TRUE(std::holds_alternative<subscription_not_found_error>(removed.error()));
}

TEST(topic_router_tests, unsubscribing_a_wildcard_subscription_prunes_the_branch) {
    router routed;
    const auto exact = routed.add_subscription(create_pattern("sensors/temperature"), qos::AT_MOST_ONCE, "exact");
    const auto plus = routed.add_subscription(create_pattern("sensors/+/reading"), qos::AT_MOST_ONCE, "plus");
    const auto hash = routed.add_subscription(create_pattern("sensors/#"), qos::AT_MOST_ONCE, "hash");

    for (const subscription_id id : { exact.second, plus.second, hash.second }) {
        const auto removed = routed.unsubscribe(id);
        ASSERT_TRUE(removed) << id.value;
        EXPECT_TRUE(removed.value().first) << id.value;
    }

    EXPECT_TRUE(active_ids(routed).empty());
    EXPECT_TRUE(subscriber_ids(routed, "sensors/temperature").empty());
}

TEST(topic_router_tests, cleanup_drops_everything_and_restarts_the_id_counter) {
    router routed;
    (void)routed.add_subscription(create_pattern("sensors/temperature"), qos::AT_MOST_ONCE, "one");
    (void)routed.add_subscription(create_pattern("home/#"), qos::AT_MOST_ONCE, "two");

    routed.cleanup();

    EXPECT_TRUE(active_ids(routed).empty());
    EXPECT_TRUE(routed.get_topics_for_unsubscribe().empty());
    EXPECT_TRUE(routed.get_topics_for_resubscribe().empty());

    const auto after = routed.add_subscription(create_pattern("sensors/temperature"), qos::AT_MOST_ONCE, "again");
    EXPECT_EQ(after.second.value, 0U);
    EXPECT_TRUE(after.first);
}

// --- what the caller has to tell the broker ---

TEST(topic_router_tests, unsubscribe_topics_are_deduplicated_by_wire_pattern) {
    router routed;
    (void)routed.add_subscription(create_pattern("sensors/{id}/data"), qos::AT_MOST_ONCE, "named");
    (void)routed.add_subscription(create_pattern("sensors/{name}/data"), qos::AT_MOST_ONCE, "renamed");
    (void)routed.add_subscription(create_pattern("home/#"), qos::AT_MOST_ONCE, "home");

    // Two templates, one wire pattern: the broker only ever saw `sensors/+/data` once.
    EXPECT_EQ(routed.get_topics_for_unsubscribe(), std::unordered_set<std::string>({ "sensors/+/data", "home/#" }));
}

TEST(topic_router_tests, resubscribe_topics_carry_the_highest_qos_of_the_pattern) {
    router routed;
    (void)routed.add_subscription(create_pattern("sensors/temperature"), qos::AT_MOST_ONCE, "low");
    (void)routed.add_subscription(create_pattern("sensors/temperature"), qos::EXACTLY_ONCE, "high");
    (void)routed.add_subscription(create_pattern("home/#"), qos::AT_LEAST_ONCE, "home");

    const std::unordered_map<std::string, qos> expected(
        {
            { "sensors/temperature", qos::EXACTLY_ONCE  },
            { "home/#",              qos::AT_LEAST_ONCE }
    }
    );
    EXPECT_EQ(routed.get_topics_for_resubscribe(), expected);
}

TEST(topic_router_tests, a_hash_only_filter_subscribes_as_hash) {
    const auto bound = create_pattern("{rest:#}").bind_parameter("rest", "");
    ASSERT_TRUE(bound);

    router routed;
    (void)routed.add_subscription(bound.value(), qos::AT_MOST_ONCE, "everything");

    // No phantom leading level: the wire pattern is `#`, not `/#`.
    EXPECT_EQ(routed.get_topics_for_resubscribe().at("#"), qos::AT_MOST_ONCE);
}

TEST(topic_router_tests, a_rejected_pattern_never_reaches_the_router) {
    // Patterns are parsed before they are registered, so the router only ever sees valid ones.
    const auto rejected = topic_pattern_path::parse("invalid/#/pattern");
    ASSERT_FALSE(rejected);
    EXPECT_TRUE(std::holds_alternative<hash_position_error>(rejected.error()));
}

// --- the active view ---

TEST(topic_router_tests, active_subscriptions_show_each_registration) {
    router routed;
    const auto first = routed.add_subscription(create_pattern("sensors/temperature"), qos::AT_MOST_ONCE, "one");
    const auto second = routed.add_subscription(create_pattern("home/#"), qos::AT_LEAST_ONCE, "two");

    EXPECT_EQ(active_ids(routed), std::vector<std::size_t>({ first.second.value, second.second.value }));

    const auto& registry = routed.get_active_subscriptions();
    EXPECT_EQ(registry.size(), 2U);
    EXPECT_EQ(registry.at(first.second).first.mqtt_pattern(), "sensors/temperature");
    EXPECT_EQ(registry.at(first.second).second, qos::AT_MOST_ONCE);
    EXPECT_EQ(registry.at(second.second).first.mqtt_pattern(), "home/#");
    EXPECT_EQ(registry.at(second.second).second, qos::AT_LEAST_ONCE);
}

} // namespace
} // namespace ba7lya::topic_engine
