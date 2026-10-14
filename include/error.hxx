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
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "result.hxx"

namespace ba7lya::topic {

///
/// @brief The base every concrete error derives from.
/// @details It renders nothing itself: each concrete error builds its message in its constructor and hands it
/// up, so `what()` is the single place a caller has to look, and an error can travel either returned inside a
/// `result` or thrown - it is a `std::exception` either way.
/// @note The message is stored rather than computed on demand because `what()` must hand out a `const char*`
/// that outlives the call.
///
class error_base : public std::exception {
public:
    /// @brief The message this error was built with.
    /// @return A NUL-terminated rendering of the error.
    [[nodiscard]]
    const char* what() const noexcept override {
        return message_.c_str();
    }

protected:
    error_base() = default;

    explicit error_base(std::string message)
        : message_(std::move(message)) {}

private:
    /// @brief The rendered message.
    std::string message_;
};

///
/// @brief The topic name handed to `topic_path::parse` was empty (MQTT §4.7.3).
///
struct empty_topic_path_error : error_base {
    empty_topic_path_error()
        : error_base("Topic cannot be empty") {}
};

///
/// @brief The topic name contained the null character (U+0000), which MQTT §4.7.3 forbids.
///
struct null_char_topic_path_error : error_base {
    null_char_topic_path_error()
        : error_base("Topic must not contain the null character (U+0000)") {}
};

///
/// @brief Everything that can go wrong while constructing a topic name.
///
using topic_path_error = std::variant<
    empty_topic_path_error, //
    null_char_topic_path_error>;

/// @brief Renders an error the way the Rust original's `Display` impls do.
[[nodiscard]]
inline std::string describe(const topic_path_error& error) {
    return std::visit([](const auto& concrete) { return std::string { concrete.what() }; }, error);
}

///
/// @brief MQTT §4.7.3 violation: the topic string must have at least one character.
///
struct empty_topic_error : error_base {
    empty_topic_error()
        : error_base("Topic cannot be empty") {}
};

///
/// @brief MQTT §4.7.3 violation: the topic string must not contain the null character (U+0000).
///
struct null_char_error : error_base {
    null_char_error()
        : error_base("Topic must not contain the null character (U+0000)") {}
};

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

/// @brief Maps the shared MQTT §4.7.3 well-formedness violation onto the topic name's error type.
/// @details The two public error types differ only in their message, so the mapping is a plain switch over `cause`.
[[nodiscard]]
inline topic_path_error classify_topic_path_error(const malformed& cause) {
    if (std::holds_alternative<null_char_error>(cause)) { return null_char_topic_path_error {}; }
    return empty_topic_path_error {};
}

///
/// @brief `#` was used somewhere other than the last level of the filter.
///
struct hash_position_error : error_base {
    /// @brief Builds the error for `pattern_`.
    explicit hash_position_error(std::string pattern_)
        : error_base("Invalid topic pattern '" + pattern_ + "': # wildcard can only be the last segment")
        , pattern(std::move(pattern_)) {}

    /// @brief The rejected filter.
    std::string pattern;
};

///
/// @brief `+` or `#` appeared in a position where the filter syntax does not allow it.
///
struct wildcard_usage_error : error_base {
    /// @brief Builds the error for `usage_`.
    explicit wildcard_usage_error(std::string usage_)
        : error_base("Invalid wildcard usage: " + usage_)
        , usage(std::move(usage_)) {}

    /// @brief The offending segment, or a description of the offending usage.
    std::string usage;
};

///
/// @brief The filter was empty, which MQTT §4.7.3 forbids.
///
struct empty_pattern_error : error_base {
    empty_pattern_error()
        : error_base("Topic pattern cannot be empty") {}
};

///
/// @brief The filter contained the null character (U+0000), which MQTT §4.7.3 forbids.
///
struct null_char_pattern_error : error_base {
    null_char_pattern_error()
        : error_base("Topic pattern must not contain the null character (U+0000)") {}
};

///
/// @brief A candidate filter was checked against a reference one and the wildcard structure differed.
///
struct pattern_structure_mismatch_error : error_base {
    /// @brief Builds the error for `original_` against `custom_`.
    pattern_structure_mismatch_error(std::string original_, std::string custom_)
        : error_base(
              "Topic pattern structure mismatch.\nOriginal: '" + original_ + "'\nCustom:   '" + custom_
              + "'\nHint: Both patterns must have the same parameter structure (same wildcards in same positions)"
          )
        , original(std::move(original_))
        , custom(std::move(custom_)) {}

