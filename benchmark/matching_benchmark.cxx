///
/// @file matching_benchmark.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Benchmarks for the per-message path: matching one topic name against filters, parameter extraction, and what
/// the LRU cache buys.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///
// SPDX-License-Identifier: MIT

#include <benchmark/benchmark.h>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "topic.hxx"

namespace ba7lya::topic {
namespace {

using ::benchmark::State;

///
/// @brief A parse-once topic, the shape a caller uses when matching against several filters.
///
struct topic_fixture {
    std::string text;
    std::shared_ptr<const topic_path> path;
};

/// @brief A spread of topics with their matching filters.
const std::vector<topic_fixture>& topic_corpus() {
    static const std::vector<topic_fixture> corpus = [] {
        std::vector<topic_fixture> built;
        for (const char* text : { "home/kitchen/temperature", "sensors/device001/reading", "logs/api/v1/users/create",
                                  "devices/sensor/42/command" }) {
            auto parsed = topic_path::parse(text);
            built.push_back(topic_fixture { text, parsed ? parsed.value() : nullptr });
        }
        return built;
    }();
    return corpus;
}

/// @brief Matching a topic against a filter that captures nothing.
void match_literal(State& state) {
    const topic_pattern_path filter = topic_pattern_path::parse("sensors/+/reading").value();

    for (auto _ : state) {
        for (const topic_fixture& entry : topic_corpus()) {
            auto matched = filter.match(entry.path);
            ::benchmark::DoNotOptimize(matched);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(topic_corpus().size()));
}

/// @brief Matching a topic against a filter that captures by name.
void match_named(State& state) {
    const topic_pattern_path filter = topic_pattern_path::parse("sensors/{id}/reading").value();

    for (auto _ : state) {
        for (const topic_fixture& entry : topic_corpus()) {
            auto matched = filter.match(entry.path);
            ::benchmark::DoNotOptimize(matched);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(topic_corpus().size()));
}

/// @brief Matching a topic against a trailing `#`, which walks every remaining level.
void match_hash(State& state) {
    const topic_pattern_path filter = topic_pattern_path::parse("logs/{service}/{rest:#}").value();

    for (auto _ : state) {
        for (const topic_fixture& entry : topic_corpus()) {
            auto matched = filter.match(entry.path);
            ::benchmark::DoNotOptimize(matched);
        }
    }
}

/// @brief The `$`-topic guard, which rejects before any level is compared.
void match_dollar_excluded(State& state) {
    const topic_pattern_path filter = topic_pattern_path::parse("#").value();
    const auto sys_topic = topic_path::parse("$SYS/broker/uptime");

    for (auto _ : state) {
        auto matched = filter.match(sys_topic.value());
        ::benchmark::DoNotOptimize(matched);
    }
}

/// @brief Re-matching one topic against one filter, cached versus uncached.
/// @details The cache is keyed by topic text, so a hot key is the case it exists for.
void match_cached(State& state) {
    for (const bool cached : { false, true }) {
        const topic_pattern_path filter
            = cached ? topic_pattern_path::parse("sensors/{id}/reading", cache_strategy::lru(256)).value()
                     : topic_pattern_path::parse("sensors/{id}/reading", cache_strategy::no_cache()).value();

        for (auto _ : state) {
            auto matched = filter.match_str("sensors/device001/reading");
            ::benchmark::DoNotOptimize(matched);
        }
    }
}

/// @brief Reading a named capture out of an already-computed match.
void read_named_capture(State& state) {
    const topic_pattern_path filter = topic_pattern_path::parse("logs/{service}/{rest:#}").value();
    const auto matched = filter.match_str("logs/api/v1/users/create");

    for (auto _ : state) {
        ::benchmark::DoNotOptimize(matched.value()->named_param("service"));
        ::benchmark::DoNotOptimize(matched.value()->named_param("rest"));
    }
}

/// @brief Constructing a topic path, once per delivered message.
void construct_topic_path(State& state) {
    for (auto _ : state) {
        auto parsed = topic_path::parse("home/kitchen/device001/temperature");
        ::benchmark::DoNotOptimize(parsed);
    }
}

BENCHMARK(match_literal);
BENCHMARK(match_named);
BENCHMARK(match_hash);
BENCHMARK(match_dollar_excluded);
BENCHMARK(match_cached);
BENCHMARK(read_named_capture);
BENCHMARK(construct_topic_path);

} // namespace
} // namespace ba7lya::topic

BENCHMARK_MAIN();
