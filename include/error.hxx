///
/// @file error.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Every error type the engine raises, from the well-formedness rules up to the router.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "result.hxx"

namespace ba7lya::topic {

///
/// @brief The topic name handed to `topic_path::parse` was empty (MQTT §4.7.3).
///
struct empty_topic_path_error {
    [[nodiscard]]
    std::string message() const;
};

///
/// @brief The topic name contained the null character (U+0000), which MQTT §4.7.3 forbids.
///
struct null_char_topic_path_error {
    [[nodiscard]]
    std::string message() const;
};

///
/// @brief Everything that can go wrong while constructing a topic name.
///
using topic_path_error = std::variant<empty_topic_path_error, null_char_topic_path_error>;

///
/// @brief MQTT §4.7.3 violation: the topic string must have at least one character.
///
struct empty_topic_error {};

///
/// @brief MQTT §4.7.3 violation: the topic string must not contain the null character (U+0000).
///
struct null_char_error {};

///
/// @brief A topic-string well-formedness violation.
/// @details Both the topic-name (`topic_path`) and the topic-filter (`topic_pattern_path`) constructors check the same
/// rule and each maps this onto its own public error type.
///
using malformed = std::variant<empty_topic_error, null_char_error>;

/// @brief MQTT §4.7.3: a topic name or filter MUST be non-empty and MUST NOT contain the null character (U+0000).
/// Every other UTF-8 topic is accepted — spaces and the discouraged but legal U+0001..U+001F control range included.
/// @details Length is deliberately not bounded here: the 65535-byte ceiling is a wire-codec concern, not a routing one.
/// @note The argument is the raw UTF-8 byte sequence; unlike Rust's `str`, `std::string_view` carries no encoding
/// guarantee, so code-point validity is assumed rather than verified.
/// @return Returns `std::nullopt` when `topic` is well formed.
[[nodiscard]]
inline std::optional<malformed> check_wellformed(std::string_view topic) {
    if (topic.empty()) { return malformed { empty_topic_error {} }; }
    if (topic.find('\0') != std::string_view::npos) { return malformed { null_char_error {} }; }
    return std::nullopt;
}

///
/// @brief Maps the shared MQTT §4.7.3 well-formedness violation onto the topic name's error type.
///
[[nodiscard]]
topic_path_error make_topic_path_error(const malformed& cause);

/// @brief Renders an error the way the Rust original's `Display` impls do.
[[nodiscard]]
std::string describe(const topic_path_error& error);

///
/// @brief `#` was used somewhere other than the last level of the filter.
///
struct hash_position_error {
    /// @brief The rejected filter.
    std::string pattern;

    [[nodiscard]]
    std::string message() const;
};

///
/// @brief `+` or `#` appeared in a position where the filter syntax does not allow it.
///
struct wildcard_usage_error {
    /// @brief The offending segment, or a description of the offending usage.
    std::string usage;

    [[nodiscard]]
    std::string message() const;
};

///
/// @brief The filter was empty, which MQTT §4.7.3 forbids.
///
struct empty_pattern_error {
    [[nodiscard]]
    std::string message() const;
};

///
/// @brief The filter contained the null character (U+0000), which MQTT §4.7.3 forbids.
///
struct null_char_pattern_error {
    [[nodiscard]]
    std::string message() const;
};

///
/// @brief A candidate filter was checked against a reference one and the wildcard structure differed.
///
struct pattern_structure_mismatch_error {
    /// @brief The pattern stored in the type.
    std::string original;
    /// @brief The candidate pattern that did not line up with it.
    std::string custom;

    [[nodiscard]]
    std::string message() const;
};

/// @brief Everything that can go wrong while building a topic filter.
using topic_pattern_error = std::variant<
    hash_position_error, wildcard_usage_error, empty_pattern_error, null_char_pattern_error,
    pattern_structure_mismatch_error>;

/// @brief Builds a `hash_position_error`.
[[nodiscard]]
topic_pattern_error make_hash_position_error(std::string pattern);

/// @brief Builds a `wildcard_usage_error`.
[[nodiscard]]
topic_pattern_error make_wildcard_usage_error(std::string usage);

/// @brief Builds a `pattern_structure_mismatch_error`.
[[nodiscard]]
topic_pattern_error make_pattern_mismatch_error(std::string original, std::string custom);

/// @brief Maps the shared MQTT §4.7.3 well-formedness violation onto the filter's error type.
[[nodiscard]]
topic_pattern_error make_topic_pattern_error(const malformed& cause);

/// @brief Renders an error the way the Rust original's `Display` impls do.
[[nodiscard]]
std::string describe(const topic_pattern_error& error);

///
/// @brief Formatting a topic for publishing was asked for on a filter containing `#`.
/// @details `#` swallows an unknown number of levels, so there is no way to know how many values to substitute for it.
///
struct hash_wildcard_not_supported_error {
    [[nodiscard]]
    std::string message() const;
};

///
/// @brief A different number of values was supplied than the filter has `+` wildcards.
///
struct parameter_count_mismatch_error {
    /// @brief How many values the filter needs.
    std::size_t expected = 0;
    /// @brief How many the caller supplied.
    std::size_t provided = 0;

