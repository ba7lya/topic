// SPDX-License-Identifier: MIT

#include "topic_matcher.hxx"

namespace ba7lya::topic_engine {

std::string invalid_segment_error::message() const {
    return "Invalid topic segment '" + segment + "' at position " + std::to_string(position);
}

topic_matcher_error invalid_segment(std::string segment, std::size_t position) {
    return invalid_segment_error { std::move(segment), position };
}

std::string describe(const topic_matcher_error& error) {
    return std::visit([](const auto& concrete) { return concrete.message(); }, error);
}

} // namespace ba7lya::topic_engine
