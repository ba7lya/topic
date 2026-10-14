///
/// @file typed_subscription_tests.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief End-to-end test of a typed MQTT session driven entirely through the public API.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <cstddef>
#include <gtest/gtest.h>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "topic.hxx"

namespace ba7lya::topic::e2e {
namespace {

/// @brief Parses a filter, throwing when the test feeds it something invalid.
[[nodiscard]]
topic_pattern_path pattern_of(const std::string& text) {
    auto parsed = topic_pattern_path::parse(text);
    if (!parsed) { throw std::runtime_error("pattern should be valid: " + text + ": " + describe(parsed.error())); }
    return std::move(parsed).value();
}

///
/// @brief The payload an application would attach to a subscription: a named handler.
/// @details `empty()` is what the trie's `payload_container` concept asks for, and here it means exactly what
/// it means for the containers: a payload with no name is nothing, so its node can be pruned.
///
struct handler {
    std::string name;

    [[nodiscard]]
    bool empty() const noexcept {
        return name.empty();
    }
};

///
/// @brief What one delivery produced: the handler name and the parameters it was able to extract.
///
struct delivery {
    std::string handler;
    std::string payload;
};

///
/// @brief A typed MQTT session: `router<handler>` plus the broker bookkeeping an app has to keep.
/// @details This is the shape the Rust README's integration example sketches, with the client replaced by
/// recorded wire calls so the flow stays offline and deterministic.
///
class typed_session {
public:
    /// @brief Registers `filter` for `name` and subscribes on the broker exactly when the router asks.
    /// @details Returns the subscription id and the wire pattern, in that order, so a test can assert on
    /// what the broker was told without reaching into the router.
    std::pair<subscription_id, std::string> subscribe(const std::string& filter, qos level, const std::string& name) {
        return subscribe(pattern_of(filter), level, name);
    }

    /// @brief Registers an already-built filter, bindings and all.
    /// @details The overload to reach for when the filter carries bound parameters: re-parsing its template
    /// would hand the router an unbound filter and quietly register the wider pattern.
    std::pair<subscription_id, std::string> subscribe(
        const topic_pattern_path& filter, qos level, const std::string& name
    ) {
        const auto registered = router_.add_subscription(filter, level, handler { .name = name });
        const auto* record = router_.get_topic_by_id(registered.id).value();

        std::string wire = record->first.mqtt_pattern();
        if (registered.needs_subscribe) {
            wire_subscriptions_[wire] = std::max(wire_subscriptions_[wire], record->second);
            wire_calls_.push_back(wire);
        }
        return { registered.id, std::move(wire) };
    }

    /// @brief Removes `id` and, if the router says the pattern went empty, drops it from the broker.
    bool unsubscribe(subscription_id id, std::string& dropped_wire) {
        const auto removed = router_.unsubscribe(id);
        if (!removed) { return false; }
        if (!removed.value().topic_now_empty) { return true; }

        dropped_wire = removed.value().pattern.mqtt_pattern();
        wire_subscriptions_.erase(dropped_wire);
        return true;
    }

    /// @brief The handler names a delivered topic reaches, each paired with the payload that arrived.
    /// @details Payloads live in the router's own storage, so they are read through the subscriber pointer
    /// rather than copied out of the registry.
    [[nodiscard]]
    std::vector<delivery> deliver(const std::string& topic, const std::string& payload) const {
        const auto parsed = topic_path::parse(topic);
        if (!parsed) { throw std::runtime_error("topic should be valid: " + topic); }

        std::vector<std::pair<std::size_t, delivery>> reached;
        for (const auto& subscriber : router_.get_subscribers(*parsed.value())) {
            reached.push_back(
                {
                    subscriber.id().value, delivery { subscriber.payload()->name, payload }
            }
            );
        }
        std::ranges::sort(reached, {}, &std::pair<std::size_t, delivery>::first);

        std::vector<delivery> deliveries;
        deliveries.reserve(reached.size());
        for (auto& entry : reached) { deliveries.push_back(std::move(entry.second)); }
        return deliveries;
    }

