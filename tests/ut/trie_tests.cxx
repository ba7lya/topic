///
/// @file trie_tests.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Unit tests for `trie.hxx`: the `trie` structure - how a filter is inserted, how a removal prunes the
/// levels it leaves behind, and how the tree enumerates what is still in it.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///
// SPDX-License-Identifier: MIT

#include <cstddef>
#include <gtest/gtest.h>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "test_support.hxx"
#include "topic.hxx"
#include "trie_support.hxx"

namespace ba7lya::topic {
namespace {

using test::active_subscriptions;
using test::expected_active;
using test::ids;

// --- insertion and enumeration ---

TEST(trie_tests, reports_its_active_subscriptions) {
    trie<ids> root;
    for (const char* text : { "sensors/+/reading", "devices/+/+/state", "home/#", "#", "/" }) {
        const auto filter = topic_pattern_path::parse(text);
        ASSERT_TRUE(filter) << text;
        root.get_or_create_subscription_table(filter.value()).insert(1);
    }

    EXPECT_EQ(
        active_subscriptions(root), //
        expected_active(
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

TEST(trie_tests, counts_the_subscribers_behind_each_pattern) {
    trie<ids> root;
    const auto sensors = topic_pattern_path::parse("sensors/temperature");
    const auto home = topic_pattern_path::parse("home/#");
    ASSERT_TRUE(sensors);
    ASSERT_TRUE(home);

    for (std::size_t id : { 1U, 2U, 3U }) { root.get_or_create_subscription_table(sensors.value()).insert(id); }
    for (std::size_t id : { 4U, 5U }) { root.get_or_create_subscription_table(home.value()).insert(id); }

    EXPECT_EQ(
        active_subscriptions(root), //
        expected_active(
            {
                { "sensors/temperature", 3 },
                { "home/#",              2 }
    }
        )
    );
}

// --- removal, which is where the pruning shows ---

TEST(trie_tests, removing_a_literal_subscription) {
    trie<ids> root;
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

TEST(trie_tests, removing_a_wildcard_subscription) {
    trie<ids> root;
    const auto filter = topic_pattern_path::parse("sensors/+/reading");
    ASSERT_TRUE(filter);
    const auto segments = filter.value().resolve_bound_segments();
    root.get_or_create_subscription_table(filter.value()).insert(1);

    const auto pruned = root.update_node(segments, [](ids& table) { table.erase(1); });
    ASSERT_TRUE(pruned);
    EXPECT_TRUE(pruned.value());
    EXPECT_TRUE(root.is_empty());
}

TEST(trie_tests, removing_a_hash_subscription) {
    trie<ids> root;
    const auto filter = topic_pattern_path::parse("home/#");
    ASSERT_TRUE(filter);
    const auto segments = filter.value().resolve_bound_segments();
    root.get_or_create_subscription_table(filter.value()).insert(1);

    const auto pruned = root.update_node(segments, [](ids& table) { table.erase(1); });
    ASSERT_TRUE(pruned);
    EXPECT_TRUE(pruned.value());
    EXPECT_TRUE(root.is_empty());
}

TEST(trie_tests, removing_one_of_several_subscribers_keeps_the_pattern) {
    trie<ids> root;
    const auto filter = topic_pattern_path::parse("sensors/temperature");
    ASSERT_TRUE(filter);
    const auto segments = filter.value().resolve_bound_segments();
    root.get_or_create_subscription_table(filter.value()).insert(1);
    root.get_or_create_subscription_table(filter.value()).insert(2);

    const auto pruned = root.update_node(segments, [](ids& table) { table.erase(1); });
    ASSERT_TRUE(pruned);
    EXPECT_FALSE(pruned.value()) << "the pattern still has a subscriber, so the node stays";
    EXPECT_EQ(
        active_subscriptions(root), //
        expected_active(
            {
                { "sensors/temperature", 1 }
    }
        )
    );
}

TEST(trie_tests, removing_an_absent_level_is_an_error) {
    trie<ids> root;
    std::vector<topic_pattern_item> missing { topic_pattern_item::make_str("nowhere") };

    const auto pruned = root.update_node(missing, [](ids&) {
        // Never reached: the level is not in the tree.
    });
    ASSERT_FALSE(pruned);
    const auto* invalid = std::get_if<invalid_segment_error>(&pruned.error());
    ASSERT_NE(invalid, nullptr);
    EXPECT_EQ(invalid->segment, "nowhere");
}

TEST(trie_tests, removing_the_last_level_is_an_error_when_nothing_was_stored) {
    trie<ids> root;
    const auto pruned = root.update_node(std::span<const topic_pattern_item> {}, [](ids&) {
        // Never reached: an empty path addresses this node's own payload, and there is none.
    });
    ASSERT_FALSE(pruned);
    EXPECT_TRUE(std::holds_alternative<invalid_segment_error>(pruned.error()));
}

TEST(trie_tests, removing_mixed_subscriptions_lifts_the_patterns_that_remain) {
    trie<ids> root;

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

    // Each surviving pattern keeps the one id that was inserted under it, so the counts below are
    // per-pattern id counts rather than a tally of registrations.
    EXPECT_EQ(
        active_subscriptions(root), //
        expected_active(
            {
                { "sensors/humidity",   1 },
                { "sensors/#",          1 },
                { "home/+/temperature", 1 }
            }
        )
    );
}

TEST(trie_tests, removing_every_subscription_empties_the_tree) {
    trie<ids> root;
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
} // namespace ba7lya::topic
