// SPDX-License-Identifier: MIT
//
// Fixtures for the trie suites: the payload container the tests store in a node, and the helpers
// that turn a tree into something a test can compare, which the Rust original's test module
// spelled out inline.

#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "topic_matcher.hxx"

namespace ba7lya::topic_engine::test {

/// The payload the matcher suites store: the set of subscription ids sitting on one pattern.
///
/// A set is what the Rust tests used, and it satisfies `payload_container` through `empty()`.
using ids = std::unordered_set<std::size_t>;

/// Every pattern in `root` paired with how many ids it holds, sorted for comparison.
///
/// `collect_active_subscriptions` returns them in an unspecified order — the levels live in a hash
/// map — so tests compare sorted sequences rather than raw container equality.
[[nodiscard]]
inline std::vector<std::pair<std::string, std::size_t>> active_subscriptions(const topic_matcher_node<ids>& root) {
    std::vector<std::pair<std::string, std::size_t>> active;
    for (const auto& [pattern, payload] : root.collect_active_subscriptions()) {
        active.emplace_back(pattern.to_string(), payload->size());
    }
    std::ranges::sort(active);
    return active;
}

/// Sorts the same list built from expected `(pattern, count)` pairs.
[[nodiscard]]
inline std::vector<std::pair<std::string, std::size_t>> expected_active(
    std::vector<std::pair<std::string, std::size_t>> expected
) {
    std::ranges::sort(expected);
    return expected;
}

} // namespace ba7lya::topic_engine::test
