///
/// @file pattern.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Topic names and filters: one level of a filter, a whole parsed filter, the concrete topic
/// name it is matched against, and the match that comes out.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
/// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "cache.hxx"
#include "cache.hxx"
#include "error.hxx"

namespace ba7lya::topic {

///
/// @brief The outcome of building or reworking a topic filter.
///
template<typename T>
using pattern_result = result<T, topic_pattern_error>;

///
/// @brief One `/`-delimited level of a topic filter.
/// @details
/// * `str` - a literal, matched byte for byte (`""` when the level is empty, as in `a//b`).
/// * `plus` - `+`, or the named `{param}`; matches exactly one level.
/// * `hash` - `#`, or the named `{param:#}`; matches the remaining levels, and only ever appears last.
/// @details The optional name is what separates the two spellings:
/// `plus{}` is an anonymous `+` and `plus{"id"}` is `{id}`, but both match identically.
///
class topic_pattern_item {
public:
    /// @brief An empty level, as produced by `a//b` or a leading/trailing slash.
    [[nodiscard]]
    static topic_pattern_item make_str(std::string value);

    /// @brief An anonymous `+`.
    [[nodiscard]]
    static topic_pattern_item make_plus();

    /// @brief An anonymous `#`.
    [[nodiscard]]
    static topic_pattern_item make_hash();

    /// @brief A named `{name}`.
    [[nodiscard]]
    static topic_pattern_item make_named_plus(std::string name);

    /// @brief A named `{name:#}`.
    [[nodiscard]]
    static topic_pattern_item make_named_hash(std::string name);

    /// @brief Parses one level of a filter.
    /// @details `+` and `#` are the anonymous wildcards;
    /// `{name}` and `{name:#}` are the named ones; an empty brace pair (`{}`, `{:#}`) is a usage error.
    /// A level that merely looks bracket-ish - `{param`, `param}`, `{{param}}` - is a literal, as is anything
    /// containing a stray `+` or `#`.
    [[nodiscard]]
    static pattern_result<topic_pattern_item> parse(std::string_view segment);

    /// @brief The filter spelling: the literal itself, or `"+"` / `"#"` for the wildcards.
    [[nodiscard]]
    std::string as_str() const;

    /// @brief The named spelling: the literal itself, `"+"` / `"#"` for anonymous wildcards, or `"{name}"` /
    /// `"{name:#}"` for the named ones.
    [[nodiscard]]
    std::string as_wildcard() const;

    /// @brief The parameter name, when this wildcard is named.
    [[nodiscard]]
    const std::optional<std::string>& param_name() const noexcept {
        return name_;
    }

    /// @brief Whether this level is a `+` or `#` wildcard, named or not.
    [[nodiscard]]
    bool is_wildcard() const noexcept;

    /// @brief Whether this level is a `+` wildcard, named or not.
    [[nodiscard]]
    bool is_plus() const noexcept {
        return kind_ == item_kind::plus;
    }

    /// @brief Whether this level is a `#` wildcard, named or not.
    [[nodiscard]]
    bool is_hash() const noexcept {
        return kind_ == item_kind::hash;
    }

    /// @brief Whether this level is a literal.
    [[nodiscard]]
    bool is_literal() const noexcept {
        return kind_ == item_kind::literal;
    }

    /// @brief Reserved-topic rule, from the filter's side: whether this level, standing as the *first* level of a
    /// filter, is the wildcard that must not reach a reserved `$` topic name.
    /// @details The exclusion is first-level-only - a `$` anywhere past the leading character is an ordinary literal -
    /// so the caller asks this about the first level and about no other. The trie, which walks a subscriber's levels,
    /// and `topic_pattern_path::match_str`, which walks a filter's, both judge with it, so the two cannot disagree
    /// about which filters see a `$` topic.
    /// @note The level is asked as it will be *walked*, bindings substituted: a parameter bound to a `$`-value is a
    /// literal by then, which is why a filter bound to `$SYS/...` keeps matching while `+/...` still does not. The
    /// topic-name side of the same rule is `topic_path::has_reserved_first_level`.
    [[nodiscard]]
    bool excludes_dollar_topic() const noexcept {
        return !is_literal();
    }

    /// @brief The literal text; empty for wildcards.
    [[nodiscard]]
    const std::string& literal() const noexcept {
        return literal_;
    }

    /// @brief Compares the filter structure, ignoring parameter names.
    /// @details `{id}` and `+` are the same level for compatibility purposes, and `sensors/+/data` lines up with
    /// `sensors/{id}/data`. Two wildcard levels of the same kind are equal whatever their parameters are called - or
    /// whether they are named at all - so the comparison is by kind first and by literal text only for literal levels.
    [[nodiscard]]
    friend bool operator==(const topic_pattern_item& lhs, const topic_pattern_item& rhs) noexcept {
        if (lhs.kind_ != rhs.kind_) { return false; }
        if (lhs.kind_ != item_kind::literal) { return true; }
        return lhs.literal_ == rhs.literal_;
    }

private:
    ///
    /// @brief Which of the three level kinds this item is.
    ///
    enum class item_kind : std::uint8_t { literal, plus, hash };

