// SPDX-License-Identifier: MIT

#include "topic_pattern_item.hxx"

namespace ba7lya::topic_engine {

namespace {

/// Drops every leading occurrence of `character`, mirroring Rust's `trim_start_matches`.
[[nodiscard]]
std::string_view trim_start_matches(std::string_view text, char character) {
    std::size_t offset = 0;
    while (offset < text.size() && text[offset] == character) { ++offset; }
    return text.substr(offset);
}

/// Drops every trailing occurrence of `suffix`, mirroring Rust's `trim_end_matches`.
[[nodiscard]]
std::string_view trim_end_matches(std::string_view text, std::string_view suffix) {
    while (text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0) {
        text.remove_suffix(suffix.size());
    }
    return text;
}

/// Whether the level contains a wildcard character that the filter syntax does not allow here.
[[nodiscard]]
bool has_stray_wildcard(std::string_view text) {
    return text.find_first_of("+#") != std::string_view::npos;
}

} // namespace

std::string hash_position_error::message() const {
    return "Invalid topic pattern '" + pattern + "': # wildcard can only be the last segment";
}

std::string wildcard_usage_error::message() const { return "Invalid wildcard usage: " + usage; }

std::string empty_pattern_error::message() const { return "Topic pattern cannot be empty"; }

std::string null_char_pattern_error::message() const {
    return "Topic pattern must not contain the null character (U+0000)";
}

std::string pattern_structure_mismatch_error::message() const {
    return "Topic pattern structure mismatch.\nOriginal: '" + original + "'\nCustom:   '" + custom
         + "'\nHint: Both patterns must have the same parameter structure (same wildcards in same positions)";
}

topic_pattern_error make_hash_position_error(std::string pattern) { return hash_position_error { std::move(pattern) }; }

topic_pattern_error make_wildcard_usage_error(std::string usage) { return wildcard_usage_error { std::move(usage) }; }

topic_pattern_error make_pattern_mismatch_error(std::string original, std::string custom) {
    return pattern_structure_mismatch_error { std::move(original), std::move(custom) };
}

topic_pattern_error make_topic_pattern_error(const malformed& cause) {
    if (std::holds_alternative<null_char_error>(cause)) { return null_char_pattern_error {}; }
    return empty_pattern_error {};
}

std::string describe(const topic_pattern_error& error) {
    return std::visit([](const auto& concrete) { return concrete.message(); }, error);
}

topic_pattern_item topic_pattern_item::make_str(std::string value) {
    return topic_pattern_item { item_kind::literal, std::move(value), std::nullopt };
}

topic_pattern_item topic_pattern_item::make_plus() { return topic_pattern_item { item_kind::plus, {}, std::nullopt }; }

topic_pattern_item topic_pattern_item::make_hash() { return topic_pattern_item { item_kind::hash, {}, std::nullopt }; }

topic_pattern_item topic_pattern_item::make_named_plus(std::string name) {
    return topic_pattern_item { item_kind::plus, {}, std::move(name) };
}

topic_pattern_item topic_pattern_item::make_named_hash(std::string name) {
    return topic_pattern_item { item_kind::hash, {}, std::move(name) };
}

pattern_result<topic_pattern_item> topic_pattern_item::parse(std::string_view segment) {
    if (segment == "+") { return make_plus(); }
    if (segment == "#") { return make_hash(); }
    if (segment.starts_with('{') && segment.ends_with(":#}")) {
        const std::string_view inner = trim_end_matches(trim_start_matches(segment, '{'), ":#}");
        if (inner.empty()) { return make_wildcard_usage_error(std::string { segment }); }
        return make_named_hash(std::string { inner });
    }
    if (segment.starts_with('{') && segment.ends_with('}')) {
        const std::string_view inner = trim_end_matches(trim_start_matches(segment, '{'), "}");
        if (inner.empty()) { return make_wildcard_usage_error(std::string { segment }); }
        return make_named_plus(std::string { inner });
    }
    if (has_stray_wildcard(segment)) { return make_wildcard_usage_error(std::string { segment }); }
    return make_str(std::string { segment });
}

std::string topic_pattern_item::as_str() const {
    switch (kind_) {
    case item_kind::literal: return literal_;
    case item_kind::plus:    return "+";
    case item_kind::hash:    return "#";
    }
    return {};
}

std::string topic_pattern_item::as_wildcard() const {
    switch (kind_) {
    case item_kind::literal: return literal_;
    case item_kind::plus:    break;
    case item_kind::hash:
        if (name_.has_value()) { return "{" + *name_ + ":#}"; }
        return "#";
    }
    if (name_.has_value()) { return "{" + *name_ + "}"; }
    return "+";
}

bool topic_pattern_item::is_wildcard() const noexcept { return kind_ != item_kind::literal; }

} // namespace ba7lya::topic_engine
