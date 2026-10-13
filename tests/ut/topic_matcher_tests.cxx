// SPDX-License-Identifier: MIT
//
// `topic_matcher_node`: the trie behind the router — one pattern per subscription, and the set of
// subscriptions a delivered topic resolves to.

#include <cstddef>
#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

#include "test_support.hxx"
#include "topic_engine.hxx"
#include "trie_support.hxx"

namespace ba7lya::topic_engine {
namespace {

using test::active_subscriptions;
using test::expected_active;
using test::ids;

/// One subscription to register: its filter text and the id to store under it.
struct subscription {
    std::string pattern;
    std::size_t id = 0;
};

/// One delivered topic and the subscription ids it must resolve to.
struct delivery {
    std::string topic;
    ids expected;
};

/// Builds a trie from `subscriptions` and checks every `deliveries` entry.
void expect_deliveries(const std::vector<subscription>& subscriptions, const std::vector<delivery>& deliveries) {
    topic_matcher_node<ids> root;
    for (const subscription& entry : subscriptions) {
        const auto filter = topic_pattern_path::parse(entry.pattern);
        ASSERT_TRUE(filter) << entry.pattern;
        root.get_or_create_subscription_table(filter.value()).insert(entry.id);
    }

    for (const delivery& entry : deliveries) {
        const auto topic = topic_path::parse(entry.topic);
        ASSERT_TRUE(topic) << entry.topic;

        ids found;
        for (const ids* payload : root.find_by_path(*topic.value())) { found.insert(payload->begin(), payload->end()); }
        EXPECT_EQ(found, entry.expected) << entry.topic;
    }
}

// --- literal matching ---

TEST(topic_matcher_tests, exact_matches) {
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

TEST(topic_matcher_tests, a_topic_may_reach_several_subscriptions) {
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

TEST(topic_matcher_tests, dollar_topics_are_excluded_from_leading_wildcards) {
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

TEST(topic_matcher_tests, plus_wildcards) {
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

TEST(topic_matcher_tests, hash_wildcards) {
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

TEST(topic_matcher_tests, degenerate_wildcards) {
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

TEST(topic_matcher_tests, several_subscribers_share_one_pattern) {
    topic_matcher_node<ids> root;
    const auto filter = topic_pattern_path::parse("sensors/temperature");
    ASSERT_TRUE(filter);
    root.get_or_create_subscription_table(filter.value()).insert(1);
    root.get_or_create_subscription_table(filter.value()).insert(2);

    const auto topic = topic_path::parse("sensors/temperature");
    ASSERT_TRUE(topic);
    // Both subscribers are behind a single pattern, so the tree has one payload to hand back.
    const std::vector<const ids*> matched = root.find_by_path(*topic.value());
    ASSERT_EQ(matched.size(), 1U);
    EXPECT_EQ(*matched[0], ids({ 1, 2 }));
}

TEST(topic_matcher_tests, one_subscriber_may_hold_several_patterns) {
    expect_deliveries(
        {
            { "sensors/temperature", 1 },
            { "sensors/+",           1 },
            { "sensors/#",           1 }
    },
        { { "sensors/temperature", { 1 } }, { "sensors/humidity", { 1 } }, { "sensors", { 1 } } }
    );
}

TEST(topic_matcher_tests, reports_its_active_subscriptions) {
    topic_matcher_node<ids> root;
    for (const char* text : { "sensors/+/reading", "devices/+/+/state", "home/#", "#", "/" }) {
        const auto filter = topic_pattern_path::parse(text);
        ASSERT_TRUE(filter) << text;
        root.get_or_create_subscription_table(filter.value()).insert(1);
    }

    EXPECT_EQ(
        active_subscriptions(root), expected_active(
                                        {
                                            { "sensors/+/reading", 1 },
                                            { "devices/+/+/state", 1 },
                                            { "home/#",            1 },
                                            { "#",                 1 },
                                            { "/",                 1 }
    }
                                    )
    );
}

TEST(topic_matcher_tests, counts_the_subscribers_behind_each_pattern) {
    topic_matcher_node<ids> root;
    const auto sensors = topic_pattern_path::parse("sensors/temperature");
    const auto home = topic_pattern_path::parse("home/#");
    ASSERT_TRUE(sensors);
    ASSERT_TRUE(home);

    for (std::size_t id : { 1U, 2U, 3U }) { root.get_or_create_subscription_table(sensors.value()).insert(id); }
    for (std::size_t id : { 4U, 5U }) { root.get_or_create_subscription_table(home.value()).insert(id); }

    EXPECT_EQ(
        active_subscriptions(root), expected_active(
                                        {
                                            { "sensors/temperature", 3 },
                                            { "home/#",              2 }
    }
                                    )
    );
}

// --- removal, which is where the pruning shows ---

TEST(topic_matcher_tests, removing_a_literal_subscription) {
    topic_matcher_node<ids> root;
    const auto filter = topic_pattern_path::parse("sensors/temperature");
    ASSERT_TRUE(filter);
    const auto segments = filter.value().resolve_bound_segments();
    root.get_or_create_subscription_table(filter.value()).insert(1);

    const auto pruned = root.update_node(segments, [](ids& table) { table.erase(1); });
    ASSERT_TRUE(pruned);
    EXPECT_TRUE(pruned.value());
    EXPECT_TRUE(root.is_empty());
    EXPECT_TRUE(active_subscriptions(root).empty());
}

TEST(topic_matcher_tests, removing_a_wildcard_subscription) {
    topic_matcher_node<ids> root;
    const auto filter = topic_pattern_path::parse("sensors/+/reading");
    ASSERT_TRUE(filter);
    const auto segments = filter.value().resolve_bound_segments();
    root.get_or_create_subscription_table(filter.value()).insert(1);

    const auto pruned = root.update_node(segments, [](ids& table) { table.erase(1); });
    ASSERT_TRUE(pruned);
    EXPECT_TRUE(pruned.value());
    EXPECT_TRUE(root.is_empty());
}

TEST(topic_matcher_tests, removing_a_hash_subscription) {
    topic_matcher_node<ids> root;
    const auto filter = topic_pattern_path::parse("home/#");
    ASSERT_TRUE(filter);
    const auto segments = filter.value().resolve_bound_segments();
    root.get_or_create_subscription_table(filter.value()).insert(1);

    const auto pruned = root.update_node(segments, [](ids& table) { table.erase(1); });
    ASSERT_TRUE(pruned);
    EXPECT_TRUE(pruned.value());
    EXPECT_TRUE(root.is_empty());
}

TEST(topic_matcher_tests, removing_one_of_several_subscribers_keeps_the_pattern) {
    topic_matcher_node<ids> root;
    const auto filter = topic_pattern_path::parse("sensors/temperature");
    ASSERT_TRUE(filter);
    const auto segments = filter.value().resolve_bound_segments();
    root.get_or_create_subscription_table(filter.value()).insert(1);
    root.get_or_create_subscription_table(filter.value()).insert(2);

    const auto pruned = root.update_node(segments, [](ids& table) { table.erase(1); });
    ASSERT_TRUE(pruned);
    EXPECT_FALSE(pruned.value()) << "the pattern still has a subscriber, so the node stays";
    EXPECT_EQ(
        active_subscriptions(root), expected_active(
                                        {
                                            { "sensors/temperature", 1 }
    }
                                    )
    );
}

TEST(topic_matcher_tests, removing_an_absent_level_is_an_error) {
    topic_matcher_node<ids> root;
    std::vector<topic_pattern_item> missing { topic_pattern_item::make_str("nowhere") };

    const auto pruned = root.update_node(missing, [](ids&) {
        // Never reached: the level is not in the tree.
    });
    ASSERT_FALSE(pruned);
    const auto* invalid = std::get_if<invalid_segment_error>(&pruned.error());
    ASSERT_NE(invalid, nullptr);
    EXPECT_EQ(invalid->segment, "nowhere");
}

TEST(topic_matcher_tests, removing_the_last_level_is_an_error_when_nothing_was_stored) {
    topic_matcher_node<ids> root;
    const auto pruned = root.update_node(std::span<const topic_pattern_item> {}, [](ids&) {
        // Never reached: an empty path addresses this node's own payload, and there is none.
    });
    ASSERT_FALSE(pruned);
    EXPECT_TRUE(std::holds_alternative<invalid_segment_error>(pruned.error()));
}

TEST(topic_matcher_tests, removing_mixed_subscriptions_lifts_the_patterns_that_remain) {
    topic_matcher_node<ids> root;

    struct op {
        const char* pattern;
        std::size_t id;
    };

    const std::vector<op> inserted {
        { "sensors/temperature", 1 },
        { "sensors/humidity",    2 },
        { "sensors/+",           3 },
        { "sensors/#",           4 },
        { "home/kitchen",        5 },
        { "home/+/temperature",  6 }
    };
    for (const op& entry : inserted) {
        const auto filter = topic_pattern_path::parse(entry.pattern);
        ASSERT_TRUE(filter) << entry.pattern;
        root.get_or_create_subscription_table(filter.value()).insert(entry.id);
    }

    for (const op& entry : {
             op { "sensors/temperature", 1 },
              op { "sensors/+",           3 },
              op { "home/kitchen",        5 }
    }) {
        const auto filter = topic_pattern_path::parse(entry.pattern);
        ASSERT_TRUE(filter);
        const auto pruned = root.update_node(filter.value().resolve_bound_segments(), [&entry](ids& table) {
            table.erase(entry.id);
        });
        ASSERT_TRUE(pruned) << entry.pattern;
    }

    EXPECT_EQ(
        active_subscriptions(root), expected_active(
                                        {
                                            { "sensors/humidity",   2 },
                                            { "sensors/#",          4 },
                                            { "home/+/temperature", 6 }
    }
                                    )
    );
}

TEST(topic_matcher_tests, removing_every_subscription_empties_the_tree) {
    topic_matcher_node<ids> root;
    const std::vector<const char*> patterns { "sensors/temperature", "sensors/+/reading", "sensors/#", "home/+/#" };
    for (std::size_t index = 0; index < patterns.size(); ++index) {
        const auto filter = topic_pattern_path::parse(patterns[index]);
        ASSERT_TRUE(filter) << patterns[index];
        root.get_or_create_subscription_table(filter.value()).insert(index);
    }

    for (std::size_t index = 0; index < patterns.size(); ++index) {
        const auto filter = topic_pattern_path::parse(patterns[index]);
        ASSERT_TRUE(filter);
        const auto pruned
            = root.update_node(filter.value().resolve_bound_segments(), [index](ids& table) { table.erase(index); });
        ASSERT_TRUE(pruned) << patterns[index];
    }

    EXPECT_TRUE(root.is_empty());
    EXPECT_TRUE(active_subscriptions(root).empty());
}

} // namespace
} // namespace ba7lya::topic_engine
