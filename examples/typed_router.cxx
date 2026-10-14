///
/// @file typed_router.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Example: typing the subscription table and riding the router's broker decisions.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///

/// @brief The `fake_client` stands in for a real MQTT client: the router says *when* a subscription has to
/// be made, and the client is the thing that would make it. Swapping it for rumqttc, Paho or
/// nng means implementing `subscribe`/`unsubscribe` and leaving the rest of this file alone.
/// @details Build and run:
/// @note     cmake --preset win-msvc-2026-x64 -Dba7lya.topic_BUILD_EXAMPLE=ON
///     cmake --build --preset win-msvc-2026-x64-dbg
///     ./build/win-msvc-2026-x64/bin/Debug/topic_example_typed_router

// SPDX-License-Identifier: MIT

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "topic.hxx"

namespace {

using ba7lya::topic::qos;

///
/// @brief The payload a subscription carries: a named handler.
/// @details `empty()` is the whole `payload_container` contract the trie asks for, and it lets a node whose
/// handlers have all been removed be pruned.
///
struct handler {
    std::string name;

    [[nodiscard]]
    bool empty() const noexcept {
        return name.empty();
    }
};

///
/// @brief What the broker would be holding, plus a log of what it was told.
///
class fake_client {
public:
    void subscribe(const std::string& wire, qos level) {
        std::cout << "    -> broker.subscribe(\"" << wire << "\", " << to_string(level) << ")\n";
        subscriptions_[wire] = std::max(subscriptions_[wire], level);
    }

    void unsubscribe(const std::string& wire) {
        std::cout << "    -> broker.unsubscribe(\"" << wire << "\")\n";
        subscriptions_.erase(wire);
    }

    [[nodiscard]]
    const std::unordered_map<std::string, qos>& subscriptions() const noexcept {
        return subscriptions_;
    }

private:
    std::unordered_map<std::string, qos> subscriptions_;
};

///
/// @brief A router and a client kept in step, which is what a session is.
///
class session {
public:
    /// @brief Registers a handler, subscribing on the broker only when the router says it is needed.
    ba7lya::topic::subscription_id subscribe(const std::string& filter, qos level, const std::string& handler_name) {
        const auto parsed = ba7lya::topic::topic_pattern_path::parse(filter);
        if (!parsed) {
            std::cout << "    !! rejected: " << describe(parsed.error()) << '\n';
            return {};
        }

        const auto registered = router_.add_subscription(parsed.value(), level, handler { handler_name });
        const auto* record = router_.get_topic_by_id(registered.id).value();
        if (registered.needs_subscribe) { client_.subscribe(record->first.mqtt_pattern(), record->second); }
        return registered.id;
    }

    /// @brief Removes `id`, unsubscribing on the broker only once the pattern has no handlers left.
    void unsubscribe(ba7lya::topic::subscription_id id) {
        const auto removed = router_.unsubscribe(id);
        if (!removed) {
            std::cout << "    !! " << describe(removed.error()) << '\n';
            return;
        }
        if (removed.value().topic_now_empty) { client_.unsubscribe(removed.value().pattern.mqtt_pattern()); }
    }

    /// @brief Delivers `topic` and prints which handlers it reached.
    void deliver(const std::string& topic) const {
        const auto parsed = ba7lya::topic::topic_path::parse(topic);
        if (!parsed) {
            std::cout << "  " << topic << " -> invalid topic\n";
            return;
        }

        std::vector<std::pair<std::size_t, std::string>> reached;
        for (const auto& subscriber : router_.get_subscribers(*parsed.value())) {
            reached.emplace_back(subscriber.id().value, subscriber.payload()->name);
        }
        std::ranges::sort(reached);

        std::cout << "  " << topic << " ->";
        if (reached.empty()) { std::cout << " nobody"; }
        else {
            for (const auto& [id, name] : reached) { std::cout << ' ' << name << "(#" << id << ')'; }
        }
        std::cout << '\n';
    }

    /// @brief What a reconnect would replay, which is the router's deduplicated subscription set.
    void print_resubscribe_plan() const {
        std::cout << "  resubscribe plan:\n";
        for (const auto& [wire, level] : router_.get_topics_for_resubscribe()) {
            std::cout << "    " << wire << " at " << to_string(level) << '\n';
        }
    }

