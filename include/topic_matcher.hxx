// SPDX-License-Identifier: MIT
//
// Prefix-tree topic matcher.
//
// `topic_matcher_node<T>` is a trie keyed by topic levels that stores a payload `T` per
// subscription pattern — literals, `+` and `#` wildcards — and resolves every payload matching a
// concrete topic name. The `payload_container` concept lets a node prune an empty payload during
// removal, standing in for the Rust original's `Len` trait.

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
#include <variant>
#include <vector>

#include "result.hxx"
#include "topic_match.hxx"
#include "topic_pattern_item.hxx"
#include "topic_pattern_path.hxx"

namespace ba7lya::topic_engine {

/// A level of a subscription pattern turned out not to be present in the tree.
struct invalid_segment_error {
    /// The offending level.
    std::string segment;
    /// Zero-based position of the level within the path.
    std::size_t position = 0;

    [[nodiscard]]
    std::string message() const;
};

/// Everything that can go wrong while maintaining the tree.
using topic_matcher_error = std::variant<invalid_segment_error>;

/// Builds an `invalid_segment_error`.
[[nodiscard]]
topic_matcher_error invalid_segment(std::string segment, std::size_t position);

/// Renders an error the way the Rust original's `Display` impls do.
[[nodiscard]]
std::string describe(const topic_matcher_error& error);

/// What a node may hold as a payload: a container that can say whether it is empty.
///
/// The Rust original spells this as the `Len` trait, implemented for `HashSet` and `HashMap`. A
/// concept does the same job here without type erasure, so the payload type stays fully visible
/// to the optimizer.
template<typename T>
concept payload_container = std::default_initializable<T> && requires(const T& container) {
    { container.empty() } -> std::convertible_to<bool>;
};

/// Hashes a key the way `std::hash<std::string_view>` would, so a `std::string_view` level can be
/// looked up in a `std::string`-keyed map without building a temporary string.
///
/// Matching runs once per delivered message per subscription, so the allocation a converting
/// lookup would cost is worth designing out. Both overloads agree on the value of a key, which is
/// what makes the heterogeneous lookup sound.
struct transparent_string_hash {
    /// Marks the hash as usable for heterogeneous lookup.
    using is_transparent = void;

    /// Hashes a view of the key.
    [[nodiscard]]
    std::size_t operator()(std::string_view text) const noexcept {
        return std::hash<std::string_view> {}(text);
    }

    /// Hashes an owning key; identical to the view overload for the same characters.
    [[nodiscard]]
    std::size_t operator()(const std::string& text) const noexcept {
        return std::hash<std::string_view> {}(text);
    }
};

/// One node of the topic matching tree, standing for a level of a topic path.
///
/// Four slots, one per way a subscription can continue from here: a literal level's payload, the
/// literal levels' subtrees, the `+` wildcard's subtree, and the `#` wildcard's payload. A `#` is
/// always the last level of a filter, which is why it needs no subtree.
///
/// Header-only by necessity: the payload type is a template parameter, so no translation unit can
/// instantiate the members for it. The companion `topic_matcher.cxx` carries the parts that do not
/// depend on `T`.
template<payload_container T>
class topic_matcher_node {
public:
    /// Builds an empty node.
    topic_matcher_node() = default;

    /// Nodes own their subtrees, so they move but never copy.
    topic_matcher_node(const topic_matcher_node&) = delete;
    /// Nodes own their subtrees, so they move but never copy.
    topic_matcher_node& operator=(const topic_matcher_node&) = delete;
    topic_matcher_node(topic_matcher_node&&) noexcept = default;
    topic_matcher_node& operator=(topic_matcher_node&&) noexcept = default;
    ~topic_matcher_node() = default;

    /// Whether this node holds no payload and has no children, so its parent may drop it.
    [[nodiscard]]
    bool is_empty() const {
        return (!exact_match_data_.has_value() || exact_match_data_->empty()) && exact_children_.empty()
            && !single_level_wildcard_node_
            && (!multi_level_wildcard_data_.has_value() || multi_level_wildcard_data_->empty());
    }

