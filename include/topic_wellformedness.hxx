// SPDX-License-Identifier: MIT
//
// The MQTT topic-name/topic-filter rules that the whole engine shares: the §4.7.3
// well-formedness kernel and the §4.7.2 `$`-topic predicate.

#pragma once

#include <optional>
#include <string_view>
#include <variant>

namespace ba7lya::topic_engine {

/// MQTT §4.7.3 violation: the topic string must have at least one character.
struct empty_topic_error {};

/// MQTT §4.7.3 violation: the topic string must not contain the null character (U+0000).
struct null_char_error {};

/// A topic-string well-formedness violation.
///
/// Both the topic-name (`topic_path`) and the topic-filter (`topic_pattern_path`) constructors
/// check the same rule and each maps this onto its own public error type.
using malformed = std::variant<empty_topic_error, null_char_error>;

/// MQTT §4.7.3: a topic name or filter MUST be non-empty and MUST NOT contain the null
/// character (U+0000). Every other UTF-8 topic is accepted — spaces and the discouraged but
/// legal U+0001..U+001F control range included.
///
/// Length is deliberately not bounded here: the 65535-byte ceiling is a wire-codec concern, not
/// a routing one.
///
/// The argument is the raw UTF-8 byte sequence; unlike Rust's `str`, `std::string_view` carries
/// no encoding guarantee, so code-point validity is assumed rather than verified.
///
/// Returns `std::nullopt` when `topic` is well formed.
[[nodiscard]]
inline std::optional<malformed> check_wellformed(std::string_view topic) {
    if (topic.empty()) { return malformed { empty_topic_error {} }; }
    if (topic.find('\0') != std::string_view::npos) { return malformed { null_char_error {} }; }
    return std::nullopt;
}

/// MQTT §4.7.2: a topic filter whose first level is a wildcard must not match a topic name
/// whose first level begins with `$` (the reserved `$SYS`/`$share` space).
///
/// The exclusion is first-level-only — a `$` anywhere but the leading character is an ordinary
/// literal — so this tests just the first segment. The one predicate is shared by the trie and
/// the pattern matcher so the two can never disagree.
[[nodiscard]]
inline bool is_dollar_topic(std::string_view first_segment) {
    return first_segment.starts_with('$');
}

} // namespace ba7lya::topic_engine
