// SPDX-License-Identifier: MIT
//
// Cache strategy configuration for topic-pattern matching results.

#pragma once

#include <cstddef>
#include <optional>

namespace ba7lya::topic_engine {

/// How a `topic_pattern_path` caches its match results.
///
/// The strategy only carries a capacity: a capacity of zero means "no caching" and any non-zero
/// capacity means "least-recently-used cache of that many entries". Modelling it as a single
/// number rather than a two-variant sum keeps the two states from ever disagreeing — the Rust
/// original needed `NonZeroUsize` to express exactly this invariant.
///
/// Caching is compiled in only when `BA7LYA_TOPIC_ENGINE_NO_LRU_CACHE` is undefined; with the
/// feature off, every constructor falls back to `no_cache()` and warns.
class cache_strategy {
public:
    /// Disables caching: every match builds a fresh result.
    [[nodiscard]]
    static cache_strategy no_cache() noexcept {
        return cache_strategy { 0 };
    }

    /// An LRU cache holding up to `capacity` match results.
    ///
    /// A `capacity` of zero yields `no_cache()`; with the `lru-cache` feature disabled this also
    /// yields `no_cache()` and logs a warning.
    [[nodiscard]]
    static cache_strategy lru(std::size_t capacity);

    /// Builds a strategy from a plain capacity, mapping zero to `no_cache()`.
    [[nodiscard]]
    static cache_strategy from_capacity(std::size_t capacity);

    /// The capacity of the LRU cache, or `std::nullopt` when caching is off.
    [[nodiscard]]
    std::optional<std::size_t> capacity() const noexcept {
        if (capacity_ == 0) { return std::nullopt; }
        return capacity_;
    }

    /// Whether this strategy caches anything.
    [[nodiscard]]
    bool is_lru() const noexcept {
        return capacity_ != 0;
    }

    friend bool operator==(const cache_strategy&, const cache_strategy&) noexcept = default;

private:
    explicit cache_strategy(std::size_t capacity) noexcept
        : capacity_(capacity) {}

    /// Number of entries to cache; zero means no caching at all.
    std::size_t capacity_ = 0;
};

} // namespace ba7lya::topic_engine
