// SPDX-License-Identifier: MIT
//
// A parsed, validated MQTT topic filter and everything one can do with it.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "cache_strategy.hxx"
#include "topic_match.hxx"
#include "topic_pattern_item.hxx"

namespace ba7lya::topic_engine {

/// Formatting a topic for publishing was asked for on a filter containing `#`.
///
/// `#` swallows an unknown number of levels, so there is no way to know how many values to
/// substitute for it.
struct hash_wildcard_not_supported_error {
    [[nodiscard]]
    std::string message() const;
};

/// A different number of values was supplied than the filter has `+` wildcards.
struct parameter_count_mismatch_error {
    /// How many values the filter needs.
    std::size_t expected = 0;
    /// How many the caller supplied.
    std::size_t provided = 0;

    [[nodiscard]]
    std::string message() const;
};

/// A supplied value could not be rendered into the topic text.
struct topic_format_error {
    [[nodiscard]]
    std::string message() const;
};

/// Everything that can go wrong while formatting a topic for publishing.
using topic_format_failure
    = std::variant<hash_wildcard_not_supported_error, parameter_count_mismatch_error, topic_format_error>;

/// Renders an error the way the Rust original's `Display` impls do.
[[nodiscard]]
std::string describe(const topic_format_failure& error);

/// An MQTT topic filter: a validated sequence of levels plus the values bound to its named
/// parameters.
///
/// Three spellings of the same filter are kept, and it is worth knowing which is which:
///
/// * the *template* — what the caller typed, named parameters and all: `sensors/{id}/data`;
/// * the *subscription pattern* — the wire form, wildcards anonymous: `sensors/+/data`. A topic
///   containing a wildcard character as data cannot be represented in this form, which is why
///   `\`-quoting exists in MQTT;
/// * the *bound pattern* — the subscription pattern with the bound parameters substituted:
///   `sensors/kitchen/data` when `id` is bound to `kitchen`.
///
/// A filter is immutable once built: `bind_parameter` returns a new value rather than mutating
/// in place, so a bound filter can be shared freely.
class topic_pattern_path {
public:
    /// Parses `pattern` and gives it a match cache per `strategy`.
    ///
    /// Rejects an empty pattern, a pattern containing U+0000, an empty or duplicated parameter
    /// name, and a `#` that is not the last level. A whitespace-only pattern is a perfectly
    /// valid literal filter and is accepted.
    [[nodiscard]]
    static pattern_result<topic_pattern_path> parse(std::string pattern, cache_strategy strategy);

    /// Parses `pattern` without a match cache.
    [[nodiscard]]
    static pattern_result<topic_pattern_path> parse(std::string pattern);

    /// Builds a filter straight from already-parsed levels.
    ///
    /// Only the `#`-must-be-last rule is enforced here; `parse` additionally rejects an empty or
    /// duplicated parameter name, and is the entry point callers are meant to use.
    [[nodiscard]]
    static pattern_result<topic_pattern_path> make_from_segments(
        std::vector<topic_pattern_item> segments, cache_strategy strategy = cache_strategy::no_cache()
    );

    /// Builds `sensors/+/data`-shaped text from the levels.
    [[nodiscard]]
    static std::string subscription_pattern_of(const std::vector<topic_pattern_item>& segments);

    /// Builds `sensors/{id}/data`-shaped text from the levels.
    [[nodiscard]]
    static std::string template_pattern_of(const std::vector<topic_pattern_item>& segments);

    topic_pattern_path(const topic_pattern_path& other);
    topic_pattern_path(topic_pattern_path&& other) noexcept;
    topic_pattern_path& operator=(const topic_pattern_path& other);
    topic_pattern_path& operator=(topic_pattern_path&& other) noexcept;
    ~topic_pattern_path();

    /// How this filter caches its matches.
    ///
    /// Not named `cache_strategy`: a member function carrying a type's name hides that type
    /// everywhere inside the class, so the `cache_strategy` parameter of `parse` and
    /// `with_cache_strategy` would stop resolving.
    [[nodiscard]]
    cache_strategy caching() const;

    /// The values bound so far, in bind order; `nullopt` when none are bound.
    [[nodiscard]]
    std::optional<std::vector<std::pair<std::string, std::string>>> parameter_bindings() const;

    /// The value bound to `name`, if any.
    [[nodiscard]]
    std::optional<std::string> bound_value(const std::string& name) const;

    /// The wire subscription pattern, with any bound parameters substituted.
    ///
    /// This is the text to hand to the broker: `sensors/{id}/data` with `id` bound to `kitchen`
    /// renders as `sensors/kitchen/data`, and unbound it renders as `sensors/+/data`.
    [[nodiscard]]
    std::string mqtt_pattern() const;

    /// The levels with bound parameters replaced by their values; unbound wildcards stay.
    [[nodiscard]]
    std::vector<topic_pattern_item> resolve_bound_segments() const;

    /// The original template, named parameters intact.
    [[nodiscard]]
    const std::string& topic_pattern() const noexcept {
        return template_pattern_;
    }

    /// Whether the filter has no levels at all.
    [[nodiscard]]
    bool is_empty() const noexcept {
        return segments_.empty();
    }

    /// Whether the last level is a `#`, which by construction means the only `#`.
    [[nodiscard]]
    bool contains_hash() const noexcept;

    /// The levels, in order.
    [[nodiscard]]
    const std::vector<topic_pattern_item>& segments() const noexcept {
        return segments_;
    }

    /// How many levels the filter has.
    [[nodiscard]]
    std::size_t len() const noexcept {
        return segments_.size();
    }