    /// @brief The pattern stored in the type.
    std::string original;
    /// @brief The candidate pattern that did not line up with it.
    std::string custom;
};

///
/// @brief Everything that can go wrong while building a topic filter.
///
using topic_pattern_error = std::variant<
    hash_position_error,     //
    wildcard_usage_error,    //
    empty_pattern_error,     //
    null_char_pattern_error, //
    pattern_structure_mismatch_error>;

///
/// @brief Renders an error the way the Rust original's `Display` impls do.
///
[[nodiscard]]
inline std::string describe(const topic_pattern_error& error) {
    return std::visit([](const auto& concrete) { return std::string { concrete.what() }; }, error);
}

///
/// @brief Maps the shared MQTT §4.7.3 well-formedness violation onto the filter's error type.
/// @details The two public error types differ only in their message, so the mapping is a plain switch over `cause`.
///
[[nodiscard]]
inline topic_pattern_error classify_topic_pattern_error(const malformed& cause) {
    if (std::holds_alternative<null_char_error>(cause)) { return null_char_pattern_error {}; }
    return empty_pattern_error {};
}

///
/// @brief Formatting a topic for publishing was asked for on a filter containing `#`.
/// @details `#` swallows an unknown number of levels, so there is no way to know how many values to substitute for it.
///
struct hash_wildcard_not_supported_error : error_base {
    /// @brief Builds the error with its message.
    hash_wildcard_not_supported_error()
        : error_base("Cannot format topic with # wildcard for publishing") {}
};

///
/// @brief A different number of values was supplied than the filter has `+` wildcards.
///
struct param_count_mismatch_error : error_base {
    /// @brief How many values the filter needs.
    std::size_t expected = 0;
    /// @brief How many the caller supplied.
    std::size_t provided = 0;

    /// @brief Builds the error for `expected_` against `provided_`.
    param_count_mismatch_error(std::size_t expected_, std::size_t provided_)
        : error_base(
              "Parameter count mismatch: expected " + std::to_string(expected_) + ", provided "
              + std::to_string(provided_)
          )
        , expected(expected_)
        , provided(provided_) {}
};

///
/// @brief A supplied value could not be rendered into the topic text.
///
struct topic_format_error : error_base {
    /// @brief Builds the error with its message.
    topic_format_error()
        : error_base("Error formatting topic") {}
};

/// @brief Everything that can go wrong while formatting a topic for publishing.
using topic_format_failure
    = std::variant<hash_wildcard_not_supported_error, param_count_mismatch_error, topic_format_error>;

/// @brief Renders an error the way the Rust original's `Display` impls do.
[[nodiscard]]
inline std::string describe(const topic_format_failure& error) {
    return std::visit([](const auto& concrete) { return std::string { concrete.what() }; }, error);
}

///
/// @brief A topic name was matched against a filter while not well formed.
///
struct invalid_topic_error : error_base {
    /// @brief Builds the error around the topic name's own failure.
    explicit invalid_topic_error(topic_path_error cause_)
        : error_base(describe(cause_))
        , cause(std::move(cause_)) {}

    /// @brief Why the topic name was rejected.
    topic_path_error cause;
};

///
/// @brief The filter ran out of levels before the topic name did.
///
struct unexpected_end_of_pattern_error : error_base {
    /// @brief Builds the error with its message.
    unexpected_end_of_pattern_error()
        : error_base("Pattern ended unexpectedly while matching topic") {}
};

///
/// @brief The topic name ran out of levels before the filter did.
///
struct unexpected_end_of_topic_error : error_base {
    /// @brief Builds the error with its message.
    unexpected_end_of_topic_error()
        : error_base("Topic ended unexpectedly while matching pattern") {}
};

///
/// @brief A `#` turned up somewhere other than the last level of the filter.
///
struct unexpected_hash_segment_error : error_base {
    /// @brief Builds the error with its message.
    unexpected_hash_segment_error()
        : error_base("Hash wildcard (#) found in unexpected position") {}
};

///
/// @brief A literal level of the filter did not equal the corresponding level of the topic name.
///
struct segment_mismatch_error : error_base {
    /// @brief Builds the error for the level at `position_`.
    segment_mismatch_error(std::string expected_, std::string found_, std::size_t position_)
        : error_base(
              "Segment mismatch at position " + std::to_string(position_) + ": expected '" + expected_ + "', found '"
              + found_ + "'"
          )
        , expected(std::move(expected_))
        , found(std::move(found_))
        , position(position_) {}

