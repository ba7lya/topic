# MQTT Topic Engine

MQTT topic matching, filtering and subscription routing in modern C++23.

`ba7lya.topic` is a C++ port of the Rust crate
[`mqtt-topic-engine`](https://github.com/holovskyi/mqtt-typed-client/tree/main/mqtt-topic-engine)from the `mqtt-typed-client` project. It parses MQTT topic filters — wildcards, named parameters and all — matches concrete topic names against them, and routes a delivered topic to every subscription that wants it, tracking on the side which broker subscriptions that implies.

The engine is MQTT-client agnostic: it never touches a socket. A client library (Paho, mosquitto, nng, …) owns the connection, and the engine tells it when a subscription has to be made or dropped.

## Contents

- [Features](#features)
- [Requirements](#requirements)
- [Installation](#installation)
  - [Feature flags](#feature-flags)
- [Quick start](#quick-start)
  - [Pattern matching](#pattern-matching)
  - [Named parameters](#named-parameters)
  - [Multi-level wildcards](#multi-level-wildcards)
- [Core concepts](#core-concepts)
  - [Topic filters](#topic-filters)
  - [The router](#the-router)
  - [Parameter binding](#parameter-binding)
- [Advanced usage](#advanced-usage)
  - [LRU caching](#lru-caching)
  - [Publishing topics](#publishing-topics)
  - [QoS aggregation](#qos-aggregation)
  - [Reconnecting after a broker restart](#reconnecting-after-a-broker-restart)
- [Configuration](#configuration)
- [Performance](#performance)
- [Building from source](#building-from-source)
- [License](#license)

## Features

- **Pattern parsing** — MQTT filters with `+` and `#` wildcards, plus named parameters (`{sensor_id}`, `{path:#}`).
- **Fast matching** — a prefix tree keyed by topic levels; a match costs O(n) in the number of levels, and matching one topic against many filters parses the topic once.
- **Optional caching** — an LRU cache of match results, behind the `LRU_CACHE` option.
- **Message routing** — `router<T>` maps patterns to caller payloads and resolves a delivered topic to every payload that wants it, behind the `ROUTER` option.
- **Broker bookkeeping** — the router reports exactly when a subscribe or unsubscribe has to reach the wire, and aggregates the effective QoS per pattern.
- **Spec-correct edge cases** — the §4.7.2 exclusion that keeps a leading wildcard away from `$SYS` topics, and the §4.7.3 well-formedness rules.
- **Value-based errors** — no exceptions on the API surface; every fallible call returns a `result<T, E>` holding the value or a describing error struct.
- **Modern C++** — concepts instead of type erasure, `constexpr` where it applies, Rule of Zero, no owning raw pointers.
- **MQTT-client agnostic** — no dependency on any client library; `spdlog` is the only runtime dependency, and it is used for diagnostics only.

## Requirements

- A C++23 compiler: MSVC 14.44 (VS2022) or newer, GCC 14+, or Clang 21+.
- CMake 3.21 or newer.
- [vcpkg](https://github.com/microsoft/vcpkg) in manifest mode, for `spdlog`.

Note for MSVC 14.29 (VS2019): the toolchain is supported and covered by CI, but it has no `std::expected`, so the library ships its own [`result<T, E>`](include/result.hxx) instead.

## Installation

Consume the library with `find_package`:

```cmake
find_package(ba7lya.topic CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE ba7lya::topic)
```

The installed headers nest under `include/ba7lya/topic/`, so the package coexists in one install prefix with other packages from the same author. In the source tree the headers are flat, and in-tree consumers include them by bare filename:

```cpp
#include "topic.hxx"
```

### Feature flags

Two CMake options mirror the Rust crate's default-on features:

| Option                                | Default | Effect when `OFF`                                           |
| ------------------------------------- | ------- | ----------------------------------------------------------- |
| `ba7lya.topic_ROUTER`          | `ON`    | Drops the prefix-tree matcher and the router; defines `BA7LYA_TOPIC_NO_ROUTER`. |
| `ba7lya.topic_LRU_CACHE`       | `ON`    | Drops the match cache; every strategy becomes `no_cache()`; defines `BA7LYA_TOPIC_NO_LRU_CACHE`. |

Both are `PUBLIC` compile definitions, so the macros travel with the target. They only ever subtract — a consumer compiled against a feature-off build cannot accidentally reach the code.

A patterns-only build, for a device that needs no routing:

```sh
cmake --preset linux-gcc-x64 -Dba7lya.topic_ROUTER=OFF
```

## Quick start

The umbrella header brings in everything:

```cpp
#include "topic.hxx"

using namespace ba7lya::topic;
```

### Pattern matching

```cpp
const auto filter = topic_pattern_path::parse("sensors/+/temperature");
if (!filter) {
    std::cerr << describe(filter.error()) << '\n';
    return 1;
}

const auto matched = filter.value().match_str("sensors/kitchen/temperature");
if (matched) {
    std::cout << matched.value()->topic_name() << '\n'; // sensors/kitchen/temperature
    std::cout << matched.value()->param(0) << '\n';     // kitchen
}
```

When one topic is matched against several filters — the hot path for a delivery handler — parse the topic once and share it:

```cpp
const auto topic = topic_path::parse("home/kitchen/temperature");
if (topic) {
    for (const char* text : { "home/+/temperature", "home/{room}/temperature", "home/#" }) {
        const auto filter = topic_pattern_path::parse(text);
        if (filter && filter.value().match(topic.value())) { /* ... */ }
    }
}
```

### Named parameters

`{name}` is a `+` that remembers what it captured; `{name:#}` is a `#` that does the same:

```cpp
const auto filter = topic_pattern_path::parse("logs/{service}/{details:#}").value();
const auto matched = filter.match_str("logs/api/v1/users/create").value();

std::cout << matched->named_param("service").value() << '\n';  // api
std::cout << matched->named_param("details").value() << '\n';  // v1/users/create
```

Captures are readable by position (`param(index)`, in filter order) as well as by name.

### Multi-level wildcards

`#` matches the remaining levels and is valid only as the last level; `+` matches exactly one level. Both are subject to the §4.7.2 rule described under [topic filters](#topic-filters).

```cpp
const auto filter = topic_pattern_path::parse("home/#").value();

filter.match_str("home");                     // match, zero remaining levels
filter.match_str("home/kitchen/temperature"); // match, three captured
filter.match_str("garden/lawn");              // no match, first level differs

topic_pattern_path::parse("home/#/temperature");  // rejected: '#' is not last
```

## Core concepts

### Topic filters

A filter is stored three ways, and it helps to know which is which:

| Spelling                 | Example                | Where it is used                                |
| ------------------------ | ---------------------- | ----------------------------------------------- |
| Template                 | `sensors/{id}/data`    | what the caller typed; survives a round trip     |
| Subscription pattern     | `sensors/+/data`       | the text handed to the broker, wildcards anonymous |
| Bound pattern            | `sensors/kitchen/data` | the subscription pattern with bindings substituted |

`parse` validates as it builds. It rejects an empty filter, a filter containing U+0000, an empty or duplicated parameter name, and a `#` anywhere but the last level. A filter containing a stray `+` or `#` inside a level — `sensors/sen+ors/data` — is a literal, not a wildcard.

Two figures from the MQTT specification are implemented rather than assumed:

- **§4.7.3 well-formedness.** A topic name or filter must be non-empty and must not contain the null character. Spaces and the legal-but-discouraged U+0001..U+001F range are accepted. The 65535-byte ceiling is a wire-codec concern and is deliberately not enforced here.
- **§4.7.2 `$` exclusion.** A filter whose first level is an *unbound* wildcard must not match a topic name whose first level begins with `$`. A literal `$SYS/...` filter still matches, and a parameter bound to a `$`-prefixed value counts as a literal. The guard sits strictly at level 0.

```cpp
topic_pattern_path::parse("#").value().match_str("$SYS/broker/uptime");   // no match, excluded
topic_pattern_path::parse("$SYS/#").value().match_str("$SYS/broker/uptime"); // match
```

### The router

`router<T>` is the subscription table. Each registration gets a `subscription_id` and a payload of the caller's type; a delivered topic resolves to every payload that matches it.

```cpp
struct handler {
    std::string name;
    [[nodiscard]] bool empty() const noexcept { return name.empty(); }
};

router<handler> router;

const auto registered = router.add_subscription(
    topic_pattern_path::parse("sensors/{id}/temperature").value(), qos::AT_LEAST_ONCE, handler { "logger" }
);

if (registered.needs_subscribe) {
    client.subscribe("sensors/+/temperature", qos::AT_LEAST_ONCE);
}

const auto topic = topic_path::parse("sensors/kitchen/temperature").value();
for (const auto& subscriber : router.get_subscribers(topic)) {
    subscriber.payload()->name;   // "logger"
}
```

`T` has to satisfy `payload_container` — default-initializable and able to say whether it is empty — because the tree prunes a node once its table empties out.

`add_subscription` returns a `registration_outcome { needs_subscribe, id }`, and `unsubscribe` returns a `removal_outcome { topic_now_empty, pattern }`. Those two flags are the whole point: they say exactly when the broker has to hear about a change.

The router is neither copyable nor thread-safe; one router belongs to one thread. The match cache *is* guarded, so a filter shared between threads may be matched concurrently.

### Parameter binding

Binding narrows a template to a concrete subtree without parsing anything new. `bind_parameter` returns a new filter, leaving the original untouched, so a bound filter is safe to share.

```cpp
const auto template_filter = topic_pattern_path::parse("sensors/{location}/{kind}/{id}/data").value();

const auto filtered = template_filter.bind_parameter("location", "kitchen")
                          .value()
                          .bind_parameter("kind", "temperature")
                          .value();

filtered.mqtt_pattern();              // sensors/kitchen/temperature/+/data
filtered.bound_value("location");     // "kitchen"
```

## Advanced usage

### LRU caching

Repeatedly matching the same topic against the same filter is common — one device publishing on a schedule, one dashboard subscribing to it. A cache strategy skips the match on the second hit:

```cpp
const auto filter = topic_pattern_path::parse("sensors/{id}/temperature", cache_strategy::lru(256)).value();

filter.caching().capacity();   // 256
```

A capacity of zero is `cache_strategy::no_cache()`, so the two states can never disagree — the Rust original needed a `NonZeroUsize` to express the same invariant. A copy of a filter gets a fresh, empty cache but shares the bindings.

### Publishing topics

The same template that subscribes can build the topic to publish on:

```cpp
const auto filter = topic_pattern_path::parse("devices/{type}/{id}/command").value();

filter.format_topic({ "sensor", "42" });   // devices/sensor/42/command
filter.format_topic({ "sensor" });         // error: parameter_count_mismatch_error{2, 1}
```

`format_topic` rejects a filter containing `#` — a `#` swallows an unknown number of levels, so there is no sensible arity.

### QoS aggregation

Two subscriptions whose templates differ only in a parameter name are one broker subscription. The router deduplicates by wire pattern and keeps the highest QoS asked for:

```cpp
router.add_subscription(topic_pattern_path::parse("sensors/{id}/data").value(), qos::AT_MOST_ONCE, a);
router.add_subscription(topic_pattern_path::parse("sensors/{name}/data").value(), qos::EXACTLY_ONCE, b);

router.get_topics_for_resubscribe();   // { "sensors/+/data" -> qos::EXACTLY_ONCE }
```

The second call's `needs_subscribe` is `true` — the effective QoS rose, so the wire subscription has to be raised.

### Reconnecting after a broker restart

The router holds the intent; the client re-establishes it.

```cpp
void on_broker_reconnect(client& c, const router<handler>& router) {
    for (const auto& [pattern, level] : router.get_topics_for_resubscribe()) {
        c.subscribe(pattern, level);
    }
}

void on_shutdown(client& c, router<handler>& router) {
    for (const auto& pattern : router.get_topics_for_unsubscribe()) { c.unsubscribe(pattern); }
    router.cleanup();
}
```

## Configuration

`cache_strategy` is the only knob on a filter, and it is per-filter rather than global — a filter on a hot topic can carry a large cache while a one-off filter carries none.

```cpp
topic_pattern_path::parse(pattern, cache_strategy::no_cache());   // the default
topic_pattern_path::parse(pattern, cache_strategy::lru(100));
topic_pattern_path::parse(pattern, cache_strategy::from_capacity(capacity));  // 0 maps to no_cache()
```

For an embedded target that wants neither routing nor caching, turn both options off at configure time; the resulting binary carries pattern parsing, validation and matching only.

## Performance

The shapes the design is built around:

- Matching is O(n) in the number of topic levels.
- Routing is O(m) in the number of matching subscriptions.
- Parsing a topic once and reusing the `shared_ptr<const topic_path>` removes the per-filter re-split, which dominates the naive loop.
- Cache hits replace a match with one map lookup; the lookup itself is a linear scan over the cache, which beats a hand-rolled eviction list at the capacities filters realistically use.

`benchmark/` carries the measurements — `topic_benchmark_pattern`, `topic_benchmark_matching` and `topic_benchmark_router` — and they are the only honest source of numbers for a given machine and workload.

## Building from source

```sh
cmake --preset win-msvc-2026-x64 \
    -Dba7lya.topic_BUILD_TEST=ON \
    -Dba7lya.topic_BUILD_BENCHMARK=ON \
    -Dba7lya.topic_BUILD_EXAMPLE=ON
cmake --build --preset win-msvc-2026-x64-dbg
ctest --preset win-msvc-2026-x64-test -C Debug --output-junit test-results.xml
```

Configure presets cover Windows MSVC 2019/2022/2026 (x86 and x64) and Linux GCC/Clang (x64); each has `<preset>-dbg` and `<preset>-rel` build presets and a `<preset>-test` test preset. Artifacts land in `build/<preset>/bin/<config>/` so a configuration always runs beside the DLLs it needs.

The optional targets are independent:

| Option                             | Targets                                                                        |
| ---------------------------------- | ------------------------------------------------------------------------------ |
| `ba7lya.topic_BUILD_TEST`   | `topic_ut`, `topic_integration`, `topic_e2e` (GoogleTest, registered with CTest) |
| `ba7lya.topic_BUILD_BENCHMARK` | `topic_benchmark_pattern`, `_matching`, `_router` (google benchmark) |
| `ba7lya.topic_BUILD_EXAMPLE` | `topic_example_basic_matching`, `topic_example_typed_router`     |

The test suites are split by kind under `tests/`: `ut/` for one unit in isolation, `integration/` for several components together, `e2e/` for whole flows through the public API.

## License

MIT — see [LICENSE](LICENSE).

`ba7lya.topic` is a C++ port of `mqtt-topic-engine`, part of [mqtt-typed-client](https://github.com/holovskyi/mqtt-typed-client) by Dmytro Holovskyi, which is dual-licensed Apache-2.0/MIT.
