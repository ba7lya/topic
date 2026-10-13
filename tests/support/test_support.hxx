// SPDX-License-Identifier: MIT
//
// Fixtures shared by the suites: building a filter that is known to be valid, folding parameter
// bindings the way the Rust original's `with_parameters` extension trait did, and the
// `$`-topic-aware match predicate.

#pragma once

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "topic_engine.hxx"

namespace ba7lya::topic_engine::test {

/// Parses `text` into a filter, throwing when it is not valid.
///
/// Throwing rather than asserting keeps the helper usable in a returning position; GoogleTest
/// reports an escaping exception as a failure of the test it escaped from.
[[nodiscard]]
inline topic_pattern_path create_pattern(const std::string& text) {
    auto parsed = topic_pattern_path::parse(text);
    if (!parsed) { throw std::runtime_error("pattern should be valid: " + text + ": " + describe(parsed.error())); }
    return std::move(parsed).value();
}

/// Binds `parameters` into `pattern` in order, stopping at the first binding the filter rejects.
///
/// The stand-in for the Rust original's `with_parameters` extension trait, which the tests here
/// call on a plain function instead.
[[nodiscard]]
inline pattern_result<topic_pattern_path> with_parameters(
    topic_pattern_path pattern, const std::vector<std::pair<std::string, std::string>>& parameters
) {
    topic_pattern_path current = std::move(pattern);
    for (const auto& [name, value] : parameters) {
        auto bound = current.bind_parameter(name, value);
        if (!bound) { return bound.error(); }
        current = std::move(bound).value();
    }
    return current;
}

/// Whether `pattern` matches `topic`, treating the MQTT §4.7.2 `$`-topic exclusion as "no match".
///
/// Any other failure is a bug in the test rather than a legitimate outcome, so it throws — as the
/// Rust original's `panic!` did.
[[nodiscard]]
inline bool matches(const std::string& pattern, const std::string& topic) {
    auto matched = create_pattern(pattern).match_str(topic);
    if (matched) { return true; }
    if (std::holds_alternative<dollar_topic_exclusion_error>(matched.error())) { return false; }
    throw std::runtime_error(
        "unexpected match error for " + pattern + " vs " + topic + ": " + describe(matched.error())
    );
}

} // namespace ba7lya::topic_engine::test
