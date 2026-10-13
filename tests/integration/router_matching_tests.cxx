// SPDX-License-Identifier: MIT
//
// Integration: the router and the trie working together behind a stand-in broker client.
//
// The point of this suite is the seam between the two: the router decides *when* a broker
// subscription has to be made or dropped, and the trie decides *who* a delivered topic reaches.
// Each case drives both and checks that the decisions agree.

#include <algorithm>
#include <cstddef>
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "test_support.hxx"
#include "topic_engine.hxx"

namespace ba7lya::topic_engine::integration {
namespace {

using test::create_pattern;

/// A message the caller would hand to a real client.
struct published {
    std::string topic;
    std::string payload;
};

/// A broker client that only records what it was asked to do.
///
/// Every real client has this shape: the router says "subscribe this" or "unsubscribe that" and
/// the client turns it into a wire operation. Recording instead of sending keeps the test offline
/// while still exercising the decision logic that precedes the send.
class fake_client {
public:
    /// Records a subscription at `level`, keeping the highest QoS seen for the pattern.
    void subscribe(const std::string& pattern, qos level) {
        subscribe_calls_.push_back(pattern);
        qos& current = subscriptions_[pattern];
        current = std::max(current, level);
    }

    /// Records an unsubscription.
    void unsubscribe(const std::string& pattern) {
        unsubscribe_calls_.push_back(pattern);
        subscriptions_.erase(pattern);
    }

    /// What the broker would be holding right now.
    [[nodiscard]]
    const std::unordered_map<std::string, qos>& subscriptions() const noexcept {
        return subscriptions_;
    }

    /// Every subscribe call, in order, with repeats kept.
    [[nodiscard]]
    const std::vector<std::string>& subscribe_calls() const noexcept {
        return subscribe_calls_;
    }

    /// Every unsubscribe call, in order, with repeats kept.
    [[nodiscard]]
    const std::vector<std::string>& unsubscribe_calls() const noexcept {
        return unsubscribe_calls_;
    }

private:
    std::unordered_map<std::string, qos> subscriptions_;
    std::vector<std::string> subscribe_calls_;
    std::vector<std::string> unsubscribe_calls_;
};

/// A router wired to a client, so the two stay in step the way a real session keeps them.
class session {
public:
    /// Registers a handler and subscribes on the broker only if the router says it must.
    subscription_id subscribe(topic_pattern_path pattern, qos level, std::string handler) {
        const auto [needs_subscribe, id] = router_.add_subscription(std::move(pattern), level, std::move(handler));
        if (needs_subscribe) {
            const auto* record = router_.get_topic_by_id(id).value();
            client_.subscribe(record->first.mqtt_pattern(), record->second);
        }
        return id;
    }

    /// Unsubscribes on the broker only once the pattern has no handlers left.
    bool unsubscribe(subscription_id id) {
        const auto removed = router_.unsubscribe(id);
        if (!removed) { return false; }
        if (removed.value().first) { client_.unsubscribe(removed.value().second.mqtt_pattern()); }
        return true;
    }

    /// Delivers `topic` and returns the payloads of every handler it reaches, in id order.
    [[nodiscard]]
    std::vector<std::string> deliver(const std::string& topic) const {
        const auto parsed = topic_path::parse(topic);
        if (!parsed) { throw std::runtime_error("topic should be valid: " + topic); }

        std::vector<std::pair<std::size_t, std::string>> reached;
        for (const auto& subscriber : router_.get_subscribers(*parsed.value())) {
            reached.emplace_back(subscriber.id.value, *subscriber.payload);
        }
        std::ranges::sort(reached);

        std::vector<std::string> payloads;
        payloads.reserve(reached.size());
        for (auto& [id, payload] : reached) { payloads.push_back(std::move(payload)); }
        return payloads;
    }

    /// The router, for the bookkeeping the test wants to inspect directly.
    [[nodiscard]]
    const topic_router<std::string>& router() const noexcept {
        return router_;
    }