    /// Returns the payload table for `topic_path`, creating it and any missing levels.
    ///
    /// The returned reference stays valid until the node's subtree is modified: children live in
    /// `std::unique_ptr`s, so creating a sibling table cannot move this one.
    [[nodiscard]]
    T& get_or_create_subscription_table(const topic_pattern_path& topic_path) {
        topic_matcher_node* current = this;
        for (const topic_pattern_item& segment : topic_path.resolve_bound_segments()) {
            if (segment.is_literal()) {
                current = &current->exact_child(segment.literal());
                continue;
            }
            if (segment.is_plus()) {
                if (!current->single_level_wildcard_node_) {
                    current->single_level_wildcard_node_ = std::make_unique<topic_matcher_node>();
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

    /// Applies `update` to the payload table stored at `path`, pruning what it empties.
    ///
    /// Returns whether this node became empty, which is the caller's signal to drop it. A level of
    /// `path` that the tree does not have is an `invalid_segment_error`.
    template<typename F>
        requires std::invocable<F&, T&>
    result<bool, topic_matcher_error> update_node(std::span<const topic_pattern_item> path, F update) {
        if (path.empty()) {
            if (!exact_match_data_.has_value()) { return invalid_segment("no_data_for_empty_path", 0); }
            update(*exact_match_data_);
            if (exact_match_data_->empty()) { exact_match_data_.reset(); }
            return is_empty();
        }

        const topic_pattern_item& current_segment = path.front();
        const std::span<const topic_pattern_item> rest = path.subspan(1);

        if (current_segment.is_literal()) {
            const auto child = exact_children_.find(current_segment.literal());
            if (child == exact_children_.end()) { return invalid_segment(current_segment.literal(), 0); }
            const auto pruned = child->second->update_node(rest, update);
            if (!pruned) { return pruned.error(); }
            if (pruned.value()) {
                exact_children_.erase(child);
                return is_empty();
            }
            return false;
        }

        if (current_segment.is_plus()) {
            if (!single_level_wildcard_node_) { return invalid_segment("+", 0); }
            const auto pruned = single_level_wildcard_node_->update_node(rest, update);
            if (!pruned) { return pruned.error(); }
            if (pruned.value()) {
                single_level_wildcard_node_.reset();
                return is_empty();
            }
            return false;
        }

        if (!multi_level_wildcard_data_.has_value()) { return invalid_segment("#", 0); }
        update(*multi_level_wildcard_data_);
        if (multi_level_wildcard_data_->empty()) {
            multi_level_wildcard_data_.reset();
            return is_empty();
        }
        return false;
    }

    /// Every payload whose pattern matches `topic`.
    ///
    /// A delivered topic may match several patterns at once, through `+` or `#`, so the result is a
    /// list rather than a single payload.
    [[nodiscard]]
    std::vector<const T*> find_by_path(const topic_path& topic) const {
        std::vector<const T*> matching_payloads;
        collect_matching_subscriptions(topic.segments(), true, matching_payloads);
        return matching_payloads;
    }

    /// Every `(pattern, payload)` pair in the tree, by walking every branch.
    ///
    /// A diagnostic helper for asserting what a router actually holds; production code that wants
    /// the active subscriptions should ask the router, which keeps a flat table and is cheaper.
    /// The order is unspecified — the levels live in a hash map — so compare the result as a set.
    [[nodiscard]]
    std::vector<std::pair<topic_pattern_path, const T*>> collect_active_subscriptions() const {
        std::vector<std::pair<topic_pattern_path, const T*>> collected;
        std::vector<topic_pattern_item> current_path;
        collect_active_subscriptions_internal(current_path, collected);
        return collected;
    }

private:
    /// Returns the subtree for a literal level, creating it if absent.
    [[nodiscard]]
    topic_matcher_node& exact_child(const std::string& segment) {
        auto& child = exact_children_[segment];
        if (!child) { child = std::make_unique<topic_matcher_node>(); }
        return *child;
    }

    /// Walks `topic`, appending every payload that matches.
    ///
    /// `at_root` marks the first level, where the MQTT §4.7.2 rule applies: a filter whose first
    /// level is `+` or `#` must not match a `$`-prefixed topic name. Skipping both wildcard
    /// branches there leaves the literal branch above intact, so `$SYS/...` subscriptions still
    /// resolve.
    void collect_matching_subscriptions(
        std::span<const std::string_view> topic, bool at_root, std::vector<const T*>& matching_payloads
    ) const {
        if (topic.empty()) {
            if (exact_match_data_.has_value()) { matching_payloads.push_back(&*exact_match_data_); }
            if (multi_level_wildcard_data_.has_value()) { matching_payloads.push_back(&*multi_level_wildcard_data_); }
            return;
        }

        const std::string_view segment = topic.front();
        const std::span<const std::string_view> remaining = topic.subspan(1);

        if (const auto child = exact_children_.find(segment); child != exact_children_.end()) {
            child->second->collect_matching_subscriptions(remaining, false, matching_payloads);
        }
        if (at_root && is_dollar_topic(segment)) { return; }
        if (single_level_wildcard_node_) {
            single_level_wildcard_node_->collect_matching_subscriptions(remaining, false, matching_payloads);
        }
        if (multi_level_wildcard_data_.has_value()) { matching_payloads.push_back(&*multi_level_wildcard_data_); }
    }

    /// Walks the subtree, rebuilding the pattern each payload was reached by.
    void collect_active_subscriptions_internal(
        std::vector<topic_pattern_item>& current_path, std::vector<std::pair<topic_pattern_path, const T*>>& collected
    ) const {
        auto append = [&current_path, &collected](const T* payload) {
            auto rebuilt = topic_pattern_path::make_from_segments(current_path);
            if (!rebuilt) {
                // Unreachable: every pattern in the tree was validated before it was inserted.
                return;
            }
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

    /// Payload for a subscription whose pattern ends at this node.
    std::optional<T> exact_match_data_;
    /// Subtrees for literal next levels.
    ///
    /// The subtree is held by pointer rather than by value: a node may not contain an incomplete
    /// type by value, and the indirection also keeps a payload table's address stable while its
    /// siblings are created.
    std::unordered_map<std::string, std::unique_ptr<topic_matcher_node>, transparent_string_hash, std::equal_to<>>
        exact_children_;
    /// Subtree for a `+` level.
    std::unique_ptr<topic_matcher_node> single_level_wildcard_node_;
    /// Payload for a pattern that ends in `#`.
    std::optional<T> multi_level_wildcard_data_;
};

} // namespace ba7lya::topic_engine