    [[nodiscard]]
    std::string message() const;
};

///
/// @brief A supplied value could not be rendered into the topic text.
///
struct topic_format_error {
    [[nodiscard]]
    std::string message() const;
};

/// @brief Everything that can go wrong while formatting a topic for publishing.
using topic_format_failure
    = std::variant<hash_wildcard_not_supported_error, parameter_count_mismatch_error, topic_format_error>;

/// @brief Renders an error the way the Rust original's `Display` impls do.
[[nodiscard]]
std::string describe(const topic_format_failure& error);

///
/// @brief A topic name was matched against a filter while not well formed.
///
struct invalid_topic_error {
    topic_path_error cause;

    [[nodiscard]]
    std::string message() const {
        return describe(cause);
    }
};

///
/// @brief The filter ran out of levels before the topic name did.
///
struct unexpected_end_of_pattern_error {
    [[nodiscard]]
    std::string message() const;
};

///
/// @brief The topic name ran out of levels before the filter did.
///
struct unexpected_end_of_topic_error {
    [[nodiscard]]
    std::string message() const;
};

///
/// @brief A `#` turned up somewhere other than the last level of the filter.
///
struct unexpected_hash_segment_error {
    [[nodiscard]]
    std::string message() const;
};

///
/// @brief A literal level of the filter did not equal the corresponding level of the topic name.
///
struct segment_mismatch_error {
    /// @brief The literal the filter expected.
    std::string expected;
    /// @brief The level the topic name actually had.
    std::string found;
    /// @brief Zero-based position at which the two diverged.
    std::size_t position = 0;

    [[nodiscard]]
    std::string message() const;
};

///
/// @brief The filter named the same parameter twice.
///
struct duplicate_parameter_name_error {
    [[nodiscard]]
    std::string message() const;
};

///
/// @brief The filter's first level is an unbound wildcard and the topic name begins with `$`, which the reserved
/// `$SYS`/`$share` space forbids.
///
struct dollar_topic_exclusion_error {
    /// @brief The `$`-prefixed topic name the filter was excluded from.
    std::string topic;

    [[nodiscard]]
    std::string message() const;
};

/// @brief Everything that can go wrong while matching a topic name against a filter.
using topic_match_error = std::variant<
    invalid_topic_error, unexpected_end_of_pattern_error, unexpected_end_of_topic_error, unexpected_hash_segment_error,
    segment_mismatch_error, duplicate_parameter_name_error, dollar_topic_exclusion_error>;

/// @brief Renders an error the way the Rust original's `Display` impls do.
[[nodiscard]]
std::string describe(const topic_match_error& error);

///
/// @brief A level of a subscription pattern turned out not to be present in the tree.
///
struct invalid_segment_error {
    /// @brief The offending level.
    std::string segment;
    /// @brief Zero-based position of the level within the path.
    std::size_t position = 0;

    [[nodiscard]]
    std::string message() const;
};

/// @brief Everything that can go wrong while maintaining the tree.
using topic_matcher_error = std::variant<invalid_segment_error>;

/// @brief Builds an `invalid_segment_error`.
[[nodiscard]]
topic_matcher_error invalid_segment(std::string segment, std::size_t position);

/// @brief Renders an error the way the Rust original's `Display` impls do.
[[nodiscard]]
std::string describe(const topic_matcher_error& error);

///
/// @brief Identifies one registration in a router.
/// @details Ids are handed out in order and reused only after `cleanup`, which is enough to find a subscription again
/// when it has to be cancelled. Copyable and comparable, so it can be a map key.
///
struct subscription_id {
    /// @brief The numeric identity.
    std::size_t value = 0;

    /// @brief Renders as `SubscriptionId(<value>)`.
    [[nodiscard]]
    std::string to_string() const;

    friend constexpr bool operator==(const subscription_id&, const subscription_id&) noexcept = default;
};

///
/// @brief Hashes a `subscription_id` for use as a map key.
///
struct subscription_id_hash {
    /// @brief Hashes the numeric identity.
    [[nodiscard]]
    std::size_t operator()(const subscription_id& id) const noexcept;
};

///
/// @brief A subscription pattern was rejected while it was being registered.
///
struct invalid_pattern_error {
    /// @brief Why the pattern was rejected.
    topic_pattern_error cause;

