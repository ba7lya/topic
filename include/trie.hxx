///
/// @file trie.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The prefix tree that indexes topic filters: one node per filter level, and the - structural - algorithms
/// that insert into it, prune it and walk it.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///
// SPDX-License-Identifier: MIT

#pragma once

#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "error.hxx"
#include "filter.hxx"
#include "pattern.hxx"
#include "result.hxx"

namespace ba7lya::topic {

/// @brief What a node may hold as a payload: a container that can say whether it is empty.
/// @details The Rust original spells this as the `Len` trait, implemented for `HashSet` and `HashMap`. A
/// concept does the same job here without type erasure, so the payload type stays fully visible to the optimizer.
template<typename T>
concept payload_container = std::default_initializable<T> && requires(const T& container) {
    { container.empty() } -> std::convertible_to<bool>;
};

///
/// @brief Hashes a key the way `std::hash<std::string_view>` would, so a `std::string_view` level can be
/// looked up in a `std::string`-keyed map without building a temporary string.
/// @details Matching runs once per delivered message per subscription, so the allocation a converting lookup would cost
/// is worth designing out. Both overloads agree on the value of a key, which is what makes the heterogeneous lookup
/// sound.
///
struct transparent_string_hash {
    /// @brief Marks the hash as usable for heterogeneous lookup.
    using is_transparent = void;

    /// @brief Hashes a view of the key.
    [[nodiscard]]
    std::size_t operator()(std::string_view text) const noexcept {
        return std::hash<std::string_view> {}(text);
    }

    /// @brief Hashes an owning key; identical to the view overload for the same characters.
    [[nodiscard]]
    std::size_t operator()(const std::string& text) const noexcept {
        return std::hash<std::string_view> {}(text);
    }
};

///
/// @brief One node of the topic trie, standing for a level of a filter's pattern.
/// @details Four slots, one per way a filter can continue from here: a literal level's payload, the
/// literal levels' subtrees, the `+` wildcard's subtree, and the `#` wildcard's payload. A `#` is always the last level
/// of a filter, which is why it needs no subtree.
/// @details The class knows nothing of matching: it stores filters and hands back the payloads it holds. Which of them
/// a delivered topic should reach is the matcher's question, and it walks the trie through the four read-only accessors
/// below.
/// @note Header-only by necessity: the payload type is a template parameter, so no translation unit can instantiate the
/// members for it.
///
template<payload_container T>
class trie {
public:
    trie() = default;

    trie(const trie&) = delete;
    trie& operator=(const trie&) = delete;
    trie(trie&&) noexcept = default;
    trie& operator=(trie&&) noexcept = default;

    ~trie() = default;

    /// @brief Whether this node holds no payload and has no children, so its parent may drop it.
    [[nodiscard]]
    bool is_empty() const {
        return (!exact_match_data_.has_value() || exact_match_data_->empty()) && exact_children_.empty()
            && !single_level_wildcard_node_
            && (!multi_level_wildcard_data_.has_value() || multi_level_wildcard_data_->empty());
    }

    /// @brief Returns the payload table for `topic_path`, creating it and any missing levels.
    /// @details The returned reference stays valid until the node's subtree is modified: children live in
    /// `std::unique_ptr`s, so creating a sibling table cannot move this one.
    [[nodiscard]]
    T& get_or_create_subscription_table(const topic_pattern_path& topic_path) {
        trie* current = this;
        for (const topic_pattern_item& segment : topic_path.resolve_bound_segments()) {
            if (segment.is_literal()) {
                current = &current->exact_child_or_create(segment.literal());
                continue;
            }
            if (segment.is_plus()) {
                if (!current->single_level_wildcard_node_) {
                    current->single_level_wildcard_node_ = std::make_unique<trie>();
                }
                current = current->single_level_wildcard_node_.get();
                continue;
            }
            // A `#` is the last level by construction, so its payload belongs to this node and
            // the walk ends here.
            if (!current->multi_level_wildcard_data_.has_value()) { current->multi_level_wildcard_data_.emplace(); }
            return *current->multi_level_wildcard_data_;
        }

        if (!current->exact_match_data_.has_value()) { current->exact_match_data_.emplace(); }
        return *current->exact_match_data_;
    }

