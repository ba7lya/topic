///
/// @file qos.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Implementation of the MQTT Quality of Service levels.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///
// SPDX-License-Identifier: MIT

#include "qos.hxx"

namespace ba7lya::topic {

std::string to_string(qos level) {
    switch (level) {
    case qos::AT_MOST_ONCE:  return "QoS0";
    case qos::AT_LEAST_ONCE: return "QoS1";
    case qos::EXACTLY_ONCE:  return "QoS2";
    }
    return "QoS?";
}

} // namespace ba7lya::topic
