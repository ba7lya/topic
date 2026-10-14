///
/// @file pattern.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Implementation of topic filters: the levels, the parsed filter, and the match cache it
/// carries.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///
// SPDX-License-Identifier: MIT

#include "pattern.hxx"

#include <algorithm>
#include <spdlog/spdlog.h>
#include <utility>

namespace ba7lya::topic {

namespace {

/// @brief Drops every leading occurrence of `character`, mirroring Rust's `trim_start_matches`.
[[nodiscard]]
std::string_view trim_start_matches(std::string_view text, char character) {
    std::size_t offset = 0;
    while (offset < text.size() && text[offset] == character) { ++offset; }
    return text.substr(offset);
}

/// @brief Drops every trailing occurrence of `suffix`, mirroring Rust's `trim_end_matches`.
[[nodiscard]]
std::string_view trim_end_matches(std::string_view text, std::string_view suffix) {
    while (text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0) {
        text.remove_suffix(suffix.size());
    }
    return text;
}

/// @brief Splits a parameter level into its name, or nothing when the brackets do not pair up one to one.
/// @details `segment` is known to start with `{` and to end with `suffix`, so the name is what lies
/// between them —
/// provided it is brace-free. `{{param}}` therefore has no name and is not a parameter at all: its inner `{` never
/// closes before the end, so the level stays a literal. An empty name (`{}`) IS a parameter, merely a malformed one;
/// `nullopt` reports the former case and an empty view the latter, so the caller can tell the two apart.
[[nodiscard]]
std::optional<std::string_view> parameter_name(std::string_view segment, std::string_view suffix) {
    const std::string_view inner = segment.substr(1, segment.size() - 1 - suffix.size());
    if (inner.find_first_of("{}") != std::string_view::npos) { return std::nullopt; }
    return inner;
}

/// @brief Whether the level contains a wildcard character that the filter syntax does not allow here.
[[nodiscard]]
bool has_stray_wildcard(std::string_view text) {
    return text.find_first_of("+#") != std::string_view::npos;
}

/// @brief Splits `text` on `/`, returning one view per level.
/// @details Mirrors Rust's `str::split`: the split is total, so `sensors/` yields `"sensors"` and `""`.[[nodiscard]]
std::vector<std::string_view> split_levels(std::string_view text) {
    std::vector<std::string_view> levels;
    std::size_t start = 0;
    while (true) {
        const std::size_t slash = text.find('/', start);
        if (slash == std::string_view::npos) {
            levels.push_back(text.substr(start));
            return levels;
        }
        levels.push_back(text.substr(start, slash - start));
        start = slash + 1;
    }
}

/// @brief Joins the levels with `/`, rendering each one as the filter's wire form spells it.
[[nodiscard]]
std::string join_subscription_pattern(const std::vector<topic_pattern_item>& segments) {
    std::string joined;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        if (index > 0) { joined.push_back('/'); }
        joined += segments[index].as_str();
    }
    return joined;
}

/// @brief Joins the levels with `/`, rendering each one as the filter's template form spells it.
[[nodiscard]]
std::string join_template_pattern(const std::vector<topic_pattern_item>& segments) {
    std::string joined;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        if (index > 0) { joined.push_back('/'); }
        joined += segments[index].as_wildcard();
    }
    return joined;
}

/// @brief The position of the first `#` level, or `npos` when the filter has none.
[[nodiscard]]
std::size_t first_hash_position(const std::vector<topic_pattern_item>& segments) {
    for (std::size_t index = 0; index < segments.size(); ++index) {
        if (segments[index].is_hash()) { return index; }
    }
    return std::string::npos;
}

/// @brief Replaces every bound `+` parameter in `segments` with its bound value.
/// @details A named `#` is located but never substituted: it already renders as an anonymous `#`, and no
/// single
/// value could stand for the levels it swallows. Matching it here keeps the lookup from reporting a parameter that
/// plainly does exist.
[[nodiscard]]
std::vector<topic_pattern_item> resolve_bindings(
    const std::vector<topic_pattern_item>& segments, const std::vector<std::pair<std::string, std::string>>& bindings
) {
    std::vector<topic_pattern_item> resolved = segments;
    for (const auto& [name, value] : bindings) {
        const auto bound = std::find_if(resolved.begin(), resolved.end(), [&name](const topic_pattern_item& segment) {
            return segment.is_wildcard() && segment.param_name() == name;
        });
        if (bound == resolved.end()) {
            spdlog::debug("Parameter '{}' not found in the pattern it is bound to", name);
            continue;
        }
        if (bound->is_plus()) { *bound = topic_pattern_item::make_str(value); }
    }
    return resolved;
}

