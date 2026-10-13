// SPDX-License-Identifier: MIT
//
// A single level of an MQTT topic filter, plus the error type raised while parsing it.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "result.hxx"
#include "topic_wellformedness.hxx"

namespace ba7lya::topic_engine {

/// `#` was used somewhere other than the last level of the filter.
struct hash_position_error {
    /// The rejected filter.
    std::string pattern;

    [[nodiscard]]
    std::string message() const;
};

/// `+` or `#` appeared in a position where the filter syntax does not allow it.
struct wildcard_usage_error {
    /// The offending segment, or a description of the offending usage.
    std::string usage;

    [[nodiscard]]
    std::string message() const;
};

/// The filter was empty, which MQTT §4.7.3 forbids.
struct empty_pattern_error {
    [[nodiscard]]
    std::string message() const;
};

/// The filter contained the null character (U+0000), which MQTT §4.7.3 forbids.
struct null_char_pattern_error {
    [[nodiscard]]
    std::string message() const;
};

/// A candidate filter was checked against a reference one and the wildcard structure differed.
struct pattern_structure_mismatch_error {
    /// The pattern stored in the type.
    std::string original;
    /// The candidate pattern that did not line up with it.
    std::string custom;

    [[nodiscard]]
    std::string message() const;
};

/// Everything that can go wrong while building a topic filter.
using topic_pattern_error = std::variant<
    hash_position_error, wildcard_usage_error, empty_pattern_error, null_char_pattern_error,
    pattern_structure_mismatch_error>;

/// The outcome of parsing a filter.
template<typename T>
using pattern_result = result<T, topic_pattern_error>;

/// Builds a `hash_position_error`.
[[nodiscard]]
topic_pattern_error make_hash_position_error(std::string pattern);

/// Builds a `wildcard_usage_error`.
[[nodiscard]]
topic_pattern_error make_wildcard_usage_error(std::string usage);

/// Builds a `pattern_structure_mismatch_error`.
[[nodiscard]]
topic_pattern_error make_pattern_mismatch_error(std::string original, std::string custom);

/// Maps the shared MQTT §4.7.3 well-formedness violation onto the filter's error type.
[[nodiscard]]
topic_pattern_error make_topic_pattern_error(const malformed& cause);

/// Renders an error the way the Rust original's `Display` impls do.
[[nodiscard]]
std::string describe(const topic_pattern_error& error);

/// One `/`-delimited level of an MQTT topic filter.
///
/// * `str` — a literal, matched byte for byte (`""` when the level is empty, as in `a//b`).
/// * `plus` — `+`, or the named `{param}`; matches exactly one level.
/// * `hash` — `#`, or the named `{param:#}`; matches the remaining levels, and only ever
///   appears last.
///
/// The optional name is what separates the two spellings: `plus{}` is an anonymous `+` and
/// `plus{"id"}` is `{id}`, but both match identically.
class topic_pattern_item {
public:
    /// An empty level, as produced by `a//b` or a leading/trailing slash.
    [[nodiscard]]
    static topic_pattern_item make_str(std::string value);

    /// An anonymous `+`.
    [[nodiscard]]
    static topic_pattern_item make_plus();

    /// An anonymous `#`.
    [[nodiscard]]
    static topic_pattern_item make_hash();

    /// A named `{name}`.
    [[nodiscard]]
    static topic_pattern_item make_named_plus(std::string name);

    /// A named `{name:#}`.
    [[nodiscard]]
    static topic_pattern_item make_named_hash(std::string name);

    /// Parses one level of a filter.
    ///
    /// `+` and `#` are the anonymous wildcards; `{name}` and `{name:#}` are the named ones; an
    /// empty brace pair (`{}`, `{:#}`) is a usage error. A level that merely looks bracket-ish
    /// — `{param`, `param}`, `{{param}}` — is a literal, as is anything containing a stray `+`
    /// or `#`.
    [[nodiscard]]
    static pattern_result<topic_pattern_item> parse(std::string_view segment);

    /// The filter spelling: the literal itself, or `"+"` / `"#"` for the wildcards.
    [[nodiscard]]
    std::string as_str() const;

    /// The named spelling: the literal itself, `"+"` / `"#"` for anonymous wildcards, or
    /// `"{name}"` / `"{name:#}"` for the named ones.
    [[nodiscard]]
    std::string as_wildcard() const;

    /// The parameter name, when this wildcard is named.
    [[nodiscard]]
    const std::optional<std::string>& param_name() const noexcept {
        return name_;
    }

    /// Whether this level is a `+` or `#` wildcard, named or not.
    [[nodiscard]]
    bool is_wildcard() const noexcept;

    /// Whether this level is a `+` wildcard, named or not.
    [[nodiscard]]
    bool is_plus() const noexcept {
        return kind_ == item_kind::plus;
    }

    /// Whether this level is a `#` wildcard, named or not.
    [[nodiscard]]
    bool is_hash() const noexcept {
        return kind_ == item_kind::hash;
    }

    /// Whether this level is a literal.
    [[nodiscard]]
    bool is_literal() const noexcept {
        return kind_ == item_kind::literal;
    }

    /// The literal text; empty for wildcards.
    [[nodiscard]]
    const std::string& literal() const noexcept {
        return literal_;
    }

    /// Compares the filter structure, ignoring parameter names.
    ///
    /// `{id}` and `+` are the same level for compatibility purposes, and `sensors/+/data` lines
    /// up with `sensors/{id}/data`.
    [[nodiscard]]
    friend bool operator==(const topic_pattern_item& lhs, const topic_pattern_item& rhs) noexcept {
        if (lhs.kind_ != rhs.kind_) { return false; }
        if (lhs.kind_ == item_kind::literal) { return lhs.literal_ == rhs.literal_; }
        return true;
    }

private:
    /// Which of the three level kinds this item is.
    enum class item_kind : std::uint8_t { literal, plus, hash };

    topic_pattern_item(item_kind kind, std::string literal, std::optional<std::string> name)
        : kind_(kind)
        , literal_(std::move(literal))
        , name_(std::move(name)) {}

    /// Which kind of level this is.
    item_kind kind_ = item_kind::literal;
    /// The literal text, for `literal` levels.
    std::string literal_;
    /// The parameter name, for named wildcards.
    std::optional<std::string> name_;
};

} // namespace ba7lya::topic_engine