    /// @brief The handler names a delivery reaches, in id order — the compact form for most assertions.
    [[nodiscard]]
    std::vector<std::string> handlers_for(const std::string& topic) const {
        std::vector<std::string> names;
        for (const auto& entry : deliver(topic, "")) { names.push_back(entry.handler); }
        return names;
    }

    /// @brief The pattern texts the router still holds, sorted, for teardown assertions.
    [[nodiscard]]
    std::vector<std::string> active_patterns() const {
        std::vector<std::string> patterns;
        for (const auto& [id, record] : router_.get_active_subscriptions()) {
            patterns.push_back(record.first.mqtt_pattern());
        }
        std::ranges::sort(patterns);
        return patterns;
    }

    /// @brief What a real client would be holding on the wire right now.
    [[nodiscard]]
    const std::unordered_map<std::string, qos>& wire_subscriptions() const noexcept {
        return wire_subscriptions_;
    }

    /// @brief Every subscribe the router asked for, in order, repeats kept.
    [[nodiscard]]
    const std::vector<std::string>& wire_calls() const noexcept {
        return wire_calls_;
    }

    /// @brief The router's answer to "what do I replay after a reconnect".
    [[nodiscard]]
    std::unordered_map<std::string, qos> resubscribe_plan() const {
        return router_.get_topics_for_resubscribe();
    }

