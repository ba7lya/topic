// SPDX-License-Identifier: MIT

#include "topic_router.hxx"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace ba7lya::topic_engine {

std::string subscription_id::to_string() const { return "SubscriptionId(" + std::to_string(value) + ")"; }

std::size_t subscription_id_hash::operator()(const subscription_id& id) const noexcept {
    return std::hash<std::size_t> {}(id.value);
}

std::string invalid_pattern_error::message() const { return "Invalid topic pattern: " + describe(cause); }

std::string matching_failed_error::message() const { return "Topic matching failed: " + describe(cause); }

std::string subscription_not_found_error::message() const { return "Subscription " + id.to_string() + " not found"; }

std::string invalid_routing_topic_error::message() const {
    return "Topic '" + topic + "' is invalid for routing: " + reason;
}

std::string internal_state_corrupted_error::message() const { return "Internal routing state corrupted: " + details; }

topic_router_error make_invalid_pattern_error(const topic_pattern_error& cause) {
    return invalid_pattern_error { cause };
}

topic_router_error make_matching_failed_error(const topic_matcher_error& cause) {
    return matching_failed_error { cause };
}

topic_router_error subscription_not_found(subscription_id id) { return subscription_not_found_error { id }; }

topic_router_error invalid_routing_topic(std::string topic, std::string reason) {
    return invalid_routing_topic_error { std::move(topic), std::move(reason) };
}

topic_router_error internal_state_corrupted(std::string details) {
    return internal_state_corrupted_error { std::move(details) };
}

std::string describe(const topic_router_error& error) {
    return std::visit([](const auto& concrete) { return concrete.message(); }, error);
}

} // namespace ba7lya::topic_engine
