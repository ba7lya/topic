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

std::string empty_topic_path_error::message() const { return "Topic cannot be empty"; }

std::string null_char_topic_path_error::message() const { return "Topic must not contain the null character (U+0000)"; }

topic_path_error make_topic_path_error(const malformed& cause) {
    if (std::holds_alternative<null_char_error>(cause)) { return null_char_topic_path_error {}; }
    return empty_topic_path_error {};
}

std::string describe(const topic_path_error& error) {
    return std::visit([](const auto& concrete) { return concrete.message(); }, error);
}

std::string hash_position_error::message() const {
    return "Invalid topic pattern '" + pattern + "': # wildcard can only be the last segment";
}

std::string wildcard_usage_error::message() const { return "Invalid wildcard usage: " + usage; }

std::string empty_pattern_error::message() const { return "Topic pattern cannot be empty"; }

std::string null_char_pattern_error::message() const {
    return "Topic pattern must not contain the null character (U+0000)";
}

std::string pattern_structure_mismatch_error::message() const {
    return "Topic pattern structure mismatch.\nOriginal: '" + original + "'\nCustom:   '" + custom
         + "'\nHint: Both patterns must have the same parameter structure (same wildcards in same positions)";
}

topic_pattern_error make_hash_position_error(std::string pattern) { return hash_position_error { std::move(pattern) }; }

topic_pattern_error make_wildcard_usage_error(std::string usage) { return wildcard_usage_error { std::move(usage) }; }

topic_pattern_error make_pattern_mismatch_error(std::string original, std::string custom) {
    return pattern_structure_mismatch_error { std::move(original), std::move(custom) };
}

topic_pattern_error make_topic_pattern_error(const malformed& cause) {
    if (std::holds_alternative<null_char_error>(cause)) { return null_char_pattern_error {}; }
    return empty_pattern_error {};
}

std::string describe(const topic_pattern_error& error) {
    return std::visit([](const auto& concrete) { return concrete.message(); }, error);
}

std::string hash_wildcard_not_supported_error::message() const {
    return "Cannot format topic with # wildcard for publishing";
}

std::string parameter_count_mismatch_error::message() const {
    return "Parameter count mismatch: expected " + std::to_string(expected) + ", provided " + std::to_string(provided);
}

std::string topic_format_error::message() const { return "Error formatting topic"; }

std::string describe(const topic_format_failure& error) {
    return std::visit([](const auto& concrete) { return concrete.message(); }, error);
}

std::string unexpected_end_of_pattern_error::message() const {
    return "Pattern ended unexpectedly while matching topic";
}

std::string unexpected_end_of_topic_error::message() const { return "Topic ended unexpectedly while matching pattern"; }

std::string unexpected_hash_segment_error::message() const { return "Hash wildcard (#) found in unexpected position"; }

std::string segment_mismatch_error::message() const {
    return "Segment mismatch at position " + std::to_string(position) + ": expected '" + expected + "', found '" + found
         + "'";
}

std::string duplicate_parameter_name_error::message() const { return "Duplicate parameter name found in pattern"; }

std::string dollar_topic_exclusion_error::message() const {
    return "Topic filter with a leading wildcard cannot match reserved $-topic '" + topic + "' (MQTT §4.7.2)";
}

std::string describe(const topic_match_error& error) {
    return std::visit([](const auto& concrete) { return concrete.message(); }, error);
}

std::string invalid_segment_error::message() const {
    return "Invalid topic segment '" + segment + "' at position " + std::to_string(position);
}

topic_matcher_error invalid_segment(std::string segment, std::size_t position) {
    return invalid_segment_error { std::move(segment), position };
}

std::string describe(const topic_matcher_error& error) {
    return std::visit([](const auto& concrete) { return concrete.message(); }, error);
}

std::string invalid_pattern_error::message() const { return "Invalid topic pattern: " + describe(cause); }

std::string matching_failed_error::message() const { return "Topic matching failed: " + describe(cause); }

std::string subscription_not_found_error::message() const { return "Subscription " + id.to_string() + " not found"; }

std::string invalid_routing_topic_error::message() const {
    return "Topic '" + topic + "' is invalid for routing: " + reason;
}

std::string internal_state_corrupted_error::message() const { return "Internal routing state corrupted: " + details; }

router_error make_invalid_pattern_error(const topic_pattern_error& cause) { return invalid_pattern_error { cause }; }

router_error make_matching_failed_error(const topic_matcher_error& cause) { return matching_failed_error { cause }; }

router_error subscription_not_found(subscription_id id) { return subscription_not_found_error { id }; }

router_error invalid_routing_topic(std::string topic, std::string reason) {
    return invalid_routing_topic_error { std::move(topic), std::move(reason) };
}

router_error internal_state_corrupted(std::string details) {
    return internal_state_corrupted_error { std::move(details) };
}

std::string describe(const router_error& error) {
    return std::visit([](const auto& concrete) { return concrete.message(); }, error);
}

std::string pattern_stage_error::message() const { return "Topic pattern error: " + describe(cause); }

std::string format_stage_error::message() const { return "Topic format error: " + describe(cause); }

std::string matcher_stage_error::message() const { return "Topic matcher error: " + describe(cause); }

std::string router_stage_error::message() const { return "Topic router error: " + describe(cause); }

std::string describe(const topic_error& error) {
    return std::visit([](const auto& concrete) { return concrete.message(); }, error);
}

} // namespace ba7lya::topic