    [[nodiscard]]
    const fake_client& client() const noexcept {
        return client_;
    }

private:
    ba7lya::topic::router<handler> router_;
    fake_client client_;
};

/// @brief Registers overlapping filters and shows which of them a topic reaches.
void show_overlapping_filters() {
    std::cout << "\n=== overlapping filters ===\n";
    session current;

    std::cout << "  registering\n";
    current.subscribe("home/kitchen/temperature", qos::AT_MOST_ONCE, "exact");
    current.subscribe("home/+/temperature", qos::AT_LEAST_ONCE, "any room");
    current.subscribe("home/kitchen/+", qos::AT_MOST_ONCE, "any kitchen sensor");
    current.subscribe("home/#", qos::AT_MOST_ONCE, "all of home");
    current.subscribe("garden/#", qos::AT_MOST_ONCE, "garden");

    std::cout << "  delivering\n";
    current.deliver("home/kitchen/temperature");
    current.deliver("home/hall/temperature");
    current.deliver("garden/lawn");
    current.deliver("office/desk");
}

/// @brief Shows that a second handler on a known pattern costs no wire traffic, and a higher QoS does.
void show_qos_aggregation() {
    std::cout << "\n=== QoS aggregation ===\n";
    session current;

    current.subscribe("sensors/+/data", qos::AT_MOST_ONCE, "first");
    std::cout << "  (the first handler needed a broker subscribe)\n";

    current.subscribe("sensors/{id}/data", qos::AT_MOST_ONCE, "second");
    std::cout << "  (same wire pattern at the same QoS: nothing was sent)\n";

    current.subscribe("sensors/+/data", qos::EXACTLY_ONCE, "third");
    std::cout << "  (the aggregated QoS rose, so the broker was told again)\n";
}

/// @brief Shows a filtered subscription: parameters bound before the pattern reaches the broker.
void show_filtered_subscription() {
    std::cout << "\n=== filtered subscription ===\n";
    session current;

    const auto template_path = ba7lya::topic::topic_pattern_path::parse("logs/{service}/{level}/{details:#}");
    if (!template_path) { return; }

    const auto filtered
        = template_path.value().bind_parameter("service", "auth").value().bind_parameter("level", "error").value();
    std::cout << "  template : " << template_path.value().topic_pattern() << '\n';
    std::cout << "  filtered : " << filtered.mqtt_pattern() << '\n';

    current.subscribe(filtered.topic_pattern(), qos::AT_LEAST_ONCE, "auth errors");
    std::cout << "  delivering\n";
    current.deliver("logs/auth/error/handshake/failed");
    current.deliver("logs/db/error/handshake/failed");
}

/// @brief Shows the shutdown path: the broker is left holding exactly what the router still serves.
void show_teardown() {
    std::cout << "\n=== teardown ===\n";
    session current;
    const auto first = current.subscribe("home/#", qos::AT_MOST_ONCE, "first");
    current.subscribe("home/#", qos::AT_MOST_ONCE, "second");

    std::cout << "  removing the first handler\n";
    current.unsubscribe(first);
    std::cout << "  (a handler remains, so the broker keeps the pattern)\n";

    std::cout << "  removing the second handler\n";
    current.unsubscribe(ba7lya::topic::subscription_id { 1 });
    std::cout << "  (the pattern is empty, so the broker subscription goes)\n";

    std::cout << "  broker now holds " << current.client().subscriptions().size() << " subscription(s)\n";
}

/// @brief Shows what an application replays after a broker restart.
void show_reconnect() {
    std::cout << "\n=== reconnect ===\n";
    session current;
    current.subscribe("sensors/+/data", qos::AT_MOST_ONCE, "low");
    current.subscribe("sensors/+/data", qos::EXACTLY_ONCE, "high");
    current.subscribe("alerts/#", qos::AT_LEAST_ONCE, "alerts");

    // Two subscribers share one wire pattern at different QoS; the plan collapses them to one entry.
    current.print_resubscribe_plan();
}

} // namespace

int main() {
    show_overlapping_filters();
    show_qos_aggregation();
    show_filtered_subscription();
    show_teardown();
    show_reconnect();
    std::cout << '\n';
}