    /// The client, for the same reason.
    [[nodiscard]]
    const fake_client& client() const noexcept {
        return client_;
    }

private:
    topic_router<std::string> router_;
    fake_client client_;
};

// --- the broker view follows the router's decisions ---

TEST(router_client_tests, subscribing_registers_exactly_one_broker_subscription) {
    session current;
    current.subscribe(create_pattern("sensors/{id}/temperature"), qos::AT_LEAST_ONCE, "handler");

    EXPECT_EQ(current.client().subscribe_calls(), std::vector<std::string>({ "sensors/+/temperature" }));
    EXPECT_EQ(current.client().subscriptions().at("sensors/+/temperature"), qos::AT_LEAST_ONCE);
}

TEST(router_client_tests, a_second_handler_on_the_same_pattern_sends_nothing) {
    session current;
    current.subscribe(create_pattern("sensors/+/temperature"), qos::AT_MOST_ONCE, "first");
    current.subscribe(create_pattern("sensors/{id}/temperature"), qos::AT_MOST_ONCE, "second");

    // Same wire pattern, same QoS: the client already has it.
    EXPECT_EQ(current.client().subscribe_calls().size(), 1U);
}

TEST(router_client_tests, raising_the_qos_resubscribes_at_the_higher_level) {
    session current;
    current.subscribe(create_pattern("sensors/+/temperature"), qos::AT_MOST_ONCE, "low");
    current.subscribe(create_pattern("sensors/+/temperature"), qos::EXACTLY_ONCE, "high");

    EXPECT_EQ(current.client().subscribe_calls().size(), 2U);
    EXPECT_EQ(current.client().subscriptions().at("sensors/+/temperature"), qos::EXACTLY_ONCE);
}

TEST(router_client_tests, the_broker_subscription_outlives_a_partial_unsubscribe) {
    session current;
    const subscription_id first = current.subscribe(create_pattern("home/#"), qos::AT_MOST_ONCE, "first");
    current.subscribe(create_pattern("home/#"), qos::AT_MOST_ONCE, "second");

    EXPECT_TRUE(current.unsubscribe(first));
    EXPECT_TRUE(current.client().unsubscribe_calls().empty()) << "a handler is left, so the broker keeps it";
    EXPECT_EQ(current.client().subscriptions().count("home/#"), 1U);
}

TEST(router_client_tests, the_broker_subscription_goes_when_the_last_handler_does) {
    session current;
    const subscription_id first = current.subscribe(create_pattern("home/#"), qos::AT_MOST_ONCE, "first");
    const subscription_id second = current.subscribe(create_pattern("home/#"), qos::AT_MOST_ONCE, "second");

    EXPECT_TRUE(current.unsubscribe(first));
    EXPECT_TRUE(current.unsubscribe(second));
    EXPECT_EQ(current.client().unsubscribe_calls(), std::vector<std::string>({ "home/#" }));
    EXPECT_TRUE(current.client().subscriptions().empty());
}

TEST(router_client_tests, one_wire_pattern_serves_two_templates) {
    session current;
    const subscription_id named = current.subscribe(create_pattern("sensors/{id}/data"), qos::AT_MOST_ONCE, "named");
    const subscription_id anonymous = current.subscribe(create_pattern("sensors/+/data"), qos::AT_MOST_ONCE, "plus");

    // Removing the first still leaves the pattern live, so nothing is sent yet.
    EXPECT_TRUE(current.unsubscribe(named));
    EXPECT_TRUE(current.client().unsubscribe_calls().empty());

    EXPECT_TRUE(current.unsubscribe(anonymous));
    EXPECT_EQ(current.client().unsubscribe_calls(), std::vector<std::string>({ "sensors/+/data" }));
}

TEST(router_client_tests, the_resubscribe_plan_matches_what_the_client_holds) {
    session current;
    current.subscribe(create_pattern("sensors/temperature"), qos::AT_MOST_ONCE, "sensors");
    current.subscribe(create_pattern("home/#"), qos::EXACTLY_ONCE, "home");
    current.subscribe(create_pattern("a/#"), qos::AT_MOST_ONCE, "a");
    current.subscribe(create_pattern("b/#"), qos::AT_MOST_ONCE, "b");
    current.subscribe(create_pattern("c/#"), qos::AT_MOST_ONCE, "c");
    current.subscribe(create_pattern("d/#"), qos::AT_MOST_ONCE, "d");
    current.subscribe(create_pattern("e/#"), qos::AT_MOST_ONCE, "e");

    // A reconnect asks the router what the broker should be holding, and the client agrees.
    EXPECT_EQ(current.router().get_topics_for_resubscribe(), current.client().subscriptions());
}

// --- a delivered topic reaches every handler the filter admits ---

TEST(router_delivery_tests, a_topic_reaches_every_handler_that_matches_it) {
    session current;
    current.subscribe(create_pattern("home/kitchen/temperature"), qos::AT_MOST_ONCE, "kitchen");
    current.subscribe(create_pattern("home/+/temperature"), qos::AT_MOST_ONCE, "any room");
    current.subscribe(create_pattern("home/#"), qos::AT_MOST_ONCE, "all of home");
    current.subscribe(create_pattern("garden/#"), qos::AT_MOST_ONCE, "garden");

    EXPECT_EQ(
        current.deliver("home/kitchen/temperature"), std::vector<std::string>({ "kitchen", "any room", "all of home" })
    );
    EXPECT_EQ(current.deliver("home/hall/temperature"), std::vector<std::string>({ "any room", "all of home" }));
    EXPECT_EQ(current.deliver("garden/lawn"), std::vector<std::string>({ "garden" }));
    EXPECT_TRUE(current.deliver("office/desk").empty());
}

TEST(router_delivery_tests, a_handler_is_reached_through_every_pattern_it_registered) {
    session current;
    const subscription_id first = current.subscribe(create_pattern("sensors/+/reading"), qos::AT_MOST_ONCE, "handler");
    current.subscribe(create_pattern("sensors/#"), qos::AT_MOST_ONCE, "handler");

    // Two registrations match, so the handler is invoked twice, once per registration.
    EXPECT_EQ(current.deliver("sensors/kitchen/reading"), std::vector<std::string>({ "handler", "handler" }));

    EXPECT_TRUE(current.unsubscribe(first));
    EXPECT_EQ(current.deliver("sensors/kitchen/reading"), std::vector<std::string>({ "handler" }));
}

TEST(router_delivery_tests, dollar_topics_reach_only_their_literal_subscribers) {
    session current;
    current.subscribe(create_pattern("#"), qos::AT_MOST_ONCE, "everything");
    current.subscribe(create_pattern("+/broker/+"), qos::AT_MOST_ONCE, "any broker");
    current.subscribe(create_pattern("$SYS/#"), qos::AT_MOST_ONCE, "sys");

    EXPECT_EQ(current.deliver("$SYS/broker/uptime"), std::vector<std::string>({ "sys" }));
    EXPECT_EQ(current.deliver("my/broker/uptime"), std::vector<std::string>({ "everything", "any broker" }));
}

TEST(router_delivery_tests, a_bound_parameter_narrows_delivery_to_one_topic) {
    session current;
    const auto bound = create_pattern("sensors/{id}/data").bind_parameter("id", "kitchen");
    ASSERT_TRUE(bound);
    current.subscribe(bound.value(), qos::AT_MOST_ONCE, "kitchen only");

    EXPECT_EQ(current.deliver("sensors/kitchen/data"), std::vector<std::string>({ "kitchen only" }));
    EXPECT_TRUE(current.deliver("sensors/hall/data").empty());
}

TEST(router_delivery_tests, cleanup_leaves_the_router_serving_nothing) {
    // A router owns a trie, so it moves but never copies; the shutdown path is exercised directly.
    topic_router<std::string> drained;
    (void)drained.add_subscription(create_pattern("home/#"), qos::AT_MOST_ONCE, "home");

    drained.cleanup();

    EXPECT_TRUE(drained.get_active_subscriptions().empty());
    EXPECT_TRUE(drained.get_topics_for_resubscribe().empty());
    EXPECT_TRUE(drained.get_topics_for_unsubscribe().empty());
}

// --- a publish/subscribe round trip ---

TEST(router_round_trip_tests, a_published_message_reaches_the_right_handlers) {
    session current;
    const subscription_id kitchen = current.subscribe(create_pattern("home/kitchen/+"), qos::AT_MOST_ONCE, "kitchen");
    current.subscribe(create_pattern("home/+/humidity"), qos::AT_MOST_ONCE, "humidity");

    const std::vector<published> published_messages {
        { "home/kitchen/humidity",    "55"   },
        { "home/kitchen/temperature", "21.5" },
        { "home/hall/humidity",       "60"   }
    };

    std::map<std::string, std::vector<std::string>> received;
    for (const published& message : published_messages) { received[message.topic] = current.deliver(message.topic); }

    EXPECT_EQ(received.at("home/kitchen/humidity"), std::vector<std::string>({ "kitchen", "humidity" }));
    EXPECT_EQ(received.at("home/kitchen/temperature"), std::vector<std::string>({ "kitchen" }));
    EXPECT_EQ(received.at("home/hall/humidity"), std::vector<std::string>({ "humidity" }));

    // After the kitchen handler leaves, its pattern is only half empty, so the broker keeps it.
    EXPECT_TRUE(current.unsubscribe(kitchen));
    EXPECT_TRUE(current.client().unsubscribe_calls().empty());
    EXPECT_EQ(current.deliver("home/kitchen/humidity"), std::vector<std::string>({ "humidity" }));
    EXPECT_TRUE(current.deliver("home/kitchen/temperature").empty());
}

TEST(router_round_trip_tests, the_wire_pattern_survives_a_full_registration_cycle) {
    session current;
    const auto bound = create_pattern("logs/{service}/{level}/{details:#}")
                           .bind_parameter("service", "auth")
                           .value()
                           .bind_parameter("level", "error")
                           .value();
    const subscription_id id = current.subscribe(bound, qos::AT_LEAST_ONCE, "auth errors");

    EXPECT_EQ(current.client().subscribe_calls(), std::vector<std::string>({ "logs/auth/error/#" }));
    EXPECT_EQ(current.deliver("logs/auth/error/handshake/failed"), std::vector<std::string>({ "auth errors" }));
    EXPECT_TRUE(current.deliver("logs/db/error/handshake/failed").empty());

    EXPECT_TRUE(current.unsubscribe(id));
    EXPECT_EQ(current.client().unsubscribe_calls(), std::vector<std::string>({ "logs/auth/error/#" }));
}

} // namespace
} // namespace ba7lya::topic_engine::integration