    topic_pattern_item(item_kind kind, std::string literal, std::optional<std::string> name)
        : kind_(kind)
        , literal_(std::move(literal))
        , name_(std::move(name)) {}

    /// @brief Which kind of level this is.
    item_kind kind_ = item_kind::literal;
    /// @brief The literal text, for `literal` levels.
    std::string literal_;
    /// @brief The parameter name, for named wildcards.
    std::optional<std::string> name_;
};

/// @brief The outcome of building a topic name.
using topic_path_result = result<std::shared_ptr<const class topic_path>, topic_path_error>;

///
/// @brief A concrete topic name, split into its `/`-delimited levels.
/// @details Construction validates well-formedness and splits once; the full text and the level slices then share this
/// single
/// object, so handing the same `topic_path` to several filters costs nothing. Build it through `parse` and
/// carry it in a `std::shared_ptr`.
///
class topic_path {
public:
    /// @brief Validates and splits `path`.
    /// @details Rejects an empty string and any string containing U+0000.
    /// Every other UTF-8 topic is accepted,
    /// spaces and the discouraged-but-legal U+0001..U+001F range included; the 65535-byte ceiling is a wire-codec
    /// concern and is not enforced here.
    [[nodiscard]]
    static topic_path_result parse(std::string path);

    /// @brief The reservation behind the leading-wildcard exclusion: a topic name whose first level begins with `$`
    /// belongs to the `$SYS`/`$share` space and is never matched by a filter whose first level is a wildcard.
    /// @details The exclusion is first-level-only, so the caller asks about the leading level; a `$` past the
    /// leading character is an ordinary literal and matches like any other.
    [[nodiscard]]
    bool has_reserved_first_level() const noexcept {
        return !segments_.empty() && segments_.front().starts_with('$');
    }

    /// @brief The full topic name.
    [[nodiscard]]
    const std::string& text() const noexcept {
        return path_;
    }

    /// @brief The levels, in order. `a/b/c` yields three; `a//b` yields an empty one in the middle.
    [[nodiscard]]
    const std::vector<std::string_view>& segments() const noexcept {
        return segments_;
    }

    /// @brief The number of levels.
    [[nodiscard]]
    std::size_t segment_count() const noexcept {
        return segments_.size();
    }

private:
    /// @brief Splits `path` on `/`; the caller has already established well-formedness.
    explicit topic_path(std::string path);

    /// @brief The full topic name; owns the storage the views in `segments_` point into.
    std::string path_;
    /// @brief Views into `path_`, one per `/`-delimited level.
    std::vector<std::string_view> segments_;
};

/// @brief The result of matching a `topic_path` against a filter.
/// @details Holds the matched topic name plus the level ranges its wildcards captured, reachable by position (`param`)
/// or by name (`named_param`). A `#` capture spans every remaining level and renders as their `/`-joined text.
/// @note Copying is cheap: a `shared_ptr` bump plus one small vector.
class topic_match {
public:
    /// @brief Records a match. Used by the filter's matcher.
    topic_match(
        std::shared_ptr<const topic_path> topic, //
        std::vector<std::pair<std::size_t, std::size_t>> param_ranges,
        std::vector<std::pair<std::string, std::pair<std::size_t, std::size_t>>> named_param_ranges
    )
        : topic_(std::move(topic))
        , param_ranges_(std::move(param_ranges))
        , named_param_ranges_(std::move(named_param_ranges)) {}

    /// @brief The matched topic name's levels.
    [[nodiscard]]
    const std::vector<std::string_view>& path_segments() const {
        return topic_->segments();
    }

    /// @brief The positional parameter captured at `index`, in filter order; empty when out of range.
    [[nodiscard]]
    std::string_view param(std::size_t index) const;

    /// @brief The value captured by the named parameter `name`, if the filter has one.
    [[nodiscard]]
    std::optional<std::string_view> named_param(std::string_view name) const;

    /// @brief The full matched topic name.
    [[nodiscard]]
    const std::string& topic_name() const {
        return topic_->text();
    }

    /// @brief How many positional parameters were captured.
    [[nodiscard]]
    std::size_t param_count() const noexcept {
        return param_ranges_.size();
    }

