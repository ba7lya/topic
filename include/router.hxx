///
/// @file router.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Subscription router built on the topic matcher.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "error.hxx"
#include "matcher.hxx"
#include "pattern.hxx"
#include "qos.hxx"
#include "result.hxx"

namespace ba7lya::topic {

///
/// @brief What registering a subscription did to the router.
/// @details The two facts travel together because a caller acts on both at once: the id says how to cancel the
/// registration later, and the flag says whether the broker has to hear about it now.
///
struct registration_outcome {
    /// @brief `true` when this is the first subscription for the pattern or it raised the pattern's effective QoS - the
    /// only cases where the caller must (re)subscribe on the broker.
    bool needs_subscribe = false;
    /// @brief The new registration's id, to hand back to `unsubscribe`.
    subscription_id id;
};

///
/// @brief What removing a subscription did to the router.
///
struct removal_outcome {
    /// @brief `true` when no subscription is left for the pattern, which is when the caller should unsubscribe it on
    /// the broker.
    bool topic_now_empty = false;
    /// @brief The pattern the removed id was registered under.
    topic_pattern_path pattern;
};

///
/// @brief Routes MQTT topic names to subscription payloads.
/// @details A subscription is a pattern plus a payload plus an id. Delivered topic names are matched against every
/// registered pattern - `+` and `#` included - and the router bookkeeps which distinct broker subscriptions are live
/// and at what QoS, so a caller can tell whether a registration actually needs a trip to the broker.
/// @note Not thread-safe: the Rust original takes `&mut self` throughout, and a caller that shares one router across
/// threads must serialise access itself.
///
template<payload_container T>
class router {
public:
    /// @brief The payload table a matcher node holds, one entry per subscription ending at that node.
    using subscription_table = std::unordered_map<subscription_id, T, subscription_id_hash>;

    /// @brief The pattern and effective QoS recorded for one subscription.
    using subscription_record = std::pair<topic_pattern_path, qos>;

    /// @brief Every active subscription, keyed by id.
    using subscription_registry = std::unordered_map<subscription_id, subscription_record, subscription_id_hash>;

    ///
    /// @brief One subscription that matched a delivered topic name.
    /// @details A view rather than an owner: the pattern and the payload point into the router's own storage and stay
    /// valid only while that registration lives, so a subscriber must not be held across a mutation of the router it
    /// came from.
    ///
    class subscriber {
    public:
        /// @brief Records one match. Used by `get_subscribers`.
        subscriber(subscription_id id, const topic_pattern_path* pattern, qos effective_qos, const T* payload)
            : id_(id)
            , pattern_(pattern)
            , effective_qos_(effective_qos)
            , payload_(payload) {}

        /// @brief The subscription's id.
        [[nodiscard]]
        subscription_id id() const noexcept {
            return id_;
        }

        /// @brief The subscription's pattern; borrowed from the router's registry.
        [[nodiscard]]
        const topic_pattern_path* pattern() const noexcept {
            return pattern_;
        }

        /// @brief The QoS the subscription was registered with.
        [[nodiscard]]
        qos effective_qos() const noexcept {
            return effective_qos_;
        }

        /// @brief The caller-supplied payload; borrowed from the router's matcher.
        [[nodiscard]]
        const T* payload() const noexcept {
            return payload_;
        }

    private:
        /// @brief The subscription's id.
        subscription_id id_;
        /// @brief The subscription's pattern.
        const topic_pattern_path* pattern_ = nullptr;
        /// @brief The QoS the subscription was registered with.
        qos effective_qos_ = qos::AT_MOST_ONCE;
        /// @brief The caller-supplied payload.
        const T* payload_ = nullptr;
    };

    /// @brief Builds an empty router with no subscriptions.
    router() = default;

    /// @brief Registers `topic` with `level` and `payload`.
    /// @details The returned `registration_outcome` says whether this registration needs a trip to the broker and under
    /// which id the registration can be cancelled later.
    /// @note Throws `std::logic_error` if the two internal tables disagree, which ports the Rust original's `panic!` -
    /// reaching it means a bug in this class, not a bad argument.
    [[nodiscard]]
    registration_outcome add_subscription(topic_pattern_path topic, qos level, T payload);

    /// @brief Removes the subscription `id`.
    /// @details The returned `removal_outcome` says whether the pattern is now empty, which is when the caller should
    /// unsubscribe it on the broker, and which pattern was removed. Fails with a `subscription_not_found_error` when
    /// `id` is unknown.
    /// @note The broker keeps the higher QoS after a removal. That is safe but can be wasteful; the downgrade is left
    /// undone, as in the Rust original.
    [[nodiscard]]
    result<removal_outcome, router_error> unsubscribe(subscription_id id);

    /// @brief Every subscription whose pattern matches `topic`.
    /// @details One delivered topic name can match several patterns through `+` and `#`, so this returns a list.
    /// @exception Throw `std::logic_error` on the same internal inconsistency as `add_subscription`.
    [[nodiscard]]
    std::vector<subscriber> get_subscribers(const topic_path& topic) const;

    /// @brief Every active subscription, keyed by id.
    /// Patterns are not deduplicated: several subscriptions may share one.
    [[nodiscard]]
    const subscription_registry& get_active_subscriptions() const noexcept {
        return registry_;
    }