    /// @brief The literal the filter expected.
    std::string expected;
    /// @brief The level the topic name actually had.
    std::string found;
    /// @brief Zero-based position at which the two diverged.
    std::size_t position = 0;
};

///
/// @brief The filter named the same parameter twice.
///
struct duplicate_param_name_error : error_base {
    /// @brief Builds the error with its message.
    duplicate_param_name_error()
        : error_base("Duplicate parameter name found in pattern") {}
};

///
/// @brief The filter's first level is an unbound wildcard and the topic name begins with `$`, which the reserved
/// `$SYS`/`$share` space forbids.
///
struct dollar_topic_exclusion_error : error_base {
    /// @brief Builds the error for `topic_`.
    explicit dollar_topic_exclusion_error(std::string topic_)
        : error_base(
              "Topic filter with a leading wildcard cannot match reserved $-topic '" + topic_ + "' (MQTT §4.7.2)"
          )
        , topic(std::move(topic_)) {}

    /// @brief The `$`-prefixed topic name the filter was excluded from.
    std::string topic;
};

/// @brief Everything that can go wrong while matching a topic name against a filter.
using topic_match_error = std::variant<
    invalid_topic_error,             //
    unexpected_end_of_pattern_error, //
    unexpected_end_of_topic_error,   //
    unexpected_hash_segment_error,   //
    segment_mismatch_error,          //
    duplicate_param_name_error,      //
    dollar_topic_exclusion_error>;

/// @brief Renders an error the way the Rust original's `Display` impls do.
[[nodiscard]]
inline std::string describe(const topic_match_error& error) {
    return std::visit([](const auto& concrete) { return std::string { concrete.what() }; }, error);
}

///
/// @brief A level of a subscription pattern turned out not to be present in the tree.
///
struct invalid_segment_error : error_base {
    /// @brief Builds the error for `segment_` at `position_`.
    invalid_segment_error(std::string segment_, std::size_t position_)
        : error_base("Invalid topic segment '" + segment_ + "' at position " + std::to_string(position_))
        , segment(std::move(segment_))
        , position(position_) {}

    /// @brief The offending level.
    std::string segment;
    /// @brief Zero-based position of the level within the path.
    std::size_t position = 0;
};

/// @brief Everything that can go wrong while maintaining the tree.
using topic_matcher_error = std::variant<invalid_segment_error>;

/// @brief Renders an error the way the Rust original's `Display` impls do.
[[nodiscard]]
inline std::string describe(const topic_matcher_error& error) {
    return std::visit([](const auto& concrete) { return std::string { concrete.what() }; }, error);
}

/// @brief Builds an `invalid_segment_error`.
[[nodiscard]]
topic_matcher_error invalid_segment(std::string segment, std::size_t position);

///
/// @brief Identifies one registration in a router.
/// @details Ids are handed out in order and reused only after `cleanup`, which is enough to find a subscription again
/// when it has to be cancelled. Copyable and comparable, so it can be a map key.
///
struct subscription_id {
    /// @brief Renders as `SubscriptionId(<value>)`.
    [[nodiscard]]
    std::string to_string() const {
        return "SubscriptionId(" + std::to_string(value) + ")";
    }

    friend constexpr bool operator==(const subscription_id&, const subscription_id&) noexcept = default;

    /// @brief The numeric identity.
    std::size_t value = 0;
};

///
/// @brief Hashes a `subscription_id` for use as a map key.
///
template<>
struct std::hash<subscription_id> {
    [[nodiscard]]
    std::size_t operator()(const subscription_id& id) const noexcept {
        return std::hash<std::size_t> {}(id.value);
    }
};

///
/// @brief A subscription pattern was rejected while it was being registered.
///
struct invalid_pattern_error : error_base {
    /// @brief Builds the error around the pattern's own failure.
    explicit invalid_pattern_error(topic_pattern_error cause_)
        : error_base("Invalid topic pattern: " + describe(cause_))
        , cause(std::move(cause_)) {}

    /// @brief Why the pattern was rejected.
    topic_pattern_error cause;
};

///
/// @brief The matcher failed while a subscription was being removed.
///
struct matching_failed_error : error_base {
    /// @brief Builds the error around the matcher's own failure.
    explicit matching_failed_error(topic_matcher_error cause_)
        : error_base("Topic matching failed: " + describe(cause_))
        , cause(std::move(cause_)) {}

