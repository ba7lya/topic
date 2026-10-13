// SPDX-License-Identifier: MIT
//
// Example: pattern parsing, matching and parameter extraction, without a router.
//
// Build and run:
//
//     cmake --preset win-msvc-2022-x64 -Dba7lya.topic-engine_BUILD_EXAMPLE=ON
//     cmake --build --preset win-msvc-2022-x64-dbg
//     ./build/win-msvc-2022-x64/bin/Debug/topic_engine_example_basic_matching

#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "topic_engine.hxx"

namespace {

/// One demo filter and the topics to try it against.
struct demo {
    std::string filter;
    std::vector<std::string> topics;
};

/// Renders the captures of a match as `  [room=kitchen]`, or nothing when there are none.
[[nodiscard]]
std::string describe_captures(const ba7lya::topic_engine::topic_match& matched) {
    if (matched.param_count() == 0) { return {}; }

    std::string rendered = "  [";
    for (std::size_t index = 0; index < matched.param_count(); ++index) {
        if (index != 0) { rendered += ", "; }
        rendered += "param" + std::to_string(index) + "=" + std::string(matched.param(index));
    }
    return rendered + ']';
}

/// Prints every filter's verdict for every topic it is given.
void show_matching(const std::vector<demo>& demos) {
    for (const demo& entry : demos) {
        const auto filter = ba7lya::topic_engine::topic_pattern_path::parse(entry.filter);
        if (!filter) {
            std::cout << "  " << entry.filter << " -> rejected: " << describe(filter.error()) << '\n';
            continue;
        }

        std::cout << "  " << entry.filter << "  (wire: " << filter.value().mqtt_pattern() << ")\n";
        for (const std::string& topic : entry.topics) {
            const auto matched = filter.value().match_str(topic);
            if (!matched) {
                const auto* excluded
                    = std::get_if<ba7lya::topic_engine::dollar_topic_exclusion_error>(&matched.error());
                const std::string why = excluded == nullptr ? describe(matched.error()) : "excluded ($ topic)";
                std::cout << "    " << topic << " -> no (" << why << ")\n";
                continue;
            }
            std::cout << "    " << topic << " -> yes" << describe_captures(*matched.value()) << '\n';
        }
    }
}

/// Looks a named capture up and prints it the way an application would.
void show_named_capture(const std::string& filter_text, const std::string& topic, const std::string& name) {
    const auto filter = ba7lya::topic_engine::topic_pattern_path::parse(filter_text);
    if (!filter) { return; }

    const auto matched = filter.value().match_str(topic);
    if (!matched) { return; }

    const std::optional<std::string_view> value = matched.value()->named_param(name);
    std::cout << "    " << name << " = " << (value.has_value() ? std::string(*value) : std::string("<absent>")) << '\n';
}

/// Shows building a publish topic from the same template that subscribes to it.
void show_publishing() {
    const auto filter = ba7lya::topic_engine::topic_pattern_path::parse("devices/{type}/{id}/command");
    if (!filter) { return; }

    const auto built = filter.value().format_topic({ "sensor", "42" });
    if (!built) {
        std::cout << "  format_topic failed: " << describe(built.error()) << '\n';
        return;
    }
    std::cout << "  template   : " << filter.value().topic_pattern() << '\n';
    std::cout << "  published  : " << built.value() << '\n';

    const auto wrong_count = filter.value().format_topic({ "sensor" });
    if (!wrong_count) { std::cout << "  one value  : " << describe(wrong_count.error()) << '\n'; }
}

/// Shows narrowing a template by binding parameters, which is how a filtered subscription is made.
void show_binding() {
    const auto filter = ba7lya::topic_engine::topic_pattern_path::parse("sensors/{location}/{kind}/{id}/data");
    if (!filter) { return; }

    const auto filtered
        = filter.value().bind_parameter("location", "kitchen").value().bind_parameter("kind", "temperature").value();
    std::cout << "  template   : " << filter.value().topic_pattern() << '\n';
    std::cout << "  filtered   : " << filtered.mqtt_pattern() << '\n';
    std::cout << "  bound value: " << filtered.bound_value("location").value_or("<unbound>") << '\n';
}

/// Shows which templates the parser refuses, and why.
void show_rejections() {
    for (const std::string& text :
         { std::string(""), std::string("sensors/#/data"), std::string("sensors/sen+ors/data") }) {
        const auto rejected = ba7lya::topic_engine::topic_pattern_path::parse(text);
        if (!rejected) { std::cout << "  \"" << text << "\" -> " << describe(rejected.error()) << '\n'; }
    }
}

} // namespace

int main() {
    using ba7lya::topic_engine::topic_pattern_path;

    std::cout << "\n=== matching ===\n";
    show_matching(
        {
            { "sensors/temperature",     { "sensors/temperature", "sensors/humidity" }    },
            { "sensors/+/reading",       { "sensors/kitchen/reading", "sensors/reading" } },
            { "sensors/#",               { "sensors", "sensors/kitchen/reading" }         },
            { "sensors/{room}/reading",  { "sensors/kitchen/reading" }                    },
            { "logs/{service}/{rest:#}", { "logs/api/v1/users", "logs/api" }              },
            { "#",                       { "anything/at/all", "$SYS/broker/uptime" }      },
            { "$SYS/#",                  { "$SYS/broker/uptime" }                         },
    }
    );

    std::cout << "\n=== named captures ===\n";
    show_named_capture("sensors/{location}/{device}/status", "sensors/kitchen/temp001/status", "location");
    show_named_capture("sensors/{location}/{device}/status", "sensors/kitchen/temp001/status", "device");
    show_named_capture("logs/{service}/{details:#}", "logs/api/v1/users/create", "details");

    std::cout << "\n=== publishing ===\n";
    show_publishing();

    std::cout << "\n=== binding parameters ===\n";
    show_binding();

    std::cout << "\n=== rejected templates ===\n";
    show_rejections();
    std::cout << '\n';

    // Matching one topic against several filters: the topic path is parsed once and shared.
    const auto topic = ba7lya::topic_engine::topic_path::parse("home/kitchen/temperature");
    if (topic) {
        std::cout << "=== one topic, several filters ===\n";
        const auto& shared_topic = topic.value();
        for (const char* text : { "home/+/temperature", "home/{room}/temperature", "home/#" }) {
            const auto filter = topic_pattern_path::parse(text);
            if (!filter) { continue; }

            const auto matched = filter.value().match(shared_topic);
            std::cout << "  " << text << " -> " << (matched ? "match" : "no match") << '\n';
        }
        std::cout << '\n';
    }
}
