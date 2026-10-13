// SPDX-License-Identifier: MIT
//
// Subscription router built on the topic matcher.
//
// `topic_router<T>` maps MQTT subscription patterns to caller-supplied payloads `T`, gives each
// one a `subscription_id`, and resolves every payload whose pattern matches a delivered topic
// name. It also tracks the set of distinct broker subscriptions and their effective QoS, so the
// caller knows when to actually subscribe or unsubscribe on the wire.

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

#include "qos.hxx"
#include "result.hxx"
#include "topic_matcher.hxx"
#include "topic_pattern_path.hxx"

namespace ba7lya::topic_engine {

/// Identifies one registration in a router.
///
/// Ids are handed out in order and reused only after `cleanup`, which is enough to find a
/// subscription again when it has to be cancelled. Copyable and comparable, so it can be a map key.
struct subscription_id {
    /// The numeric identity.
    std::size_t value = 0;

    /// Renders as `SubscriptionId(<value>)`.
    [[nodiscard]]
    std::string to_string() const;

    friend constexpr bool operator==(const subscription_id&, const subscription_id&) noexcept = default;
};

/// Hashes a `subscription_id` for use as a map key.
struct subscription_id_hash {
    /// Hashes the numeric identity.
    [[nodiscard]]
    std::size_t operator()(const subscription_id& id) const noexcept;
};

/// A subscription pattern was rejected while it was being registered.
struct invalid_pattern_error {
    /// Why the pattern was rejected.
    topic_pattern_error cause;

    [[nodiscard]]
    std::string message() const;
};

/// The matcher failed while a subscription was being removed.
struct matching_failed_error {
    /// What the matcher objected to.
    topic_matcher_error cause;

    [[nodiscard]]
    std::string message() const;
};

/// No subscription is registered under this id.
struct subscription_not_found_error {
    /// The id that was looked up.
    subscription_id id;

    [[nodiscard]]
    std::string message() const;
};

/// A topic name was rejected for a routing operation.
struct invalid_routing_topic_error {
    /// The topic that was rejected.
    std::string topic;
    /// Why it was rejected.
    std::string reason;

    [[nodiscard]]
    std::string message() const;
};

/// The router's internal tables disagree with each other, which is a bug rather than a caller
/// mistake.
struct internal_state_corrupted_error {
    /// What the inconsistency was.
    std::string details;

    [[nodiscard]]
    std::string message() const;
};

/// Everything that can go wrong while routing.
using topic_router_error = std::variant<
    invalid_pattern_error, matching_failed_error, subscription_not_found_error, invalid_routing_topic_error,
    internal_state_corrupted_error>;

/// Wraps a pattern error as a routing error.
[[nodiscard]]
topic_router_error make_invalid_pattern_error(const topic_pattern_error& cause);

/// Wraps a matcher error as a routing error.
[[nodiscard]]
topic_router_error make_matching_failed_error(const topic_matcher_error& cause);

/// Builds a `subscription_not_found_error`.
[[nodiscard]]
topic_router_error subscription_not_found(subscription_id id);

/// Builds an `invalid_routing_topic_error`.
[[nodiscard]]
topic_router_error invalid_routing_topic(std::string topic, std::string reason);

/// Builds an `internal_state_corrupted_error`.
[[nodiscard]]
topic_router_error internal_state_corrupted(std::string details);

/// Renders an error the way the Rust original's `Display` impls do.
[[nodiscard]]
std::string describe(const topic_router_error& error);

/// Routes MQTT topic names to subscription payloads.
///
/// A subscription is a pattern plus a payload plus an id. Delivered topic names are matched
/// against every registered pattern — `+` and `#` included — and the router bookkeeps which
/// distinct broker subscriptions are live and at what QoS, so a caller can tell whether a
/// registration actually needs a trip to the broker.
///
/// Not thread-safe: the Rust original takes `&mut self` throughout, and a caller that shares one
/// router across threads must serialise access itself.
template<payload_container T>
class topic_router {
public:
    /// The payload table a matcher node holds, one entry per subscription ending at that node.
    using subscription_table = std::unordered_map<subscription_id, T, subscription_id_hash>;

