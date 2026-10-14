///
/// @file test_support.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Fixtures shared by the suites: building a filter that is known to be valid, folding parameter bindings, and
/// the `$`-topic-aware match predicate.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///
// SPDX-License-Identifier: MIT

#pragma once

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "topic.hxx"

namespace ba7lya::topic::test {

/// @brief Parses `text` into a filter, throwing when it is not valid.
/// @details Throwing rather than asserting keeps the helper usable in a returning position; GoogleTest
/// reports an escaping exception as a failure of the test it escaped from.
[[nodiscard]]
inline topic_pattern_path create_pattern(const std::string& text) {
    auto parsed = topic_pattern_path::parse(text);
    if (!parsed) { throw std::runtime_error("pattern should be valid: " + text + ": " + describe(parsed.error())); }
    return std::move(parsed).value();
}

/// @brief Binds `params` into `pattern` in order, stopping at the first binding the filter rejects.
/// @details The stand-in for the Rust original's `with_params` extension trait, which the tests here
/// call on a plain function instead.
[[nodiscard]]
inline pattern_result<topic_pattern_path> with_params(
    topic_pattern_path pattern, const std::vector<std::pair<std::string, std::string>>& params
) {
    topic_pattern_path current = std::move(pattern);
    for (const auto& [name, value] : params) {
        auto bound = current.bind_param(name, value);
        if (!bound) { return bound.error(); }
        current = std::move(bound).value();
    }
    return current;
}

/// @brief Whether `pattern` matches `topic`, treating the MQTT §4.7.2 `$`-topic exclusion as "no match".
/// @details Any other failure is a bug in the test rather than a legitimate outcome, so it throws — as the
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

} // namespace ba7lya::topic::test
