///
/// @file router.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Implementation of the subscription router.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///
// SPDX-License-Identifier: MIT

#include "router.hxx"

#include <string>

namespace ba7lya::topic {

std::string subscription_id::to_string() const { return "SubscriptionId(" + std::to_string(value) + ")"; }

std::size_t subscription_id_hash::operator()(const subscription_id& id) const noexcept {
    return std::hash<std::size_t> {}(id.value);
}

} // namespace ba7lya::topic
