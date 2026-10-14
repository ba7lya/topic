///
/// @file cache.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The match caches a `topic_pattern_path` may carry: the strategy that selects one and the LRU cache itself.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace ba7lya::topic {

class topic_match;

///
/// @brief How a `topic_pattern_path` caches its match results.
/// @details The strategy only carries a capacity: a capacity of zero means "no caching" and any non-zero capacity means
/// "least-recently-used cache of that many entries". Modelling it as a single number rather than a two-variant sum
/// keeps the two states from ever disagreeing — the Rust original needed `NonZeroUsize` to express exactly this
/// invariant.
/// @note Caching is compiled in only when `BA7LYA_TOPIC_NO_LRU_CACHE` is undefined; with the feature off, every
/// constructor falls back to `no_cache()` and warns.
///
class cache_strategy {
public:
    /// @brief Disables caching: every match builds a fresh result.
    /// @return A strategy that caches nothing.
    [[nodiscard]]
    static cache_strategy no_cache() noexcept {
        return cache_strategy { 0 };
    }

    /// @brief An LRU cache holding up to `capacity` match results.
    /// @details A `capacity` of zero yields `no_cache()`; with the `lru-cache` feature disabled this also yields
    /// `no_cache()` and logs a warning.
    /// @param capacity The number of match results to hold.
    /// @return The strategy, or `no_cache()` when `capacity` is zero or caching is compiled out.
    [[nodiscard]]
    static cache_strategy lru(std::size_t capacity);

    /// @brief Builds a strategy from a plain capacity, mapping zero to `no_cache()`.
    /// @param capacity The number of match results to hold.
    /// @return The strategy, or `no_cache()` when `capacity` is zero or caching is compiled out.
    [[nodiscard]]
    static cache_strategy from_capacity(std::size_t capacity);

    /// @brief The capacity of the LRU cache, or `std::nullopt` when caching is off.
    /// @return The capacity, or `std::nullopt` when caching is off.
    [[nodiscard]]
    std::optional<std::size_t> capacity() const noexcept {
        if (capacity_ == 0) { return std::nullopt; }
        return capacity_;
    }

    /// @brief Whether this strategy caches anything.
    /// @return `true` when the capacity is non-zero.
    [[nodiscard]]
    bool is_lru() const noexcept {
        return capacity_ != 0;
    }

    friend bool operator==(const cache_strategy& lhs, const cache_strategy& rhs) noexcept = default;

private:
    explicit cache_strategy(std::size_t capacity) noexcept
        : capacity_(capacity) {}

    /// @brief Number of entries to cache; zero means no caching at all.
    std::size_t capacity_ = 0;
};

///
/// @brief A value looked up by topic text, together with the position it holds in the eviction order.
/// @tparam T The cached value.
///
template<typename T>
struct cached_value {
    /// @brief The cached value.
    std::shared_ptr<const T> value;
    /// @brief Monotonic stamp; the least recently used entry has the lowest stamp.
    std::uint64_t last_used = 0;
};

///
/// @brief A `topic_match` together with the topic text it was computed from.
/// @details A match borrows its topic through a pointer, so a cached match must outlive the `topic_path` it came from;
/// the pair is what makes caching a match safe. `cache.hxx` owns the type because `topic_match` is only
/// forward-declared here - the cache is stored behind a pointer, so the definition is not needed at this point.
///
struct cached_match {
    /// @brief The matched topic's text, kept alive for `match` to point at.
    std::shared_ptr<const std::string> topic_text;
    /// @brief The match over that topic.
    std::shared_ptr<const topic_match> match;
};

///
/// @brief A bounded cache of values keyed by topic text, evicting the least recently used entry.
/// @tparam T The cached value.
/// @details A linear scan over the map beats a hand-rolled eviction list at the capacities filters realistically use -
/// tens to a few hundred entries - and keeps the code small enough to read. The scan is linear in the capacity per hit,
/// so a filter that really is hot wants a larger capacity, not a cleverer cache.
/// @note The cache is guarded by its own mutex, since one filter may be matched from several threads.
///
template<typename T>
class lru_cache {
public:
    /// @brief Builds a cache holding at most `capacity` entries.
    /// @param capacity The maximum number of entries.
    explicit lru_cache(std::size_t capacity) noexcept
        : capacity_(capacity) {}

    /// @brief The maximum number of entries this cache holds.
    /// @return The capacity.
    [[nodiscard]]
    std::size_t capacity() const noexcept {
        return capacity_;
    }

    /// @brief Looks a key up, refreshing its position in the eviction order.
    /// @param key The key to look up.
    /// @return The cached value, or `nullptr` when the key is absent.
    [[nodiscard]]
    std::shared_ptr<const T> get(std::string_view key) {
        const std::lock_guard<std::mutex> guard { mutex_ };
        const auto entry = entries_.find(key);
        if (entry == entries_.end()) { return nullptr; }
        entry->second.last_used = ++clock_;
        return entry->second.value;
    }

    /// @brief Inserts or refreshes a key, evicting the least recently used entry when that overflows the capacity.
    /// @param key The key to store.
    /// @param value The value to store under it.
    void put(std::string key, std::shared_ptr<const T> value) {
        const std::lock_guard<std::mutex> guard { mutex_ };
        if (const auto entry = entries_.find(key); entry != entries_.end()) {
            entry->second.value = std::move(value);
            entry->second.last_used = ++clock_;
            return;
        }
        entries_.insert_or_assign(std::move(key), cached_value<T> { std::move(value), ++clock_ });
        evict_over_capacity();
    }

private:
    /// @brief Drops least-recently-used entries until the cache fits.
    /// @details Needed because refreshing an already-cached key does not grow the map, so only an insertion can push it
    /// over the limit - and a `map::insert` alone would not say so.
    void evict_over_capacity() {
        while (entries_.size() > capacity_) {
            entries_.erase(std::min_element(entries_.begin(), entries_.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.second.last_used < rhs.second.last_used;
            }));
        }
    }

    /// @brief Guards the cache, since one filter may be matched from several threads.
    mutable std::mutex mutex_;
    /// @brief Topic text to cached value.
    std::map<std::string, cached_value<T>, std::less<>> entries_;
    /// @brief Monotonic counter feeding `cached_value::last_used`.
    std::uint64_t clock_ = 0;
    /// @brief Maximum number of entries.
    std::size_t capacity_ = 0;
};

} // namespace ba7lya::topic