    /// @brief Renders as `Match(<topic>)`, plus ` with N params` when any were captured.
    [[nodiscard]]
    std::string to_string() const;

private:
    /// @brief Resolves a span of levels to the text it covers, `path_[start] / ... / path_[end - 1]`.
    /// @details An empty span resolves to the empty string; a single level resolves to that level.
    /// Both fall out of
    /// the arithmetic below, since an empty span starts and ends at the same offset.
    [[nodiscard]]
    std::string_view range_text(std::size_t start, std::size_t end) const;

    /// @brief The matched topic name.
    std::shared_ptr<const topic_path> topic_;
    /// @brief Spans of levels captured by positional wildcards, in filter order.
    std::vector<std::pair<std::size_t, std::size_t>> param_ranges_;
    /// @brief Spans of levels captured by named wildcards, paired with the parameter name.
    std::vector<std::pair<std::string, std::pair<std::size_t, std::size_t>>> named_param_ranges_;
};

///
/// @brief An MQTT topic filter: a validated sequence of levels plus the values bound to its named parameters.
/// @details Three spellings of the same filter are kept, and it is worth knowing which is which:
/// * the *template* - what the caller typed, named parameters and all: `sensors/{id}/data`;
/// * the *subscription pattern* - the wire form, wildcards anonymous: `sensors/+/data`. A topic containing a wildcard
///   character as data cannot be represented in this form, which is why `\`-quoting exists in the specification;
/// * the *bound pattern* - the subscription pattern with the bound parameters substituted: `sensors/kitchen/data` when
///   `id` is bound to `kitchen`.
/// @note A filter is immutable once built: `bind_parameter` returns a new value rather than mutating
/// in place, so a
/// bound filter can be shared freely.
///
class topic_pattern_path {
public:
    /// @brief Parses `pattern` and gives it a match cache per `strategy`.
    /// @details Rejects an empty pattern, a pattern containing U+0000, an empty or duplicated parameter name, and a `#`
    /// that is
    /// not the last level. A whitespace-only pattern is a perfectly valid literal filter and is
    /// accepted.
    /// @note The parameter is named `pattern_` rather than `pattern` because a parameter of the latter name would hide
    /// the
    /// free function `to_string(const topic_pattern_path&)` inside this class.
    [[nodiscard]]
    static pattern_result<topic_pattern_path> parse(std::string pattern_, cache_strategy strategy);

    /// @brief Parses `pattern` without a match cache.
    [[nodiscard]]
    static pattern_result<topic_pattern_path> parse(std::string pattern_);

    /// @brief Builds a filter straight from already-parsed levels.
    /// @details Only the `#`-must-be-last rule is enforced here; `parse` additionally rejects an empty or duplicated
    /// parameter
    /// name, and is the entry point callers are meant to use.
    [[nodiscard]]
    static pattern_result<topic_pattern_path> make_from_segments(
        std::vector<topic_pattern_item> segments, cache_strategy strategy = cache_strategy::no_cache()
    );

    /// @brief Builds `sensors/+/data`-shaped text from the levels.
    [[nodiscard]]
    static std::string subscription_pattern_of(const std::vector<topic_pattern_item>& segments);

    /// @brief Builds `sensors/{id}/data`-shaped text from the levels.
    [[nodiscard]]
    static std::string template_pattern_of(const std::vector<topic_pattern_item>& segments);

    /// @brief No copy, move, assignment or destructor is declared, and none is needed: the implicit ones are already
    /// what this class wants. Copying shares the match cache through its `shared_ptr` - a match does not depend on the
    /// bindings, so a rebinding may reuse everything the original computed - and moving hands the cache over. `Rule of
    /// Zero` in place of the four hand-written operations this class used to carry.

    /// @brief How this filter caches its matches.
    /// @details Not named `cache_strategy`: a member function carrying a type's name hides that type everywhere inside
    /// the
    /// class, so the `cache_strategy` parameter of `parse` and `with_cache_strategy` would stop resolving.
    [[nodiscard]]
    cache_strategy caching() const;

    /// @brief The values bound so far, in bind order; `nullopt` when none are bound.
    [[nodiscard]]
    std::optional<std::vector<std::pair<std::string, std::string>>> parameter_bindings() const;

    /// @brief The value bound to `name`, if any.
    [[nodiscard]]
    std::optional<std::string> bound_value(const std::string& name) const;

    /// @brief The wire subscription pattern, with any bound parameters substituted.
    /// @details This is the text to hand to the broker: `sensors/{id}/data` with `id` bound to `kitchen` renders as
    /// `sensors/kitchen/data`, and unbound it renders as `sensors/+/data`.
    [[nodiscard]]
    std::string mqtt_pattern() const;

    /// @brief The levels with bound parameters replaced by their values; unbound wildcards stay.
    [[nodiscard]]
    std::vector<topic_pattern_item> resolve_bound_segments() const;

    /// @brief The original template, named parameters intact.
    [[nodiscard]]
    const std::string& topic_pattern() const noexcept {
        return template_pattern_;
    }

