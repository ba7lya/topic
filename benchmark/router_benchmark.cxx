// SPDX-License-Identifier: MIT
//
// Benchmarks: the subscription table — registering filters, resolving a delivered topic through the
// trie, and the bookkeeping a reconnect reads.

#include <benchmark/benchmark.h>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "topic_engine.hxx"

namespace ba7lya::topic_engine {
namespace {

using ::benchmark::State;

/// The payload a subscription carries. `empty()` satisfies the trie's `payload_container` concept;
/// a named handler is never empty, so no node is pruned early.
struct handler {
    std::string name;

    [[nodiscard]]
    bool empty() const noexcept {
        return name.empty();
    }
};

/// A spread of filters, one per shape the trie has to distinguish: literal, `+`, named, and `#`.
const std::vector<std::string>& filter_corpus() {
    static const std::vector<std::string> corpus {
        "sensors/temperature",
        "sensors/+/reading",
        "sensors/{room}/reading",
        "home/#",
        "logs/{service}/{level}/{details:#}",
        "devices/{type}/{id}/command",
    };
    return corpus;
}

/// Builds a router holding `count` registrations that all share one wire pattern.
[[nodiscard]]
topic_router<handler> router_with_shared_pattern(std::size_t count) {
    topic_router<handler> router;
    for (std::size_t index = 0; index < count; ++index) {
        const auto parsed = topic_pattern_path::parse("sensors/{id}/reading");
        const auto registered
            = router.add_subscription(parsed.value(), qos::AT_MOST_ONCE, handler { "handler" + std::to_string(index) });
        ::benchmark::DoNotOptimize(registered);
    }
    return router;
}

/// Registering distinct filters, which is what a start-up or reconnect pass does.
void add_subscription(State& state) {
    for (auto _ : state) {
        topic_router<handler> router;
        for (const std::string& text : filter_corpus()) {
            const auto parsed = topic_pattern_path::parse(text);
            ::benchmark::DoNotOptimize(router.add_subscription(parsed.value(), qos::AT_LEAST_ONCE, handler { text }));
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(filter_corpus().size()));
}

/// Registering N handlers on one pattern, the case where the QoS scan walks the table.
void add_subscription_repeated(State& state) {
    for (auto size : state) {
        const auto parsed = topic_pattern_path::parse("sensors/{id}/reading");

        for (auto _ : state) {
            topic_router<handler> router;
            for (std::size_t index = 0; index < static_cast<std::size_t>(size); ++index) {
                ::benchmark::DoNotOptimize(
                    router.add_subscription(parsed.value(), qos::AT_MOST_ONCE, handler { "handler" })
                );
            }
        }
        state.SetItemsProcessed(state.iterations() * size);
    }
}

/// Resolving a delivered topic against every stored filter, which is the per-message cost.
void get_subscribers(State& state) {
    topic_router<handler> router;
    for (const std::string& text : filter_corpus()) {
        const auto parsed = topic_pattern_path::parse(text);
        const auto registered = router.add_subscription(parsed.value(), qos::AT_LEAST_ONCE, handler { text });
        ::benchmark::DoNotOptimize(registered);
    }

    const auto topic = topic_path::parse("sensors/device001/reading");

    for (auto _ : state) {
        auto subscribers = router.get_subscribers(*topic.value());
        ::benchmark::DoNotOptimize(subscribers);
    }
}

/// Resolving a topic that reaches nothing, so the walk still visits every branch.
void get_subscribers_miss(State& state) {
    topic_router<handler> router;
    for (const std::string& text : filter_corpus()) {
        const auto parsed = topic_pattern_path::parse(text);
        const auto registered = router.add_subscription(parsed.value(), qos::AT_LEAST_ONCE, handler { text });
        ::benchmark::DoNotOptimize(registered);
    }

    const auto topic = topic_path::parse("garden/lawn/moisture");

    for (auto _ : state) {
        auto subscribers = router.get_subscribers(*topic.value());
        ::benchmark::DoNotOptimize(subscribers);
    }
}

/// Removing registrations, and with them the broker patterns they were holding up.
void unsubscribe(State& state) {
    for (auto _ : state) {
        topic_router<handler> router;
        std::vector<subscription_id> ids;
        for (const std::string& text : filter_corpus()) {
            const auto parsed = topic_pattern_path::parse(text);
            ids.push_back(router.add_subscription(parsed.value(), qos::AT_LEAST_ONCE, handler { text }).second);
        }

        for (const subscription_id id : ids) {
            auto removed = router.unsubscribe(id);
            ::benchmark::DoNotOptimize(removed);
        }
    }
}

/// Reading the reconnect plan, which folds the registry down to distinct wire patterns.
void resubscribe_plan(State& state) {
    const topic_router<handler> router = router_with_shared_pattern(64);

    for (auto _ : state) {
        auto plan = router.get_topics_for_resubscribe();
        ::benchmark::DoNotOptimize(plan);
    }
}

BENCHMARK(add_subscription);
BENCHMARK(add_subscription_repeated)->Range(1, 1024);
BENCHMARK(get_subscribers);
BENCHMARK(get_subscribers_miss);
BENCHMARK(unsubscribe);
BENCHMARK(resubscribe_plan);

} // namespace
} // namespace ba7lya::topic_engine

BENCHMARK_MAIN();