    /// @brief The distinct broker subscription patterns to unsubscribe, with wildcards anonymous.
    /// @details Deduplicated by the wire pattern, so two templates that differ only in a parameter name -
    /// `sensors/{id}/data` and `sensors/{name}/data` - appear once.
    [[nodiscard]]
    std::unordered_set<std::string> get_topics_for_unsubscribe() const;

    /// @brief The distinct broker subscription patterns to (re)subscribe, each at the highest QoS any of its
    /// subscriptions asked for.
    /// @details An empty `#` filter behaves as `#` rather than `/#`: bound-value substitution fills the levels between
    /// the separators, so no phantom leading or trailing level appears.
    [[nodiscard]]
    std::unordered_map<std::string, qos> get_topics_for_resubscribe() const;

    /// @brief Drops every subscription and resets the id counter.
    /// @details A shutdown hook: the caller is expected to have let the broker know first.
    void cleanup() {
        matcher_ = topic_matcher_node<subscription_table> {};
        registry_.clear();
        next_id_ = 0;
    }

    /// @brief The `(pattern, qos)` recorded for `id`.
    /// @details Fails with a `subscription_not_found_error` when `id` is unknown.    [[nodiscard]]
    result<const subscription_record*, router_error> get_topic_by_id(subscription_id id) const;

private:
    /// @brief Looks up the record for `id`, throwing when the registry disagrees with the tree.
    [[nodiscard]]
    const subscription_record* expect_record(const subscription_id& id) const;

    /// @brief The trie of subscription payload tables.
    topic_matcher_node<subscription_table> matcher_;
    /// @brief The pattern and QoS of every subscription, keyed by id.
    subscription_registry registry_;
    /// @brief The id to hand out next.
    std::size_t next_id_ = 0;
};

// The member definitions live here rather than in a `.cxx` because the payload type is a template parameter: no
// translation unit could instantiate them on the caller's behalf. The error types and the id helpers - everything that
// does not depend on `T` - live in `error.hxx` and `error.cxx`.

template<payload_container T>
const router<T>::subscription_record* router<T>::expect_record(const subscription_id& id) const {
    const auto entry = registry_.find(id);
    if (entry == registry_.end()) {
        throw std::logic_error(
            "BUG: Subscription id " + id.to_string() + " exists in topic matcher but missing from subscriptions"
        );
    }
    return &entry->second;
}

template<payload_container T>
registration_outcome router<T>::add_subscription(topic_pattern_path topic, qos level, T payload) {
    subscription_table& table = matcher_.get_or_create_subscription_table(topic);

    const bool needs_subscribe = std::none_of(table.begin(), table.end(), [this, level](const auto& entry) {
        return expect_record(entry.first)->second >= level;
    });

    const subscription_id id { next_id_ };
    next_id_ = next_id_ + 1; // Wraps at the type's width, as the Rust original's wrapping_add does.

    table.insert_or_assign(id, std::move(payload));
    registry_.insert_or_assign(id, subscription_record { std::move(topic), level });

    return registration_outcome { needs_subscribe, id };
}

template<payload_container T>
result<removal_outcome, router_error> router<T>::unsubscribe(subscription_id id) {
    const auto entry = registry_.find(id);
    if (entry == registry_.end()) { return subscription_not_found(id); }

    topic_pattern_path topic = entry->second.first;
    registry_.erase(entry);

    const std::vector<topic_pattern_item> resolved_segments = topic.resolve_bound_segments();
    const auto pruned = matcher_.update_node(resolved_segments, [id](subscription_table& table) { table.erase(id); });
    if (!pruned) { return make_matching_failed_error(pruned.error()); }
    return removal_outcome { pruned.value(), std::move(topic) };
}

template<payload_container T>
std::vector<typename router<T>::subscriber> router<T>::get_subscribers(const topic_path& topic) const {
    std::vector<subscriber> subscribers;
    for (const subscription_table* table : matcher_.find_by_path(topic)) {
        for (const auto& [id, payload] : *table) {
            const subscription_record* record = expect_record(id);
            subscribers.push_back(subscriber { id, &record->first, record->second, &payload });
        }
    }
    return subscribers;
}

template<payload_container T>
std::unordered_set<std::string> router<T>::get_topics_for_unsubscribe() const {
    std::unordered_set<std::string> topics;
    for (const auto& [id, record] : registry_) {
        (void)id;
        topics.insert(record.first.mqtt_pattern());
    }
    return topics;
}

template<payload_container T>
std::unordered_map<std::string, qos> router<T>::get_topics_for_resubscribe() const {
    std::unordered_map<std::string, qos> topics;
    for (const auto& [id, record] : registry_) {
        (void)id;
        const auto [entry, inserted] = topics.insert({ record.first.mqtt_pattern(), record.second });
        if (!inserted && record.second > entry->second) { entry->second = record.second; }
    }
    return topics;
}

template<payload_container T>
result<const typename router<T>::subscription_record*, router_error> router<T>::get_topic_by_id(
    subscription_id id
) const {
    const auto entry = registry_.find(id);
    if (entry == registry_.end()) { return subscription_not_found(id); }
    return &entry->second;
}

} // namespace ba7lya::topic
