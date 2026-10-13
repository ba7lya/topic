// SPDX-License-Identifier: MIT

#include "qos.hxx"

namespace ba7lya::topic_engine {

std::string to_string(qos level) {
    switch (level) {
    case qos::AT_MOST_ONCE:  return "QoS0";
    case qos::AT_LEAST_ONCE: return "QoS1";
    case qos::EXACTLY_ONCE:  return "QoS2";
    }
    return "QoS?";
}

} // namespace ba7lya::topic_engine
