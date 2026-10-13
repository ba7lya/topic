// SPDX-License-Identifier: MIT

#include "error.hxx"

namespace ba7lya::topic_engine {

std::string pattern_stage_error::message() const { return "Topic pattern error: " + describe(cause); }

std::string format_stage_error::message() const { return "Topic format error: " + describe(cause); }

#ifndef BA7LYA_TOPIC_ENGINE_NO_ROUTER
std::string matcher_stage_error::message() const { return "Topic matcher error: " + describe(cause); }

std::string router_stage_error::message() const { return "Topic router error: " + describe(cause); }
#endif

std::string describe(const topic_error& error) {
    return std::visit([](const auto& concrete) { return concrete.message(); }, error);
}

} // namespace ba7lya::topic_engine