    /// @brief Applies `update` to the payload table stored at `path`, pruning what it empties.
    /// @details Returns whether the payload table at `path` came out empty — the question a router has to ask before it
    /// tells a broker to drop the wire subscription, since two distinct patterns may share one node. The node a table
    /// is dropped from is a separate matter: a callee prunes under itself whenever the caller reports the table gone,
    /// and this node then drops the branch only if that left it with nothing at all.
    /// @note A level of `path` that the tree does not have is an `invalid_segment_error`.
    template<typename F>
        requires std::invocable<F&, T&>
    result<bool, topic_matcher_error> update_node(std::span<const topic_pattern_item> path, F update) {
        if (path.empty()) {
            if (!exact_match_data_.has_value()) { return invalid_segment("no_data_for_empty_path", 0); }
            update(*exact_match_data_);
            if (!exact_match_data_->empty()) { return false; }
            exact_match_data_.reset();
            return true;
        }

        const topic_pattern_item& current_segment = path.front();
        const std::span<const topic_pattern_item> rest = path.subspan(1);

        if (current_segment.is_literal()) {
            const auto child = exact_children_.find(current_segment.literal());
            if (child == exact_children_.end()) { return invalid_segment(current_segment.literal(), 0); }
            const auto pruned = child->second->update_node(rest, update);
            if (!pruned) { return pruned.error(); }
            // The level's table is gone, so drop the level itself — but only if the subtree below it
            // has nothing left either, since a `#` payload or a sibling wildcard may still live on.
            if (pruned.value() && child->second->is_empty()) { exact_children_.erase(child); }
            return pruned.value();
        }

        if (current_segment.is_plus()) {
            if (!single_level_wildcard_node_) { return invalid_segment("+", 0); }
            const auto pruned = single_level_wildcard_node_->update_node(rest, update);
            if (!pruned) { return pruned.error(); }
            if (pruned.value() && single_level_wildcard_node_->is_empty()) { single_level_wildcard_node_.reset(); }
            return pruned.value();
        }

        if (!multi_level_wildcard_data_.has_value()) { return invalid_segment("#", 0); }
        update(*multi_level_wildcard_data_);
        if (!multi_level_wildcard_data_->empty()) { return false; }
        multi_level_wildcard_data_.reset();
        return true;
    }

    /// @brief Every `(pattern, payload)` pair in the tree, by walking every branch.
    /// @details A diagnostic helper for asserting what a router actually holds; production code that wants
    /// the active subscriptions should ask the router, which keeps a flat table and is cheaper. The order is
    /// unspecified — the levels live in a hash map — so compare the result as a set.
    [[nodiscard]]
    std::vector<std::pair<topic_pattern_path, const T*>> collect_active_subscriptions() const {
        std::vector<std::pair<topic_pattern_path, const T*>> collected;
        std::vector<topic_pattern_item> current_path;
        collect_active_subscriptions_internal(current_path, collected);
        return collected;
    }

    /// @brief The payload of a pattern that ends at this node, if any.
    [[nodiscard]]
    const T* exact_match_payload() const noexcept {
        return exact_match_data_.has_value() ? &*exact_match_data_ : nullptr;
    }

    /// @brief The payload of a pattern that ends in `#` at this node, if any.
    [[nodiscard]]
    const T* multi_level_wildcard_payload() const noexcept {
        return multi_level_wildcard_data_.has_value() ? &*multi_level_wildcard_data_ : nullptr;
    }

    /// @brief The subtree for a literal level, or null when the level is absent.
    /// @note Not `noexcept`: the lookup hashes the key.
    [[nodiscard]]
    const trie* exact_child(std::string_view level) const {
        const auto child = exact_children_.find(level);
        return child == exact_children_.end() ? nullptr : child->second.get();
    }

    /// @brief The subtree for a `+` level, or null when the level is absent.
    [[nodiscard]]
    const trie* single_level_wildcard_child() const noexcept {
        return single_level_wildcard_node_.get();
    }

private:
    /// @brief Returns the subtree for a literal level, creating it if absent.
    [[nodiscard]]
    trie& exact_child_or_create(const std::string& segment) {
        auto& child = exact_children_[segment];
        if (!child) { child = std::make_unique<trie>(); }
        return *child;
    }

    /// @brief Walks the subtree, rebuilding the pattern each payload was reached by.
    void collect_active_subscriptions_internal(
        std::vector<topic_pattern_item>& current_path, std::vector<std::pair<topic_pattern_path, const T*>>& collected
    ) const {
        auto append = [&current_path, &collected](const T* payload) {
            auto rebuilt = topic_pattern_path::make_from_segments(current_path);
            // Unreachable: every pattern in the tree was validated before it was inserted.
            if (!rebuilt) { return; }
            collected.emplace_back(std::move(rebuilt).value(), payload);
        };

        if (exact_match_data_.has_value()) { append(&*exact_match_data_); }
        if (multi_level_wildcard_data_.has_value()) {
            current_path.push_back(topic_pattern_item::make_hash());
            append(&*multi_level_wildcard_data_);
            current_path.pop_back();
        }
        if (single_level_wildcard_node_) {
            current_path.push_back(topic_pattern_item::make_plus());
            single_level_wildcard_node_->collect_active_subscriptions_internal(current_path, collected);
            current_path.pop_back();
        }
        for (const auto& [segment, child] : exact_children_) {
            current_path.push_back(topic_pattern_item::make_str(segment));
            child->collect_active_subscriptions_internal(current_path, collected);
            current_path.pop_back();
        }
    }

    /// @brief Payload for a subscription whose pattern ends at this node.
    std::optional<T> exact_match_data_;
    /// @brief Subtrees for literal next levels.
    /// @details The subtree is held by pointer rather than by value: a node may not contain an incomplete type by
    /// value, and the indirection also keeps a payload table's address stable while its siblings are created.
    std::unordered_map<std::string, std::unique_ptr<trie>, transparent_string_hash, std::equal_to<>> exact_children_;
    /// @brief Subtree for a `+` level.
    std::unique_ptr<trie> single_level_wildcard_node_;
    /// @brief Payload for a pattern that ends in `#`.
    std::optional<T> multi_level_wildcard_data_;
};

} // namespace ba7lya::topic