    [[nodiscard]]
    std::string message() const;
};

///
/// @brief The matcher failed while a subscription was being removed.
///
struct matching_failed_error {
    /// @brief What the matcher objected to.
    topic_matcher_error cause;

    [[nodiscard]]
    std::string message() const;
};

///
/// @brief No subscription is registered under this id.
///
struct subscription_not_found_error {
    /// @brief The id that was looked up.
    subscription_id id;

    [[nodiscard]]
    std::string message() const;
};

///
/// @brief A topic name was rejected for a routing operation.
///
struct invalid_routing_topic_error {
    /// @brief The topic that was rejected.
    std::string topic;
    /// @brief Why it was rejected.
    std::string reason;

    [[nodiscard]]
    std::string message() const;
};

///
/// @brief The router's internal tables disagree with each other, which is a bug rather than a caller mistake.
///
struct internal_state_corrupted_error {
    /// @brief What the inconsistency was.
    std::string details;

    [[nodiscard]]
    std::string message() const;
};

/// @brief Everything that can go wrong while routing.
using router_error = std::variant<
    invalid_pattern_error, matching_failed_error, subscription_not_found_error, invalid_routing_topic_error,
    internal_state_corrupted_error>;

/// @brief Wraps a pattern error as a routing error.
[[nodiscard]]
router_error make_invalid_pattern_error(const topic_pattern_error& cause);

/// @brief Wraps a matcher error as a routing error.
[[nodiscard]]
router_error make_matching_failed_error(const topic_matcher_error& cause);

/// @brief Builds a `subscription_not_found_error`.
[[nodiscard]]
router_error subscription_not_found(subscription_id id);

/// @brief Builds an `invalid_routing_topic_error`.
[[nodiscard]]
router_error invalid_routing_topic(std::string topic, std::string reason);

/// @brief Builds an `internal_state_corrupted_error`.
[[nodiscard]]
router_error internal_state_corrupted(std::string details);

/// @brief Renders an error the way the Rust original's `Display` impls do.
[[nodiscard]]
std::string describe(const router_error& error);

///
/// @brief A topic pattern failed to parse or validate.
///
struct pattern_stage_error {
    /// @brief What the pattern parser objected to.
    topic_pattern_error cause;

    /// @brief Renders as `"Topic pattern error: <cause>"`.
    [[nodiscard]]
    std::string message() const;
};

///
/// @brief A topic failed to format for publishing.
///
struct format_stage_error {
    /// @brief What the formatter objected to.
    topic_format_failure cause;

    /// @brief Renders as `"Topic format error: <cause>"`.
    [[nodiscard]]
    std::string message() const;
};

///
/// @brief The topic matcher failed.
///
struct matcher_stage_error {
    /// @brief What the matcher objected to.
    topic_matcher_error cause;

    /// @brief Renders as `"Topic matcher error: <cause>"`.
    [[nodiscard]]
    std::string message() const;
};

///
/// @brief The topic router failed.
///
struct router_stage_error {
    /// @brief What the router objected to.
    router_error cause;

    /// @brief Renders as `"Topic router error: <cause>"`.
    [[nodiscard]]
    std::string message() const;
};

/// @brief Everything that can go wrong in a topic operation, whichever stage it failed at.
using topic_error = std::variant<pattern_stage_error, format_stage_error, matcher_stage_error, router_stage_error>;

/// @brief A topic operation's outcome.
template<typename T>
using topic_result = result<T, topic_error>;

/// @brief Renders an error the way the Rust original's `Display` impls do.
[[nodiscard]]
std::string describe(const topic_error& error);

// Each aggregate alternative is constructible from the per-module error it wraps, so a caller can return one without
// naming the wrapper. Written by hand because a variadic using-declaration does not exist.

/// @brief Wraps a pattern error.
[[nodiscard]]
inline topic_error make_topic_error(const topic_pattern_error& cause) {
    return pattern_stage_error { cause };
}

/// @brief Wraps a format error.
[[nodiscard]]
inline topic_error make_topic_error(const topic_format_failure& cause) {
    return format_stage_error { cause };
}

/// @brief Wraps a matcher error.
[[nodiscard]]
inline topic_error make_topic_error(const topic_matcher_error& cause) {
    return matcher_stage_error { cause };
}

/// @brief Wraps a router error.
[[nodiscard]]
inline topic_error make_topic_error(const router_error& cause) {
    return router_stage_error { cause };
}

} // namespace ba7lya::topic