    /// @brief Shuts the session down the way an application does on exit.
    void shutdown() {
        router_.cleanup();
        wire_subscriptions_.clear();
    }

private:
    router<handler> router_;
    std::unordered_map<std::string, qos> wire_subscriptions_;
    std::vector<std::string> wire_calls_;
};

/// @brief Extracts a named parameter from a delivery, the way an application unpacks a topic.
[[nodiscard]]
std::string param_of(const std::string& filter, const std::string& topic, const std::string& name) {
    const auto matched = pattern_of(filter).match_str(topic);
    if (!matched) { throw std::runtime_error("topic should match: " + topic + ": " + describe(matched.error())); }

    const auto value = matched.value()->named_param(name);
    if (!value) { throw std::runtime_error("param should exist: " + name); }
    return std::string(*value);
}

// --- the flow the README walks through, start to finish ---

TEST(typed_subscription_tests, a_sensor_pipeline_from_pattern_to_delivery) {
    typed_session session;

    const auto [id, wire] = session.subscribe("sensors/{location}/{device}/data", qos::AT_LEAST_ONCE, "sensor data");
    EXPECT_EQ(wire, "sensors/+/+/data");
    EXPECT_EQ(id.value, 0U);

    // Subscribing on the broker is only the first step; the app still has to say what arrived.
    EXPECT_EQ(session.wire_calls(), std::vector<std::string>({ "sensors/+/+/data" }));
    EXPECT_EQ(session.handlers_for("sensors/kitchen/temp001/data"), std::vector<std::string>({ "sensor data" }));
    EXPECT_TRUE(session.handlers_for("sensors/kitchen/temp001/status").empty());

    // The parameters are what make the subscription typed rather than merely filtered.
    const std::string topic = "sensors/kitchen/temp001/data";
    EXPECT_EQ(param_of("sensors/{location}/{device}/data", topic, "location"), "kitchen");
    EXPECT_EQ(param_of("sensors/{location}/{device}/data", topic, "device"), "temp001");
}

TEST(typed_subscription_tests, a_payload_reaches_every_registered_handler_once) {
    typed_session session;
    session.subscribe("home/kitchen/temperature", qos::AT_MOST_ONCE, "exact");
    session.subscribe("home/+/temperature", qos::AT_MOST_ONCE, "any room");
    session.subscribe("home/kitchen/+", qos::AT_MOST_ONCE, "any kitchen sensor");
    session.subscribe("home/#", qos::AT_MOST_ONCE, "all of home");
    session.subscribe("+/kitchen/#", qos::AT_MOST_ONCE, "any kitchen subtree");
    session.subscribe("garden/#", qos::AT_MOST_ONCE, "garden");

    EXPECT_EQ(
        session.handlers_for("home/kitchen/temperature"),
        std::vector<std::string>({ "exact", "any room", "any kitchen sensor", "all of home", "any kitchen subtree" })
    );

    // The payload travels with each delivery, so the app can dispatch on the content too.
    const std::vector<delivery> received = session.deliver("home/kitchen/temperature", "21.5");
    ASSERT_EQ(received.size(), 5U);
    for (const delivery& entry : received) { EXPECT_EQ(entry.payload, "21.5"); }
}

// --- the decisions the application cannot make on its own ---

TEST(typed_subscription_tests, the_application_only_talks_to_the_broker_when_told_to) {
    typed_session session;

    // A new pattern is a new wire subscription.
    session.subscribe("sensors/+/data", qos::AT_MOST_ONCE, "first");
    EXPECT_EQ(session.wire_calls().size(), 1U);

    // A second handler at a higher QoS has to be replayed at the higher level.
    session.subscribe("sensors/{id}/data", qos::AT_LEAST_ONCE, "second");
    ASSERT_EQ(session.wire_calls().size(), 2U);
    EXPECT_EQ(session.wire_calls().back(), "sensors/+/data");
    EXPECT_EQ(session.wire_subscriptions().at("sensors/+/data"), qos::AT_LEAST_ONCE);

    // A third at a lower QoS changes nothing on the wire.
    session.subscribe("sensors/+/data", qos::AT_MOST_ONCE, "third");
    EXPECT_EQ(session.wire_calls().size(), 2U);
    EXPECT_EQ(session.handlers_for("sensors/x/data"), std::vector<std::string>({ "first", "second", "third" }));
}

TEST(typed_subscription_tests, a_reconnect_replays_exactly_what_the_broker_lost) {
    typed_session session;
    session.subscribe("sensors/+/data", qos::AT_MOST_ONCE, "low");
    session.subscribe("sensors/+/data", qos::EXACTLY_ONCE, "high");
    session.subscribe("alerts/#", qos::AT_LEAST_ONCE, "alerts");

    // The plan is deduplicated by wire pattern and carries the aggregated QoS.
    const std::unordered_map<std::string, qos> plan = session.resubscribe_plan();
    ASSERT_EQ(plan.size(), 2U);
    EXPECT_EQ(plan.at("sensors/+/data"), qos::EXACTLY_ONCE);
    EXPECT_EQ(plan.at("alerts/#"), qos::AT_LEAST_ONCE);
    EXPECT_EQ(plan, session.wire_subscriptions()) << "the router and the client must agree";

    // Replaying the plan is idempotent: the client ends up exactly where it was.
    for (const auto& [wire, level] : plan) { EXPECT_EQ(session.wire_subscriptions().at(wire), level); }
}

// --- the parts of MQTT a naive `unordered_map<string, handler>` gets wrong ---

TEST(typed_subscription_tests, dollar_topics_reach_only_their_literal_subscribers) {
    typed_session session;
    session.subscribe("#", qos::AT_MOST_ONCE, "everything");
    session.subscribe("$SYS/#", qos::AT_MOST_ONCE, "broker stats");

    // MQTT §4.7.2: a leading wildcard never sees a `$` topic.
    EXPECT_EQ(session.handlers_for("$SYS/broker/uptime"), std::vector<std::string>({ "broker stats" }));
    EXPECT_EQ(session.handlers_for("app/temperature"), std::vector<std::string>({ "everything" }));
}

TEST(typed_subscription_tests, binding_a_parameter_narrows_the_wire_subscription) {
    typed_session session;

    // A multi-tenant app binds the tenant away before it ever reaches the broker.
    const auto bound = pattern_of("logs/{service}/{level}/{details:#}")
                           .bind_param("service", "auth")
                           .value()
                           .bind_param("level", "error")
                           .value();
    const auto [id, wire] = session.subscribe(bound, qos::AT_LEAST_ONCE, "auth errors");

    // The registered filter is narrower than the template it came from.
    EXPECT_EQ(wire, "logs/auth/error/#");
    EXPECT_EQ(session.handlers_for("logs/auth/error/handshake/failed"), std::vector<std::string>({ "auth errors" }));
    EXPECT_TRUE(session.handlers_for("logs/db/error/handshake/failed").empty());
    EXPECT_EQ(id.value, 0U);
}

TEST(typed_subscription_tests, building_a_publish_topic_from_the_same_template) {
    // The subscribe side and the publish side share one template, so they cannot drift apart.
    const topic_pattern_path filter = pattern_of("devices/{type}/{id}/command");
    const auto built = filter.format_topic({ "sensor", "42" });
    ASSERT_TRUE(built);
    EXPECT_EQ(built.value(), "devices/sensor/42/command");

    // And the built topic is one the matching filter accepts.
    const auto matched = filter.match_str(built.value());
    ASSERT_TRUE(matched);
    EXPECT_EQ(std::string(*matched.value()->named_param("type")), "sensor");
    EXPECT_EQ(std::string(*matched.value()->named_param("id")), "42");
}

TEST(typed_subscription_tests, a_malformed_template_is_rejected_before_it_reaches_the_broker) {
    // Empty, a misplaced `#`, and a stray `+` inside a level are all refused up front.
    EXPECT_FALSE(topic_pattern_path::parse(""));
    EXPECT_FALSE(topic_pattern_path::parse("sensors/#/data"));
    EXPECT_FALSE(topic_pattern_path::parse("sensors/sen+ors/data"));
    EXPECT_FALSE(topic_pattern_path::parse(std::string("sensors/\0/data", 14)));

    EXPECT_TRUE(std::holds_alternative<hash_position_error>(topic_pattern_path::parse("sensors/#/data").error()));
    EXPECT_TRUE(std::holds_alternative<empty_pattern_error>(topic_pattern_path::parse("").error()));
}

// --- teardown ---

TEST(typed_subscription_tests, unsubscribing_frees_the_broker_pattern_only_at_the_end) {
    typed_session session;
    const auto [shared, wire] = session.subscribe("home/#", qos::AT_MOST_ONCE, "first");
    session.subscribe("home/#", qos::AT_MOST_ONCE, "second");

    std::string dropped;
    EXPECT_TRUE(session.unsubscribe(shared, dropped));
    EXPECT_TRUE(dropped.empty()) << "a handler remains, so the broker keeps the pattern";

    const auto [other, other_wire] = session.subscribe("home/#", qos::AT_MOST_ONCE, "third");
    EXPECT_EQ(other_wire, wire);

    EXPECT_TRUE(session.unsubscribe(other, dropped));
    EXPECT_TRUE(dropped.empty());
    EXPECT_TRUE(session.unsubscribe(subscription_id { 1 }, dropped));
    EXPECT_EQ(dropped, "home/#");
    EXPECT_TRUE(session.wire_subscriptions().empty());
}

TEST(typed_subscription_tests, shutting_down_leaves_nothing_behind) {
    typed_session session;
    session.subscribe("sensors/#", qos::AT_MOST_ONCE, "sensors");
    session.subscribe("$SYS/#", qos::AT_LEAST_ONCE, "stats");

    session.shutdown();

    EXPECT_TRUE(session.active_patterns().empty());
    EXPECT_TRUE(session.wire_subscriptions().empty());
    EXPECT_TRUE(session.resubscribe_plan().empty());
    EXPECT_TRUE(session.handlers_for("sensors/kitchen").empty());
}

} // namespace
} // namespace ba7lya::topic::e2e
