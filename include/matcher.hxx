///
/// @file matcher.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The matching rule over the filter trie: which of the indexed filters a delivered topic name resolves
/// to.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///
// SPDX-License-Identifier: MIT

#pragma once

#include <concepts>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "error.hxx"
#include "filter.hxx"
#include "pattern.hxx"
#include "result.hxx"
#include "trie.hxx"

namespace ba7lya::topic {

/// @brief Renders a match the way the Rust original's `Display` impl does.
[[nodiscard]]
std::string to_string(const topic_match& matched);

/// @brief From the topic name's side: whether this first level puts the delivered name in
/// the reserved `$SYS`/`$share` space that a leading wildcard must not reach.
/// @details Only ever asked about level 0, which is where the exclusion applies; a `$` past the leading
/// character is an ordinary literal. The filter side of the same rule is `topic_pattern_item::excludes_dollar_topic`
/// and `topic_path::has_reserved_first_level`.
[[nodiscard]]
inline bool topic_has_reserved_first_level(std::string_view first_segment) {
    return first_segment.starts_with('$');
}

///
/// @brief The subscription index: the filter trie, plus the matching rule that walks it.
/// @details Holds a `trie` and owns no storage of its own. Insertion, pruning and enumeration are forwarded
/// straight to the trie - they are structural questions - so callers keep talking to one object; what lives here is
/// only the part that reads a delivered topic name, the reserved-`$` exclusion included.
/// @note Header-only by necessity, like the trie it wraps.
///
template<payload_container T>
class topic_matcher {
public:
    topic_matcher() = default;

    topic_matcher(const topic_matcher&) = delete;
    topic_matcher& operator=(const topic_matcher&) = delete;
    topic_matcher(topic_matcher&&) noexcept = default;
    topic_matcher& operator=(topic_matcher&&) noexcept = default;

    ~topic_matcher() = default;

    /// @brief Returns the payload table for `topic_path`, creating it and any missing levels.
    [[nodiscard]]
    T& get_or_create_subscription_table(const topic_pattern_path& topic_path) {
        return trie_storage_.get_or_create_subscription_table(topic_path);
    }

    /// @brief Applies `update` to the payload table stored at `path`, pruning what it empties.
    template<typename F>
        requires std::invocable<F&, T&>
    result<bool, topic_matcher_error> update_node(std::span<const topic_pattern_item> path, F update) {
        return trie_storage_.update_node(path, update);
    }

    /// @brief Every payload whose pattern matches `topic`.
    /// @details A delivered topic may match several patterns at once, through `+` or `#`, so the result is a
    /// list rather than a single payload.
    [[nodiscard]]
    std::vector<const T*> find_by_path(const topic_path& topic) const {
        std::vector<const T*> matching_payloads;
        collect_matching_subscriptions(trie_storage_, topic.segments(), true, matching_payloads);
        return matching_payloads;
    }

    /// @brief Every `(pattern, payload)` pair in the index; a diagnostic pass-through to the trie.
    [[nodiscard]]
    std::vector<std::pair<topic_pattern_path, const T*>> collect_active_subscriptions() const {
        return trie_storage_.collect_active_subscriptions();
    }

    /// @brief The index the matcher walks.
    /// @note Named `storage` rather than `trie`, and the member `trie_storage_` rather than `trie_`: a member
    /// function carrying the type's name would hide the type `trie` inside this class.
    [[nodiscard]]
    const trie<T>& storage() const noexcept {
        return trie_storage_;
    }

private:
    /// @brief Walks `node`, appending every payload that matches.
    /// @details `at_root` marks the first level, where a filter whose first level is a wildcard must not
    /// match a reserved `$`-prefixed topic name — the topic name knows whether it has one, and skipping both wildcard
    /// branches here leaves the literal branch above intact, so `$SYS/...` subscriptions still resolve.
    void collect_matching_subscriptions(
        const trie<T>& node, std::span<const std::string_view> topic, bool at_root,
        std::vector<const T*>& matching_payloads
    ) const {
        if (topic.empty()) {
            if (const T* payload = node.exact_match_payload(); payload != nullptr) {
                matching_payloads.push_back(payload);
            }
            if (const T* payload = node.multi_level_wildcard_payload(); payload != nullptr) {
                matching_payloads.push_back(payload);
            }
            return;
        }

        const std::string_view segment = topic.front();
        const std::span<const std::string_view> remaining = topic.subspan(1);

        if (const trie<T>* child = node.exact_child(segment); child != nullptr) {
            collect_matching_subscriptions(*child, remaining, false, matching_payloads);
        }
        if (at_root && topic_has_reserved_first_level(segment)) { return; }
        if (const trie<T>* child = node.single_level_wildcard_child(); child != nullptr) {
            collect_matching_subscriptions(*child, remaining, false, matching_payloads);
        }
        if (const T* payload = node.multi_level_wildcard_payload(); payload != nullptr) {
            matching_payloads.push_back(payload);
        }
    }

    /// @brief The filter trie this matcher walks.
    trie<T> trie_storage_;
};

} // namespace ba7lya::topic