    /// The pattern and effective QoS recorded for one subscription.
    using subscription_record = std::pair<topic_pattern_path, qos>;

    /// Every active subscription, keyed by id.
    using subscription_registry = std::unordered_map<subscription_id, subscription_record, subscription_id_hash>;

    /// One subscription that matched a delivered topic name.
    struct subscriber {
        /// The subscription's id.
        subscription_id id;
        /// The subscription's pattern.
        const topic_pattern_path* pattern = nullptr;
        /// The QoS the subscription was registered with.
        qos effective_qos = qos::AT_MOST_ONCE;
        /// The caller-supplied payload.
        const T* payload = nullptr;
    };

    /// Builds an empty router with no subscriptions.
    topic_router() = default;

    /// Registers `topic` with `level` and `payload`.
    ///
    /// Returns `(needs_subscribe, id)`. `needs_subscribe` is `true` when this is the first
    /// subscription for the pattern or when it raises the pattern's effective QoS, which is when,
    /// and only when, the caller must (re)subscribe on the broker. `id` names the registration for
    /// later `unsubscribe`.
    ///
    /// Throws `std::logic_error` if the two internal tables disagree, which ports the Rust
    /// original's `panic!` — reaching it means a bug in this class, not a bad argument.
    [[nodiscard]]
    std::pair<bool, subscription_id> add_subscription(topic_pattern_path topic, qos level, T payload);

    /// Removes the subscription `id`.
    ///
    /// Returns `(topic_now_empty, pattern)`. `topic_now_empty` is `true` when no subscription is
    /// left for the pattern, which is when the caller should unsubscribe it on the broker. Fails
    /// with a `subscription_not_found_error` when `id` is unknown.
    ///
    /// The broker keeps the higher QoS after a removal. That is safe but can be wasteful; see
    /// `max_qos_for_topic`, which is the helper for the downgrade this port also leaves undone.
    [[nodiscard]]
    result<std::pair<bool, topic_pattern_path>, topic_router_error> unsubscribe(subscription_id id);

    /// Every subscription whose pattern matches `topic`.
    ///
    /// One delivered topic name can match several patterns through `+` and `#`, so this returns a
    /// list. Throws `std::logic_error` on the same internal inconsistency as `add_subscription`.
    [[nodiscard]]
    std::vector<subscriber> get_subscribers(const topic_path& topic) const;

    /// Every active subscription, keyed by id. Patterns are not deduplicated: several
    /// subscriptions may share one.
    [[nodiscard]]
    const subscription_registry& get_active_subscriptions() const noexcept {
        return registry_;
    }

    /// The distinct broker subscription patterns to unsubscribe, with wildcards anonymous.
    ///
    /// Deduplicated by the wire pattern, so two templates that differ only in a parameter name —
    /// `sensors/{id}/data` and `sensors/{name}/data` — appear once.
    [[nodiscard]]
    std::unordered_set<std::string> get_topics_for_unsubscribe() const;

    /// The distinct broker subscription patterns to (re)subscribe, each at the highest QoS any of
    /// its subscriptions asked for.
    ///
    /// An empty `#` filter behaves as `#` rather than `/#`: bound-value substitution fills the
    /// levels between the separators, so no phantom leading or trailing level appears.
    [[nodiscard]]
    std::unordered_map<std::string, qos> get_topics_for_resubscribe() const;

    /// Drops every subscription and resets the id counter.
    ///
    /// A shutdown hook: the caller is expected to have let the broker know first.
    void cleanup() {
        matcher_ = topic_matcher_node<subscription_table> {};
        registry_.clear();
        next_id_ = 0;
    }

    /// The `(pattern, qos)` recorded for `id`.
    ///
    /// Fails with a `subscription_not_found_error` when `id` is unknown.
    [[nodiscard]]
    result<const subscription_record*, topic_router_error> get_topic_by_id(subscription_id id) const;

private:
    /// The highest QoS among the subscribers to `topic`.
    ///
    /// Deliberately unused for now: it is the ready-made helper for the QoS downgrade described on
    /// `unsubscribe`. The QoS-raising counterpart is inlined in `add_subscription`; when the
    /// downgrade is implemented the two should share this helper.
    [[nodiscard]]
    qos max_qos_for_topic(const topic_pattern_path& topic, const subscription_table& topic_subscriptions) const;