    /// Substitutes `values` for the filter's `+` wildcards, left to right.
    ///
    /// Rejects a `#` filter (there is no sensible arity) and a value count that disagrees with
    /// the wildcard count.
    [[nodiscard]]
    result<std::string, topic_format_failure> format_topic(const std::vector<std::string>& values) const;

    /// Checks that `candidate` has the same wildcard structure, then returns it.
    ///
    /// Literal levels may differ freely — that is the point of the check — but the wildcards
    /// must line up in kind and order. `sensors/kitchen/data` and `sensors/+/data` are
    /// compatible; `sensors/+/data` and `sensors/#` are not.
    [[nodiscard]]
    pattern_result<topic_pattern_path> check_compatibility(const topic_pattern_path& candidate) const;

    /// Returns the same filter with a different cache strategy, keeping its bindings.
    [[nodiscard]]
    topic_pattern_path with_cache_strategy(cache_strategy strategy) const;

    /// Returns a copy of this filter with `name` bound to `value`.
    ///
    /// Only `+` parameters can be bound; binding a name the filter does not have is a usage
    /// error. Rebinding an already bound name replaces the value.
    [[nodiscard]]
    pattern_result<topic_pattern_path> bind_parameter(std::string name, std::string value) const;

    /// Matches `topic` against this filter.
    ///
    /// Takes a shared topic so that matching one topic against many filters — the hot path —
    /// parses the topic once. For a single one-off match from text, `match_str` is handier.
    [[nodiscard]]
    result<std::shared_ptr<const topic_match>, topic_match_error> match(std::shared_ptr<const topic_path> topic) const;

    /// Builds the topic name and matches it in one step.
    [[nodiscard]]
    result<std::shared_ptr<const topic_match>, topic_match_error> match_str(const std::string& topic) const;

    /// Renders as the original template.
    [[nodiscard]]
    std::string to_string() const {
        return template_pattern_;
    }

private:
    /// A match result paired with its position in the eviction order.
    struct cache_entry {
        /// The cached match.
        std::shared_ptr<const topic_match> matched;
        /// Monotonic stamp; the least recently used entry has the lowest stamp.
        std::uint64_t last_used = 0;
    };

    /// Least-recently-used cache of topic text to match result.
    ///
    /// A linear scan over the map beats a hand-rolled eviction list at the capacities filters
    /// realistically use — tens to a few hundred entries — and keeps the code small enough to
    /// read. The scan is linear in the capacity per hit, so a filter that really is hot wants a
    /// larger capacity, not a cleverer cache.
    class match_cache {
    public:
        /// Builds a cache holding at most `capacity` entries.
        explicit match_cache(std::size_t capacity) noexcept
            : capacity_(capacity) {}

        /// The number of entries this cache will hold.
        [[nodiscard]]
        std::size_t capacity() const noexcept {
            return capacity_;
        }

        /// Looks `topic` up, refreshing its position in the eviction order.
        [[nodiscard]]
        std::shared_ptr<const topic_match> get(const std::string& topic);

        /// Inserts or refreshes `topic`, evicting the least recently used entry if needed.
        void put(const std::string& topic, std::shared_ptr<const topic_match> matched);

    private:
        /// Drops least-recently-used entries until the cache fits.
        ///
        /// Needed because refreshing an already-cached key does not grow the map, so only an
        /// insertion can push it over the limit — and a `map::insert` alone would not say so.
        void evict_over_capacity();

        /// Guards the cache, since one filter may be matched from several threads.
        mutable std::mutex mutex_;
        /// Topic text to cached match.
        std::map<std::string, cache_entry, std::less<>> entries_;
        /// Monotonic counter feeding `cache_entry::last_used`.
        std::uint64_t clock_ = 0;
        /// Maximum number of entries.
        std::size_t capacity_ = 0;
    };

    /// The mutable part of a filter: the cache and the bindings.
    ///
    /// Copying a filter shares this, and that is deliberate — the cache maps topic text to a
    /// match, and a match does not depend on the bindings, so a rebinding may reuse the cache.
    /// The bindings themselves are never mutated in place: `bind_parameter` works on a copy.
    struct shared_state {
        /// The match cache, absent when caching is off.
        std::unique_ptr<match_cache> cache;
        /// Bound parameter name/value pairs, in bind order.
        std::vector<std::pair<std::string, std::string>> bindings;
    };

    topic_pattern_path(
        std::string template_pattern, std::string subscription_pattern, std::vector<topic_pattern_item> segments,
        std::shared_ptr<shared_state> state
    );

    /// Whether the first level is a wildcard that has NOT been bound.
    ///
    /// A bound named wildcard resolves to a literal — the trie stores it as an exact child — so
    /// it must not count as a wildcard for the §4.7.2 `$`-exclusion.
    [[nodiscard]]
    bool first_level_is_unbound_wildcard() const;

    /// The non-caching matcher: a level walk that captures parameter spans.
    [[nodiscard]]
    result<topic_match, topic_match_error> match_internal(const std::shared_ptr<const topic_path>& topic) const;

    /// Returns `built` carrying this filter's bindings.
    [[nodiscard]]
    topic_pattern_path inherit_bindings(topic_pattern_path built) const;

    /// The caller-typed pattern, named parameters intact.
    std::string template_pattern_;
    /// The wire pattern, wildcards anonymous.
    std::string subscription_pattern_;
    /// The parsed levels.
    std::vector<topic_pattern_item> segments_;
    /// The cache and bindings, shared by copies of this filter.
    std::shared_ptr<shared_state> state_;
};

/// Renders a filter the way the Rust original's `Display` impl does: its template.
[[nodiscard]]
std::string to_string(const topic_pattern_path& pattern);

} // namespace ba7lya::topic_engine