    /// @brief What the matcher objected to.
    topic_matcher_error cause;
};

///
/// @brief No subscription is registered under this id.
///
struct subscription_not_found_error : error_base {
    /// @brief Builds the error for `id_`.
    explicit subscription_not_found_error(subscription_id id_)
        : error_base("Subscription " + id_.to_string() + " not found")
        , id(id_) {}

    /// @brief The id that was looked up.
    subscription_id id;
};

///
/// @brief A topic name was rejected for a routing operation.
///
struct invalid_routing_topic_error : error_base {
    /// @brief Builds the error for `topic_` with `reason_`.
    invalid_routing_topic_error(std::string topic_, std::string reason_)
        : error_base("Topic '" + topic_ + "' is invalid for routing: " + reason_)
        , topic(std::move(topic_))
        , reason(std::move(reason_)) {}

    /// @brief The topic that was rejected.
    std::string topic;
    /// @brief Why it was rejected.
    std::string reason;
};

///
/// @brief The router's internal tables disagree with each other, which is a bug rather than a caller mistake.
///
struct internal_state_corrupted_error : error_base {
    /// @brief Builds the error for `details_`.
    explicit internal_state_corrupted_error(std::string details_)
        : error_base("Internal routing state corrupted: " + details_)
        , details(std::move(details_)) {}

    /// @brief What the inconsistency was.
    std::string details;
};

///
/// @brief Everything that can go wrong while routing.
///
using router_error = std::variant<
    invalid_pattern_error,        //
    matching_failed_error,        //
    subscription_not_found_error, //
    invalid_routing_topic_error,  //
    internal_state_corrupted_error>;

///
/// @brief Renders an error the way the Rust original's `Display` impls do.
///
[[nodiscard]]
inline std::string describe(const router_error& error) {
    return std::visit([](const auto& concrete) { return std::string { concrete.what() }; }, error);
}

/// @brief Builds a `subscription_not_found_error`.
[[nodiscard]]
router_error subscription_not_found(subscription_id id);

/// @brief Builds an `invalid_routing_topic_error`.
[[nodiscard]]
router_error invalid_routing_topic(std::string topic, std::string reason);

/// @brief Builds an `internal_state_corrupted_error`.
[[nodiscard]]
router_error internal_state_corrupted(std::string details);

///
/// @brief A topic pattern failed to parse or validate.
///
struct pattern_stage_error : error_base {
    /// @brief Builds the error around the pattern's own failure; renders as `"Topic pattern error: <cause>"`.
    explicit pattern_stage_error(topic_pattern_error cause_)
        : error_base("Topic pattern error: " + describe(cause_))
        , cause(std::move(cause_)) {}

    /// @brief What the pattern parser objected to.
    topic_pattern_error cause;
};

///
/// @brief A topic failed to format for publishing.
///
struct format_stage_error : error_base {
    /// @brief Builds the error around the formatter's own failure; renders as `"Topic format error: <cause>"`.
    explicit format_stage_error(topic_format_failure cause_)
        : error_base("Topic format error: " + describe(cause_))
        , cause(std::move(cause_)) {}

    /// @brief What the formatter objected to.
    topic_format_failure cause;
};

///
/// @brief The topic matcher failed.
///
struct matcher_stage_error : error_base {
    /// @brief Builds the error around the matcher's own failure; renders as `"Topic matcher error: <cause>"`.
    explicit matcher_stage_error(topic_matcher_error cause_)
        : error_base("Topic matcher error: " + describe(cause_))
        , cause(std::move(cause_)) {}

    /// @brief What the matcher objected to.
    topic_matcher_error cause;
};

///
/// @brief The topic router failed.
///
struct router_stage_error : error_base {
    /// @brief Builds the error around the router's own failure; renders as `"Topic router error: <cause>"`.
    explicit router_stage_error(router_error cause_)
        : error_base("Topic router error: " + describe(cause_))
        , cause(std::move(cause_)) {}

    /// @brief What the router objected to.
    router_error cause;
};

/// @brief Everything that can go wrong in a topic operation, whichever stage it failed at.
using topic_error = std::variant<pattern_stage_error, format_stage_error, matcher_stage_error, router_stage_error>;

/// @brief A topic operation's outcome.
template<typename T>
using topic_result = result<T, topic_error>;

/// @brief Renders an error the way the Rust original's `Display` impls do.
[[nodiscard]]
inline std::string describe(const topic_error& error) {
    return std::visit([](const auto& concrete) { return std::string { concrete.what() }; }, error);
}

} // namespace ba7lya::topic
