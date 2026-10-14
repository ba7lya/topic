///
/// @file qos.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief MQTT Quality of Service levels, defined independently of any MQTT client library.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///
// SPDX-License-Identifier: MIT

#pragma once

#include <compare>
#include <cstdint>
#include <string>

namespace ba7lya::topic {

///
/// @brief Delivery guarantees, weakest first.
/// @details The numeric values are the wire values a broker speaks, so a level can be handed to a client library unchanged.
/// The numeric values are the wire values a broker speaks, so a level can be handed to a client library unchanged.
///
enum class qos : std::uint8_t {
    AT_MOST_ONCE = 0,  ///< Best-effort delivery, fire and forget.
    AT_LEAST_ONCE = 1, ///< Acknowledged delivery, duplicates possible.
    EXACTLY_ONCE = 2,  ///< Assured delivery, exactly once.
};

/// @brief Orders the levels by strength, so the highest of a set is a `std::max` away.
/// @param lhs The left level.
/// @param rhs The right level.
/// @return The three-way comparison of the two wire values.
/// @details A single three-way operator is all the class needs: a scoped enum has no built-in ordering, but a comparison
/// A single three-way operator is all the class needs: a scoped enum has no built-in ordering, but a comparison
/// category in hand gives `==`, `!=`, `<`, `<=`, `>` and `>=` by rewriting, so none of them is written out by hand.
[[nodiscard]]
constexpr std::strong_ordering operator<=>(qos lhs, qos rhs) noexcept {
    return static_cast<std::uint8_t>(lhs) <=> static_cast<std::uint8_t>(rhs);
}

/// @brief Renders the level the way client libraries spell it: `"QoS0"`, `"QoS1"`, `"QoS2"`.
/// @param level The level to render.
/// @return The rendered text.
[[nodiscard]]
std::string to_string(qos level);

} // namespace ba7lya::topic
