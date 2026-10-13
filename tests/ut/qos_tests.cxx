// SPDX-License-Identifier: MIT
//
// `qos`: the wire values, the ordering the router relies on, and the rendering.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "qos.hxx"

namespace ba7lya::topic_engine {
namespace {

TEST(qos_tests, wire_values) {
    EXPECT_EQ(static_cast<std::uint8_t>(qos::AT_MOST_ONCE), 0);
    EXPECT_EQ(static_cast<std::uint8_t>(qos::AT_LEAST_ONCE), 1);
    EXPECT_EQ(static_cast<std::uint8_t>(qos::EXACTLY_ONCE), 2);
}

TEST(qos_tests, orders_by_strength) {
    EXPECT_LT(qos::AT_MOST_ONCE, qos::AT_LEAST_ONCE);
    EXPECT_LT(qos::AT_LEAST_ONCE, qos::EXACTLY_ONCE);
    EXPECT_GT(qos::EXACTLY_ONCE, qos::AT_MOST_ONCE);
    EXPECT_EQ(qos::AT_LEAST_ONCE, qos::AT_LEAST_ONCE);
}

TEST(qos_tests, max_picks_the_highest) {
    const std::vector<qos> levels { qos::EXACTLY_ONCE, qos::AT_MOST_ONCE, qos::AT_LEAST_ONCE };
    EXPECT_EQ(*std::ranges::max_element(levels), qos::EXACTLY_ONCE);
}

TEST(qos_tests, display) {
    EXPECT_EQ(to_string(qos::AT_MOST_ONCE), "QoS0");
    EXPECT_EQ(to_string(qos::AT_LEAST_ONCE), "QoS1");
    EXPECT_EQ(to_string(qos::EXACTLY_ONCE), "QoS2");
}

} // namespace
} // namespace ba7lya::topic_engine
