///
/// @file matcher_tests.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Unit tests for `matcher.hxx`: the matching rule - which filters a delivered topic name resolves to, the
/// `$`-reserved exclusion included.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///
// SPDX-License-Identifier: MIT

#include <cstddef>
#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

#include "test_support.hxx"
#include "topic.hxx"
#include "trie_support.hxx"

namespace ba7lya::topic {
namespace {

using test::ids;

///
/// @brief One subscription to register: its filter text and the id to store under it.
///
struct subscription {
    std::string pattern;
    std::size_t id = 0;
};

///
/// @brief One delivered topic and the subscription ids it must resolve to.
///
struct delivery {
    std::string topic;
    ids expected;
};

/// @brief Builds a matcher from `subscriptions` and checks every `deliveries` entry.
void expect_deliveries(const std::vector<subscription>& subscriptions, const std::vector<delivery>& deliveries) {
    topic_matcher<ids> matcher;
    for (const subscription& entry : subscriptions) {
        const auto filter = topic_pattern_path::parse(entry.pattern);
        ASSERT_TRUE(filter) << entry.pattern;
        matcher.get_or_create_subscription_table(filter.value()).insert(entry.id);
    }

    for (const delivery& entry : deliveries) {
        const auto topic = topic_path::parse(entry.topic);
        ASSERT_TRUE(topic) << entry.topic;

        ids found;
        for (const ids* payload : matcher.find_by_path(*topic.value())) {
            found.insert(payload->begin(), payload->end());
        }
        EXPECT_EQ(found, entry.expected) << entry.topic;
    }
}

// --- literal matching ---

TEST(matcher_tests, exact_matches) {
    expect_deliveries(
        {
            { "sensors/temperature",  1 },
            { "sensors/humidity",     2 },
            { "devices/light/status", 3 }
    },
        { { "sensors/temperature", { 1 } },
          { "sensors/humidity", { 2 } },
          { "devices/light/status", { 3 } },
          { "sensors/pressure", {} } }
    );
}

TEST(matcher_tests, a_topic_may_reach_several_subscriptions) {
    expect_deliveries(
        {
            { "home/kitchen/temperature", 1 },
            { "home/+/temperature",       2 },
            { "home/kitchen/+",           3 },
            { "home/#",                   4 },
            { "+/kitchen/#",              5 }
    },
        { { "home/kitchen/temperature", { 1, 2, 3, 4, 5 } },
          { "home/livingroom/temperature", { 2, 4 } },
          { "home/kitchen/humidity", { 3, 4, 5 } },
          { "home/kitchen/temperature/celsius", { 4, 5 } },
          { "office/kitchen/temperature", { 5 } },
          { "home", { 4 } } }
    );
}

// --- the `$`-topic exclusion (MQTT §4.7.2) ---

TEST(matcher_tests, dollar_topics_are_excluded_from_leading_wildcards) {
    expect_deliveries(
        {
            { "#",         1 },
            { "+/monitor", 2 },
            { "+",         3 },
            { "$SYS/#",    4 },
            { "$SYS/+",    5 },
            { "sport/#",   6 }
    },
        { { "$SYS/broker/uptime", { 4 } },
          { "$SYS/monitor", { 4, 5 } },
          { "$SYS", { 4 } },
          { "sport/tennis", { 1, 6 } },
          { "weather", { 1, 3 } },
          { "a/monitor", { 1, 2 } },
          { "sport/$inner", { 1, 6 } } }
    );
}

// --- `+` matching exactly one level ---

TEST(matcher_tests, plus_wildcards) {
    expect_deliveries(
        {
            { "sensors/+/reading", 1 },
            { "devices/+/+/state", 2 },
            { "home/+",            3 }
    },
        { { "sensors/temperature/reading", { 1 } },
          { "sensors/humidity/reading", { 1 } },
          { "sensors/temperature/value", {} },
          { "devices/light/kitchen/state", { 2 } },
          { "devices/light/state", {} },
          { "home/kitchen", { 3 } },
          { "home/livingroom", { 3 } },
          { "home/kitchen/temperature", {} } }
    );
}

// --- `#` matching the remaining levels, including none at all ---

TEST(matcher_tests, hash_wildcards) {
    expect_deliveries(
        {
            { "sensors/#",         1 },
            { "home/livingroom/#", 2 },
            { "#",                 3 }
    },
        { { "sensors", { 1, 3 } },
          { "sensors/temperature", { 1, 3 } },
          { "sensors/kitchen/temperature", { 1, 3 } },
          { "home/livingroom", { 2, 3 } },
          { "home/livingroom/light", { 2, 3 } },
          { "home/kitchen", { 3 } } }
    );
}

TEST(matcher_tests, degenerate_wildcards) {
    expect_deliveries(
        {
            { "#",   2 },
            { "+",   3 },
            { "+/+", 4 },
            { "+/#", 5 }
    },
        { { "segment", { 2, 3, 5 } }, { "segment1/segment2", { 2, 4, 5 } }, { "segment1/segment2/segment3", { 2, 5 } } }
    );
}

// --- several subscriptions sharing a pattern ---

TEST(matcher_tests, several_subscribers_share_one_pattern) {
    topic_matcher<ids> matcher;
    const auto filter = topic_pattern_path::parse("sensors/temperature");
    ASSERT_TRUE(filter);
    matcher.get_or_create_subscription_table(filter.value()).insert(1);
    matcher.get_or_create_subscription_table(filter.value()).insert(2);

    const auto topic = topic_path::parse("sensors/temperature");
    ASSERT_TRUE(topic);
    // Both subscribers are behind a single pattern, so the tree has one payload to hand back.
    const std::vector<const ids*> matched = matcher.find_by_path(*topic.value());
    ASSERT_EQ(matched.size(), 1U);
    EXPECT_EQ(*matched[0], ids({ 1, 2 }));
}

TEST(matcher_tests, one_subscriber_may_hold_several_patterns) {
    expect_deliveries(
        {
            { "sensors/temperature", 1 },
            { "sensors/+",           1 },
            { "sensors/#",           1 }
    },
        { { "sensors/temperature", { 1 } }, { "sensors/humidity", { 1 } }, { "sensors", { 1 } } }
    );
}

} // namespace
} // namespace ba7lya::topic
