///
/// @file pattern_benchmark.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Benchmarks for the cost of the front end: parsing a template, rendering it, and binding values.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///
// SPDX-License-Identifier: MIT

#include <benchmark/benchmark.h>
#include <cstddef>
#include <string>
#include <vector>

#include "topic.hxx"

namespace ba7lya::topic {
namespace {

using ::benchmark::State;

/// @brief A spread of templates: literal, wildcard, named, and a long one.
const std::vector<std::string>& template_corpus() {
    static const std::vector<std::string> corpus {
        "sensors/temperature",
        "sensors/+/reading",
        "sensors/{room}/reading",
        "home/#",
        "logs/{service}/{level}/{details:#}",
        "a/b/c/d/e/f/g/h/{tail}",
        "$SYS/broker/+",
        "devices/{type}/{id}/command",
    };
    return corpus;
}

/// @brief Parsing a template, which happens once per subscription.
void parse_template(State& state) {
    for (auto _ : state) {
        for (const std::string& text : template_corpus()) {
            auto parsed = topic_pattern_path::parse(text);
            ::benchmark::DoNotOptimize(parsed);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(template_corpus().size()));
}

/// @brief Rendering a parsed template back to its filter text.
void render_template(State& state) {
    std::vector<topic_pattern_path> parsed;
    parsed.reserve(template_corpus().size());
    for (const std::string& text : template_corpus()) { parsed.push_back(topic_pattern_path::parse(text).value()); }

    for (auto _ : state) {
        for (const topic_pattern_path& filter : parsed) { ::benchmark::DoNotOptimize(filter.topic_pattern()); }
    }
}

/// @brief Computing the wire pattern, which is what a broker subscription is keyed by.
void wire_pattern(State& state) {
    std::vector<topic_pattern_path> parsed;
    parsed.reserve(template_corpus().size());
    for (const std::string& text : template_corpus()) { parsed.push_back(topic_pattern_path::parse(text).value()); }

    for (auto _ : state) {
        for (const topic_pattern_path& filter : parsed) { ::benchmark::DoNotOptimize(filter.mqtt_pattern()); }
    }
}

/// @brief Binding a parameter, which returns a fresh filter rather than mutating one.
void bind_parameter(State& state) {
    const topic_pattern_path filter = topic_pattern_path::parse("home/{room}/{device}/temperature").value();

    for (auto _ : state) {
        auto bound = filter.bind_parameter("room", "kitchen");
        ::benchmark::DoNotOptimize(bound);
    }
}

/// @brief Composing a publish topic from a template and its values.
void format_topic(State& state) {
    const topic_pattern_path filter = topic_pattern_path::parse("devices/{type}/{id}/command").value();
    const std::vector<std::string> values { "sensor", "42" };

    for (auto _ : state) {
        auto built = filter.format_topic(values);
        ::benchmark::DoNotOptimize(built);
    }
}

/// @brief Checking two templates for structural compatibility.
void check_compatibility(State& state) {
    const topic_pattern_path left = topic_pattern_path::parse("home/{room}/temperature").value();
    const topic_pattern_path right = topic_pattern_path::parse("home/{other}/temperature").value();

    for (auto _ : state) {
        auto compatible = left.check_compatibility(right);
        ::benchmark::DoNotOptimize(compatible);
    }
}

/// @brief Parsing a topic name, which happens once per delivered message.
void parse_topic(State& state) {
    for (auto _ : state) {
        auto parsed = topic_path::parse("home/kitchen/device001/temperature");
        ::benchmark::DoNotOptimize(parsed);
    }
}

BENCHMARK(parse_template);
BENCHMARK(render_template);
BENCHMARK(wire_pattern);
BENCHMARK(bind_parameter);
BENCHMARK(format_topic);
BENCHMARK(check_compatibility);
BENCHMARK(parse_topic);

} // namespace
} // namespace ba7lya::topic

BENCHMARK_MAIN();