    /// @brief Whether the filter has no levels at all.
    [[nodiscard]]
    bool is_empty() const noexcept {
        return segments_.empty();
    }

    /// @brief Whether the last level is a `#`, which by construction means the only `#`.
    [[nodiscard]]
    bool contains_hash() const noexcept;

    /// @brief The levels, in order.
    [[nodiscard]]
    const std::vector<topic_pattern_item>& segments() const noexcept {
        return segments_;
    }

    /// @brief How many levels the filter has.
    [[nodiscard]]
    std::size_t len() const noexcept {
        return segments_.size();
    }

    /// @brief Substitutes `values` for the filter's `+` wildcards, left to right.
    /// @details Rejects a `#` filter (there is no sensible arity) and a value count that disagrees with
    /// the
    /// wildcard count.
    [[nodiscard]]
    result<std::string, topic_format_failure> format_topic(const std::vector<std::string>& values) const;

    /// @brief Checks that `candidate` is a strict refinement of this filter, then returns it.
    /// @details `candidate` is compatible when it matches every topic this filter matches: at each level the two either
    /// agree on
    /// the wildcard character, or this filter is the wildcard and `candidate` is anything. So
    /// `sensors/kitchen/data` refines `sensors/+/data`, and `sensors/+/data` refines itself, but neither refines
    /// `sensors/+/data/extra`, whose extra level this filter would not have matched.
    [[nodiscard]]
    pattern_result<topic_pattern_path> check_compatibility(const topic_pattern_path& candidate) const;

    /// @brief Returns the same filter with a different cache strategy, keeping its bindings.
    [[nodiscard]]
    topic_pattern_path with_cache_strategy(cache_strategy strategy) const;

    /// @brief Returns a copy of this filter with `name` bound to `value`.
    /// @details A `+` parameter takes the bound value as its literal level. A named `#` may be bound too, for the
    /// parameter
    /// bookkeeping, but the value cannot stand for the levels it swallows: the level still renders -
    /// and matches - as an anonymous `#`. Binding a name the filter does not have is a usage error. Rebinding an
    /// already bound name replaces the value.
    [[nodiscard]]
    pattern_result<topic_pattern_path> bind_parameter(std::string name, std::string value) const;

    /// @brief Matches `topic` against this filter.
    /// @details Takes a shared topic so that matching one topic against many filters - the hot path - parses the topic
    /// once.
    /// For a single one-off match from text, `match_str` is handier.
    [[nodiscard]]
    result<std::shared_ptr<const topic_match>, topic_match_error> match(std::shared_ptr<const topic_path> topic) const;

    /// @brief Builds the topic name and matches it in one step.
    [[nodiscard]]
    result<std::shared_ptr<const topic_match>, topic_match_error> match_str(const std::string& topic) const;

    /// @brief Renders as the original template.
    [[nodiscard]]
    std::string to_string() const {
        return template_pattern_;
    }

private:
    ///
    /// @brief The cache this filter carries, when one was requested.
    /// @details Holds the matched topic's text alongside its match, because a lazy cache has no key of its own:
    /// a `topic_match` keeps only a pointer to its topic, so a cached match whose topic was thrown away would dangle.
    /// Retaining the text is what keeps the match alive. A match does not depend on the bindings, so a rebinding keeps
    /// using the same cache. It is the only mutable member, hence held behind a pointer and shared by copies.
    ///
    using match_cache = lru_cache<cached_match>;

    topic_pattern_path(
        std::string template_pattern, std::string subscription_pattern, std::vector<topic_pattern_item> segments,
        std::vector<std::pair<std::string, std::string>> bindings, std::shared_ptr<match_cache> cache
    );

    /// @brief The non-caching matcher: a level walk that captures parameter spans.
    [[nodiscard]]
    result<topic_match, topic_match_error> match_internal(const std::shared_ptr<const topic_path>& topic) const;

    /// @brief Returns `built` carrying this filter's bindings.
    [[nodiscard]]
    topic_pattern_path inherit_bindings(topic_pattern_path built) const;

    /// @brief The caller-typed pattern, named parameters intact.
    std::string template_pattern_;
    /// @brief The wire pattern, wildcards anonymous.
    std::string subscription_pattern_;
    /// @brief The parsed levels.
    std::vector<topic_pattern_item> segments_;
    /// @brief Bound parameter name/value pairs, in bind order.
    std::vector<std::pair<std::string, std::string>> bindings_;
    /// @brief The match cache, shared with copies; absent when caching is off.
    std::shared_ptr<match_cache> cache_;
};

/// @brief Renders a filter the way the Rust original's `Display` impl does: its template.
[[nodiscard]]
std::string to_string(const topic_pattern_path& pattern);

} // namespace ba7lya::topic
