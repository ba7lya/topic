// SPDX-License-Identifier: MIT
//
// Concise MQTT topic names and the result of matching one against a filter.

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "result.hxx"
#include "topic_wellformedness.hxx"

namespace ba7lya::topic_engine {

/// The topic name handed to `topic_path::parse` was empty (MQTT §4.7.3).
struct empty_topic_path_error {
    [[nodiscard]]
    std::string message() const;
};

/// The topic name contained the null character (U+0000), which MQTT §4.7.3 forbids.
struct null_char_topic_path_error {
    [[nodiscard]]
    std::string message() const;
};

/// Everything that can go wrong while constructing a topic name.
using topic_path_error = std::variant<empty_topic_path_error, null_char_topic_path_error>;

/// The outcome of building a topic name.
using topic_path_result = result<std::shared_ptr<const class topic_path>, topic_path_error>;

/// Maps the shared MQTT §4.7.3 well-formedness violation onto the topic name's error type.
[[nodiscard]]
topic_path_error make_topic_path_error(const malformed& cause);

/// Renders an error the way the Rust original's `Display` impls do.
[[nodiscard]]
std::string describe(const topic_path_error& error);

/// A concrete MQTT topic name, split into its `/`-delimited levels.
///
/// Construction validates MQTT §4.7.3 well-formedness and splits once; the full text and the
/// level slices then share this single object, so handing the same `topic_path` to several
/// filters costs nothing. Build it through `parse` and carry it in a `std::shared_ptr`.
class topic_path {
public:
    /// Validates and splits `path`.
    ///
    /// Rejects an empty string and any string containing U+0000. Every other UTF-8 topic is
    /// accepted, spaces and the discouraged-but-legal U+0001..U+001F range included; the
    /// 65535-byte ceiling is a wire-codec concern and is not enforced here.
    [[nodiscard]]
    static topic_path_result parse(std::string path);

    /// The full topic name.
    [[nodiscard]]
    const std::string& text() const noexcept {
        return path_;
    }

    /// The levels, in order. `a/b/c` yields three; `a//b` yields an empty one in the middle.
    [[nodiscard]]
    const std::vector<std::string_view>& segments() const noexcept {
        return segments_;
    }

    /// The number of levels.
    [[nodiscard]]
    std::size_t segment_count() const noexcept {
        return segments_.size();
    }

private:
    /// Splits `path` on `/`; the caller has already established well-formedness.
    explicit topic_path(std::string path);

    /// The full topic name; owns the storage the views in `segments_` point into.
    std::string path_;
    /// Views into `path_`, one per `/`-delimited level.
    std::vector<std::string_view> segments_;
};

/// A topic name was matched against a filter while not well formed.
struct invalid_topic_error {
    topic_path_error cause;

    [[nodiscard]]
    std::string message() const {
        return describe(cause);
    }
};

/// The filter ran out of levels before the topic name did.
struct unexpected_end_of_pattern_error {
    [[nodiscard]]
    std::string message() const;
};

/// The topic name ran out of levels before the filter did.
struct unexpected_end_of_topic_error {
    [[nodiscard]]
    std::string message() const;
};

/// A `#` turned up somewhere other than the last level of the filter.
struct unexpected_hash_segment_error {
    [[nodiscard]]
    std::string message() const;
};

/// A literal level of the filter did not equal the corresponding level of the topic name.
struct segment_mismatch_error {
    /// The literal the filter expected.
    std::string expected;
    /// The level the topic name actually had.
    std::string found;
    /// Zero-based position at which the two diverged.
    std::size_t position = 0;

    [[nodiscard]]
    std::string message() const;
};

/// The filter named the same parameter twice.
struct duplicate_parameter_name_error {
    [[nodiscard]]
    std::string message() const;
};

/// MQTT §4.7.2: the filter's first level is an unbound wildcard and the topic name begins with
/// `$`, which the reserved `$SYS`/`$share` space forbids.
struct dollar_topic_exclusion_error {
    /// The `$`-prefixed topic name the filter was excluded from.
    std::string topic;

    [[nodiscard]]
    std::string message() const;
};

/// Everything that can go wrong while matching a topic name against a filter.
using topic_match_error = std::variant<
    invalid_topic_error, unexpected_end_of_pattern_error, unexpected_end_of_topic_error, unexpected_hash_segment_error,
    segment_mismatch_error, duplicate_parameter_name_error, dollar_topic_exclusion_error>;

/// Renders an error the way the Rust original's `Display` impls do.
[[nodiscard]]
std::string describe(const topic_match_error& error);

/// The result of matching a `topic_path` against a filter.
///
/// Holds the matched topic name plus the level ranges its wildcards captured, reachable by
/// position (`param`) or by name (`named_param`). A `#` capture spans every remaining level and
/// renders as their `/`-joined text.
///
/// Copying is cheap: a `shared_ptr` bump plus one small vector.
class topic_match {
public:
    /// Records a match. Used by the filter's matcher.
    topic_match(
        std::shared_ptr<const topic_path> topic, std::vector<std::pair<std::size_t, std::size_t>> param_ranges,
        std::vector<std::pair<std::string, std::pair<std::size_t, std::size_t>>> named_param_ranges
    )
        : topic_(std::move(topic))
        , param_ranges_(std::move(param_ranges))
        , named_param_ranges_(std::move(named_param_ranges)) {}

    /// The matched topic name's levels.
    [[nodiscard]]
    const std::vector<std::string_view>& path_segments() const {
        return topic_->segments();
    }

    /// The positional parameter captured at `index`, in filter order; empty when out of range.
    [[nodiscard]]
    std::string_view param(std::size_t index) const;

    /// The value captured by the named parameter `name`, if the filter has one.
    [[nodiscard]]
    std::optional<std::string_view> named_param(std::string_view name) const;

    /// The full matched topic name.
    [[nodiscard]]
    const std::string& topic_name() const {
        return topic_->text();
    }

    /// How many positional parameters were captured.
    [[nodiscard]]
    std::size_t param_count() const noexcept {
        return param_ranges_.size();
    }

    /// Renders as `Match(<topic>)`, plus ` with N params` when any were captured.
    [[nodiscard]]
    std::string to_string() const;

private:
    /// Resolves a span of levels to the text it covers, `path_[start] / ... / path_[end - 1]`.
    ///
    /// An empty span resolves to the empty string; a single level resolves to that level. Both
    /// fall out of the arithmetic below, since an empty span starts and ends at the same offset.
    [[nodiscard]]
    std::string_view range_text(std::size_t start, std::size_t end) const;

    /// The matched topic name.
    std::shared_ptr<const topic_path> topic_;
    /// Spans of levels captured by positional wildcards, in filter order.
    std::vector<std::pair<std::size_t, std::size_t>> param_ranges_;
    /// Spans of levels captured by named wildcards, paired with the parameter name.
    std::vector<std::pair<std::string, std::pair<std::size_t, std::size_t>>> named_param_ranges_;
};

/// Renders a match the way the Rust original's `Display` impl does.
[[nodiscard]]
std::string to_string(const topic_match& matched);

} // namespace ba7lya::topic_engine