    /// Looks up the record for `id`, throwing when the registry disagrees with the tree.
    [[nodiscard]]
    const subscription_record* expect_record(const subscription_id& id) const;

    /// The trie of subscription payload tables.
    topic_matcher_node<subscription_table> matcher_;
    /// The pattern and QoS of every subscription, keyed by id.
    subscription_registry registry_;
    /// The id to hand out next.
    std::size_t next_id_ = 0;
};

// The member definitions live here rather than in a `.cxx` because the payload type is a template
// parameter: no translation unit could instantiate them on the caller's behalf. What does not
// depend on `T` — the error types and the id helpers — stays in `topic_router.cxx`.

template<payload_container T>
const topic_router<T>::subscription_record* topic_router<T>::expect_record(const subscription_id& id) const {
    const auto entry = registry_.find(id);
    if (entry == registry_.end()) {
        throw std::logic_error(
            "BUG: Subscription id " + id.to_string() + " exists in topic matcher but missing from subscriptions"
        );
    }
    return &entry->second;
}

template<payload_container T>
std::pair<bool, subscription_id> topic_router<T>::add_subscription(topic_pattern_path topic, qos level, T payload) {
    subscription_table& table = matcher_.get_or_create_subscription_table(topic);

    const bool needs_subscribe = std::none_of(table.begin(), table.end(), [this, level](const auto& entry) {
        return expect_record(entry.first)->second >= level;
    });

    const subscription_id id { next_id_ };
    next_id_ = next_id_ + 1; // Wraps at the type's width, as the Rust original's wrapping_add does.

    table.insert_or_assign(id, std::move(payload));
    registry_.insert_or_assign(id, subscription_record { std::move(topic), level });

    return { needs_subscribe, id };
}

template<payload_container T>
result<std::pair<bool, topic_pattern_path>, topic_router_error> topic_router<T>::unsubscribe(subscription_id id) {
    const auto entry = registry_.find(id);
    if (entry == registry_.end()) { return subscription_not_found(id); }

    topic_pattern_path topic = entry->second.first;
    registry_.erase(entry);

    const std::vector<topic_pattern_item> resolved_segments = topic.resolve_bound_segments();
    const auto pruned = matcher_.update_node(resolved_segments, [id](subscription_table& table) { table.erase(id); });
    if (!pruned) { return make_matching_failed_error(pruned.error()); }
    return std::pair<bool, topic_pattern_path> { pruned.value(), std::move(topic) };
}

template<payload_container T>
std::vector<typename topic_router<T>::subscriber> topic_router<T>::get_subscribers(const topic_path& topic) const {
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
qos topic_router<T>::max_qos_for_topic(
    const topic_pattern_path& topic, const subscription_table& topic_subscriptions
) const {
    (void)topic;
    qos maximum = qos::AT_MOST_ONCE;
    for (const auto& [id, payload] : topic_subscriptions) {
        (void)payload;
        maximum = std::max(maximum, expect_record(id)->second);
    }
    return maximum;
}

template<payload_container T>
std::unordered_set<std::string> topic_router<T>::get_topics_for_unsubscribe() const {
    std::unordered_set<std::string> topics;
    for (const auto& [id, record] : registry_) {
        (void)id;
        topics.insert(record.first.mqtt_pattern());
    }
    return topics;
}

template<payload_container T>
std::unordered_map<std::string, qos> topic_router<T>::get_topics_for_resubscribe() const {
    std::unordered_map<std::string, qos> topics;
    for (const auto& [id, record] : registry_) {
        (void)id;
        const auto [entry, inserted] = topics.insert({ record.first.mqtt_pattern(), record.second });
        if (!inserted && record.second > entry->second) { entry->second = record.second; }
    }
    return topics;
}

template<payload_container T>
result<const typename topic_router<T>::subscription_record*, topic_router_error> topic_router<T>::get_topic_by_id(
    subscription_id id
) const {
    const auto entry = registry_.find(id);
    if (entry == registry_.end()) { return subscription_not_found(id); }
    return &entry->second;
}

} // namespace ba7lya::topic_engine
