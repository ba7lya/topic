// SPDX-License-Identifier: MIT
//
// MQTT Quality of Service levels, defined independently of any MQTT client library.

#pragma once

#include <compare>
#include <cstdint>
#include <string>

namespace ba7lya::topic_engine {

/// MQTT Quality of Service levels.
///
/// The numeric values are the wire values from the MQTT specification.
enum class qos : std::uint8_t {
    AT_MOST_ONCE = 0,  ///< QoS 0: best-effort delivery, fire and forget.
    AT_LEAST_ONCE = 1, ///< QoS 1: acknowledged delivery, duplicates possible.
    EXACTLY_ONCE = 2,  ///< QoS 2: assured delivery, exactly once.
};

/// Orders the levels, so the highest QoS of a set is a `std::max` away.
[[nodiscard]]
constexpr std::strong_ordering operator<=>(qos lhs, qos rhs) noexcept {
    return static_cast<std::uint8_t>(lhs) <=> static_cast<std::uint8_t>(rhs);
}

/// Renders the level as the MQTT client libraries spell it: `"QoS0"`, `"QoS1"`, `"QoS2"`.
[[nodiscard]]
std::string to_string(qos level);

} // namespace ba7lya::topic_engine
