///
/// @file error.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Implementation of every error message and error factory the engine raises.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///
// SPDX-License-Identifier: MIT

#include "error.hxx"

#include <cstddef>
#include <string>
#include <utility>
#include <variant>

namespace ba7lya::topic {

topic_matcher_error invalid_segment(std::string segment, std::size_t position) {
    return invalid_segment_error { std::move(segment), position };
}

router_error subscription_not_found(subscription_id id) { return subscription_not_found_error { id }; }

router_error invalid_routing_topic(std::string topic, std::string reason) {
    return invalid_routing_topic_error { std::move(topic), std::move(reason) };
}

router_error internal_state_corrupted(std::string details) {
    return internal_state_corrupted_error { std::move(details) };
}

} // namespace ba7lya::topic