/// @brief Rejects a `#` that is not the last level, which MQTT does not allow.
[[nodiscard]]
std::optional<topic_pattern_error> check_hash_position(
    const std::vector<topic_pattern_item>& segments, const std::string& pattern
) {
    const std::size_t hash_position = first_hash_position(segments);
    if (hash_position != std::string::npos && hash_position != segments.size() - 1) {
        return make_hash_position_error(pattern);
    }
    return std::nullopt;
}

} // namespace

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
    // `{name:#}` is the named `#`; `{name}` the named `+`. A level whose brackets do not pair up
    // is neither, and falls through to the literal branch below; an empty name is an error.
    if (segment.starts_with('{') && segment.ends_with(":#}")) {
        const auto name = parameter_name(segment, ":#}");
        if (!name) { return make_str(std::string { segment }); }
        if (name->empty()) { return make_wildcard_usage_error(std::string { segment }); }
        return make_named_hash(std::string { *name });
    }
    if (segment.starts_with('{') && segment.ends_with('}')) {
        const auto name = parameter_name(segment, "}");
        if (!name) { return make_str(std::string { segment }); }
        if (name->empty()) { return make_wildcard_usage_error(std::string { segment }); }
        return make_named_plus(std::string { *name });
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

topic_pattern_path::topic_pattern_path(
    std::string template_pattern, std::string subscription_pattern, std::vector<topic_pattern_item> segments,
    std::vector<std::pair<std::string, std::string>> bindings, std::shared_ptr<match_cache> cache
)
    : template_pattern_(std::move(template_pattern))
    , subscription_pattern_(std::move(subscription_pattern))
    , segments_(std::move(segments))
    , bindings_(std::move(bindings))
    , cache_(std::move(cache)) {}

pattern_result<topic_pattern_path> topic_pattern_path::parse(std::string pattern, cache_strategy strategy) {
    if (const auto cause = check_wellformed(pattern)) { return make_topic_pattern_error(*cause); }

    // Index into `pattern` as it stands: the levels are parsed before anything moves it.
    std::vector<topic_pattern_item> segments;
    for (const std::string_view level : split_levels(pattern)) {
        auto item = topic_pattern_item::parse(level);
        if (!item) { return item.error(); }
        segments.push_back(std::move(item).value());
    }

    // A parameter may be named once per filter; a second `{id}` would make `named_param("id")`
    // ambiguous.
    std::vector<std::string_view> seen_names;
    for (const topic_pattern_item& segment : segments) {
        if (!segment.param_name().has_value()) { continue; }
        if (std::find(seen_names.begin(), seen_names.end(), *segment.param_name()) != seen_names.end()) {
            return make_wildcard_usage_error(segment.as_str());
        }
        seen_names.push_back(*segment.param_name());
    }

    if (const auto hash_error = check_hash_position(segments, pattern)) { return *hash_error; }

    std::shared_ptr<match_cache> cache;
#ifdef BA7LYA_TOPIC_NO_LRU_CACHE
    if (strategy.is_lru()) {
        spdlog::warn(
            "LRU cache strategy requested for topic pattern '{}' with capacity {}, but support for it is compiled "
            "out. No caching will be used.",
            pattern, strategy.capacity().value_or(0)
        );
    }
#else
    if (const auto capacity = strategy.capacity()) { cache = std::make_shared<match_cache>(*capacity); }
#endif
    // Build the parts first: a braced-init-list gives no evaluation order, so joining `segments`
    // in the same expression that moves it into the constructor would be indeterminate.
    std::string subscription_pattern = join_subscription_pattern(segments);
    return topic_pattern_path {
        std::move(pattern), std::move(subscription_pattern), std::move(segments), {}, std::move(cache),
    };
}

pattern_result<topic_pattern_path> topic_pattern_path::parse(std::string pattern) {
    return parse(std::move(pattern), cache_strategy::no_cache());
}

pattern_result<topic_pattern_path> topic_pattern_path::make_from_segments(
    std::vector<topic_pattern_item> segments, cache_strategy strategy
) {
    const std::string template_pattern = join_template_pattern(segments);
    if (const auto hash_error = check_hash_position(segments, template_pattern)) { return *hash_error; }

    std::shared_ptr<match_cache> cache;
#ifndef BA7LYA_TOPIC_NO_LRU_CACHE
    if (const auto capacity = strategy.capacity()) { cache = std::make_shared<match_cache>(*capacity); }
#endif

    std::string subscription_pattern = join_subscription_pattern(segments);
    return topic_pattern_path {
        std::move(template_pattern), std::move(subscription_pattern), std::move(segments), {}, std::move(cache),
    };
}

std::string topic_pattern_path::subscription_pattern_of(const std::vector<topic_pattern_item>& segments) {
    return join_subscription_pattern(segments);
}

std::string topic_pattern_path::template_pattern_of(const std::vector<topic_pattern_item>& segments) {
    return join_template_pattern(segments);
}

cache_strategy topic_pattern_path::caching() const {
    if (cache_ == nullptr) { return cache_strategy::no_cache(); }
    return cache_strategy::lru(cache_->capacity());
}

std::optional<std::vector<std::pair<std::string, std::string>>> topic_pattern_path::parameter_bindings() const {
    if (bindings_.empty()) { return std::nullopt; }
    return bindings_;
}

std::optional<std::string> topic_pattern_path::bound_value(const std::string& name) const {
    for (const auto& [binding_name, value] : bindings_) {
        if (binding_name == name) { return value; }
    }
    return std::nullopt;
}

std::vector<topic_pattern_item> topic_pattern_path::resolve_bound_segments() const {
    return resolve_bindings(segments_, bindings_);
}

std::string topic_pattern_path::mqtt_pattern() const {
    if (bindings_.empty()) { return subscription_pattern_; }
    return join_subscription_pattern(resolve_bound_segments());
}

bool topic_pattern_path::contains_hash() const noexcept { return !segments_.empty() && segments_.back().is_hash(); }

result<std::string, topic_format_failure> topic_pattern_path::format_topic(
    const std::vector<std::string>& values
) const {
    const std::size_t wildcard_count = static_cast<std::size_t>(std::count_if(
        segments_.begin(), segments_.end(), [](const topic_pattern_item& item) { return item.is_wildcard(); }
    ));

    if (values.size() != wildcard_count) { return parameter_count_mismatch_error { wildcard_count, values.size() }; }

    std::string formatted;
    std::size_t value_index = 0;
    for (std::size_t index = 0; index < segments_.size(); ++index) {
        const topic_pattern_item& segment = segments_[index];
        if (index > 0) { formatted.push_back('/'); }
        if (segment.is_literal()) {
            formatted += segment.literal();
            continue;
        }
        if (segment.is_hash()) {
            // Unreachable in a filter built by `parse`, which requires `#` to be last; kept so a
            // filter built from raw segments cannot silently publish a `#`.
            return hash_wildcard_not_supported_error {};
        }
        formatted += values[value_index];
        ++value_index;
    }
    return formatted;
}

pattern_result<topic_pattern_path> topic_pattern_path::check_compatibility(const topic_pattern_path& candidate) const {
    if (candidate.segments_.size() != segments_.size()) {
        return make_pattern_mismatch_error(template_pattern_, candidate.template_pattern_);
    }
    for (std::size_t index = 0; index < segments_.size(); ++index) {
        const topic_pattern_item& expected = segments_[index];
        const topic_pattern_item& provided = candidate.segments_[index];
        // `#` swallows the rest of the topic, so only a `#` is a refinement of one.
        if (provided.is_hash() != expected.is_hash()) {
            return make_pattern_mismatch_error(template_pattern_, candidate.template_pattern_);
        }
        // A literal level pins the text; a `+` admits any single level, literal or wildcard.
        if (expected.is_literal() && (!provided.is_literal() || provided.literal() != expected.literal())) {
            return make_pattern_mismatch_error(template_pattern_, candidate.template_pattern_);
        }
    }
    return candidate;
}

topic_pattern_path topic_pattern_path::inherit_bindings(topic_pattern_path built) const {
    built.bindings_ = bindings_;
    return built;
}

topic_pattern_path topic_pattern_path::with_cache_strategy(cache_strategy strategy) const {
    auto rebuilt = parse(template_pattern_, strategy);
    if (!rebuilt) {
        // Unreachable: a filter that exists has already passed this validation.
        spdlog::error("Re-parsing an already valid topic pattern failed: {}", describe(rebuilt.error()));
        return *this;
    }
    return inherit_bindings(std::move(rebuilt).value());
}

pattern_result<topic_pattern_path> topic_pattern_path::bind_parameter(std::string name, std::string value) const {
    const bool parameter_exists
        = std::any_of(segments_.begin(), segments_.end(), [&name](const topic_pattern_item& segment) {
              return segment.is_wildcard() && segment.param_name() == name;
          });
    if (!parameter_exists) {
        return make_wildcard_usage_error("Parameter '" + name + "' not found in pattern '" + topic_pattern() + "'");
    }

    topic_pattern_path bound = *this;
    const auto existing = std::find_if(bound.bindings_.begin(), bound.bindings_.end(), [&name](const auto& binding) {
        return binding.first == name;
    });
    if (existing != bound.bindings_.end()) { existing->second = std::move(value); }
    else { bound.bindings_.emplace_back(std::move(name), std::move(value)); }
    return bound;
}

result<topic_match, topic_match_error> topic_pattern_path::match_internal(
    const std::shared_ptr<const topic_path>& topic
) const {
    const std::vector<std::string_view>& topic_segments = topic->segments();

    // A bound parameter is matched as a literal, so the walk runs over the levels with the
    // bindings substituted. The common case has no bindings at all, and copying the levels
    // there would only cost the hot path.
    std::vector<topic_pattern_item> resolved;
    const std::vector<topic_pattern_item>* levels = &segments_;
    if (!bindings_.empty()) {
        resolved = resolve_bound_segments();
        levels = &resolved;
    }

    // MQTT §4.7.2: a filter whose first level is an UNBOUND wildcard must not match a topic
    // whose first level begins with `$`. A parameter bound to a `$`-value has become a literal by
    // `resolve_bound_segments` — the trie stores it as an exact child — so judging on the levels
    // actually being walked is what keeps the two matchers agreeing. Deeper wildcards after a
    // literal first level (`$SYS/#`) are unaffected: the guard is strictly level-0.
    if (topic->has_reserved_first_level() && !levels->empty() && levels->front().excludes_dollar_topic()) {
        return dollar_topic_exclusion_error { topic->text() };
    }

    std::vector<std::pair<std::size_t, std::size_t>> param_ranges;
    std::vector<std::pair<std::string, std::pair<std::size_t, std::size_t>>> named_param_ranges;

    std::size_t topic_index = 0;
    for (std::size_t index = 0; index < levels->size(); ++index) {
        const topic_pattern_item& segment = (*levels)[index];
        if (segment.is_hash()) {
            const std::pair<std::size_t, std::size_t> span { topic_index, topic_segments.size() };
            param_ranges.push_back(span);
            if (segment.param_name().has_value()) { named_param_ranges.emplace_back(*segment.param_name(), span); }
            if (index + 1 < levels->size()) { return unexpected_hash_segment_error {}; }
            return topic_match { topic, std::move(param_ranges), std::move(named_param_ranges) };
        }

        if (topic_index >= topic_segments.size()) { return unexpected_end_of_topic_error {}; }

        if (segment.is_plus()) {
            const std::pair<std::size_t, std::size_t> span { topic_index, topic_index + 1 };
            param_ranges.push_back(span);
            if (segment.param_name().has_value()) { named_param_ranges.emplace_back(*segment.param_name(), span); }
            ++topic_index;
            continue;
        }

        if (topic_segments[topic_index] != segment.literal()) {
            return segment_mismatch_error { segment.literal(), std::string { topic_segments[topic_index] },
                                            topic_index };
        }
        ++topic_index;
    }

    if (topic_index < topic_segments.size()) { return unexpected_end_of_pattern_error {}; }
    return topic_match { topic, std::move(param_ranges), std::move(named_param_ranges) };
}

result<std::shared_ptr<const topic_match>, topic_match_error> topic_pattern_path::match(
    std::shared_ptr<const topic_path> topic
) const {
    if (cache_ != nullptr) {
        // The cached pair owns the topic text, so the hit's match points at storage that outlives
        // the `topic_path` the caller handed this call.
        if (auto cached = cache_->get(topic->text())) { return cached->match; }
        auto computed = match_internal(topic);
        if (!computed) { return computed.error(); }
        auto text = std::make_shared<const std::string>(topic->text());
        auto shared = std::make_shared<const topic_match>(std::move(computed).value());
        cache_->put(topic->text(), std::make_shared<const cached_match>(cached_match { std::move(text), shared }));
        return shared;
    }

    auto computed = match_internal(topic);
    if (!computed) { return computed.error(); }
    return std::make_shared<const topic_match>(std::move(computed).value());
}

result<std::shared_ptr<const topic_match>, topic_match_error> topic_pattern_path::match_str(
    const std::string& topic
) const {
    auto parsed = topic_path::parse(topic);
    if (!parsed) { return invalid_topic_error { parsed.error() }; }
    return match(std::move(parsed).value());
}

std::string to_string(const topic_pattern_path& pattern) { return pattern.to_string(); }

} // namespace ba7lya::topic
