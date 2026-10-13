// SPDX-License-Identifier: MIT

#include "topic_pattern_path.hxx"

#include <algorithm>
#include <iterator>
#include <spdlog/spdlog.h>
#include <utility>

namespace ba7lya::topic_engine {

namespace {

/// Splits `text` on `/`, returning one view per level.
///
/// Mirrors Rust's `str::split`: the split is total, so `sensors/` yields `"sensors"` and `""`.
[[nodiscard]]
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

/// Joins the levels with `/`, rendering each one as the filter's wire form spells it.
[[nodiscard]]
std::string join_subscription_pattern(const std::vector<topic_pattern_item>& segments) {
    std::string joined;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        if (index > 0) { joined.push_back('/'); }
        joined += segments[index].as_str();
    }
    return joined;
}

/// Joins the levels with `/`, rendering each one as the filter's template form spells it.
[[nodiscard]]
std::string join_template_pattern(const std::vector<topic_pattern_item>& segments) {
    std::string joined;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        if (index > 0) { joined.push_back('/'); }
        joined += segments[index].as_wildcard();
    }
    return joined;
}

/// The position of the first `#` level, or `npos` when the filter has none.
[[nodiscard]]
std::size_t first_hash_position(const std::vector<topic_pattern_item>& segments) {
    for (std::size_t index = 0; index < segments.size(); ++index) {
        if (segments[index].is_hash()) { return index; }
    }
    return std::string::npos;
}

/// Rejects a `#` that is not the last level, which MQTT does not allow.
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

std::string hash_wildcard_not_supported_error::message() const {
    return "Cannot format topic with # wildcard for publishing";
}

std::string parameter_count_mismatch_error::message() const {
    return "Parameter count mismatch: expected " + std::to_string(expected) + ", provided " + std::to_string(provided);
}

std::string topic_format_error::message() const { return "Error formatting topic"; }

std::string describe(const topic_format_failure& error) {
    return std::visit([](const auto& concrete) { return concrete.message(); }, error);
}

std::shared_ptr<const topic_match> topic_pattern_path::match_cache::get(const std::string& topic) {
    const std::lock_guard<std::mutex> guard { mutex_ };
    const auto entry = entries_.find(topic);
    if (entry == entries_.end()) { return nullptr; }
    entry->second.last_used = ++clock_;
    return entry->second.matched;
}

void topic_pattern_path::match_cache::put(const std::string& topic, std::shared_ptr<const topic_match> matched) {
    const std::lock_guard<std::mutex> guard { mutex_ };
    if (const auto entry = entries_.find(topic); entry != entries_.end()) {
        entry->second.matched = std::move(matched);
        entry->second.last_used = ++clock_;
        return;
    }
    entries_.insert_or_assign(topic, cache_entry { std::move(matched), ++clock_ });
    evict_over_capacity();
}

void topic_pattern_path::match_cache::evict_over_capacity() {
    while (entries_.size() > capacity_) {
        entries_.erase(std::min_element(entries_.begin(), entries_.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.second.last_used < rhs.second.last_used;
        }));
    }
}

topic_pattern_path::topic_pattern_path(
    std::string template_pattern, std::string subscription_pattern, std::vector<topic_pattern_item> segments,
    std::shared_ptr<shared_state> state
)
    : template_pattern_(std::move(template_pattern))
    , subscription_pattern_(std::move(subscription_pattern))
    , segments_(std::move(segments))
    , state_(std::move(state)) {}

topic_pattern_path::topic_pattern_path(const topic_pattern_path& other)
    : template_pattern_(other.template_pattern_)
    , subscription_pattern_(other.subscription_pattern_)
    , segments_(other.segments_)
    ,
    // The cache is intentionally left behind: a copy starts with an empty one, so its lifetime
    // is bounded by the copy's own usefulness rather than by the original's.
    state_(other.state_ ? std::make_shared<shared_state>(shared_state { nullptr, other.state_->bindings }) : nullptr) {}

topic_pattern_path::topic_pattern_path(topic_pattern_path&& other) noexcept = default;

topic_pattern_path& topic_pattern_path::operator=(const topic_pattern_path& other) {
    if (this != &other) {
        topic_pattern_path copy { other };
        *this = std::move(copy);
    }
    return *this;
}

topic_pattern_path& topic_pattern_path::operator=(topic_pattern_path&& other) noexcept = default;

topic_pattern_path::~topic_pattern_path() = default;

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

    auto state = std::make_shared<shared_state>();
#ifdef BA7LYA_TOPIC_ENGINE_NO_LRU_CACHE
    if (strategy.is_lru()) {
        spdlog::warn(
            "LRU cache strategy requested for topic pattern '{}' with capacity {}, but support for it is compiled "
            "out. No caching will be used.",
            pattern, strategy.capacity().value_or(0)
        );
    }
#else
    if (const auto capacity = strategy.capacity()) { state->cache = std::make_unique<match_cache>(*capacity); }
#endif

    return topic_pattern_path {
        pattern,
        join_subscription_pattern(segments),
        std::move(segments),
        std::move(state),
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

    auto state = std::make_shared<shared_state>();
#ifndef BA7LYA_TOPIC_ENGINE_NO_LRU_CACHE
    if (const auto capacity = strategy.capacity()) { state->cache = std::make_unique<match_cache>(*capacity); }
#endif

    return topic_pattern_path {
        template_pattern,
        join_subscription_pattern(segments),
        std::move(segments),
        std::move(state),
    };
}

std::string topic_pattern_path::subscription_pattern_of(const std::vector<topic_pattern_item>& segments) {
    return join_subscription_pattern(segments);
}

std::string topic_pattern_path::template_pattern_of(const std::vector<topic_pattern_item>& segments) {
    return join_template_pattern(segments);
}

cache_strategy topic_pattern_path::caching() const {
    if (state_ == nullptr || state_->cache == nullptr) { return cache_strategy::no_cache(); }
    return cache_strategy::lru(state_->cache->capacity());
}

std::optional<std::vector<std::pair<std::string, std::string>>> topic_pattern_path::parameter_bindings() const {
    if (state_ == nullptr || state_->bindings.empty()) { return std::nullopt; }
    return state_->bindings;
}

std::optional<std::string> topic_pattern_path::bound_value(const std::string& name) const {
    if (state_ == nullptr) { return std::nullopt; }
    for (const auto& [binding_name, value] : state_->bindings) {
        if (binding_name == name) { return value; }
    }
    return std::nullopt;
}

std::vector<topic_pattern_item> topic_pattern_path::resolve_bound_segments() const {
    std::vector<topic_pattern_item> resolved = segments_;
    if (state_ == nullptr) { return resolved; }

    for (const auto& [name, value] : state_->bindings) {
        const auto bound = std::find_if(resolved.begin(), resolved.end(), [&name](const topic_pattern_item& segment) {
            return segment.is_plus() && segment.param_name() == name;
        });
        if (bound == resolved.end()) {
            spdlog::debug("Parameter '{}' not found in pattern '{}'", name, topic_pattern());
            continue;
        }
        *bound = topic_pattern_item::make_str(value);
    }
    return resolved;
}

std::string topic_pattern_path::mqtt_pattern() const {
    if (state_ == nullptr || state_->bindings.empty()) { return subscription_pattern_; }
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
    std::vector<topic_pattern_item> expected_wildcards;
    std::vector<topic_pattern_item> candidate_wildcards;
    std::copy_if(segments_.begin(), segments_.end(), std::back_inserter(expected_wildcards), [](const auto& item) {
        return item.is_wildcard();
    });
    std::copy_if(
        candidate.segments_.begin(), candidate.segments_.end(), std::back_inserter(candidate_wildcards),
        [](const auto& item) { return item.is_wildcard(); }
    );

    if (expected_wildcards != candidate_wildcards) {
        return make_pattern_mismatch_error(template_pattern_, candidate.template_pattern_);
    }
    return candidate;
}

topic_pattern_path topic_pattern_path::inherit_bindings(topic_pattern_path built) const {
    if (state_ != nullptr) {
        built.state_
            = std::make_shared<shared_state>(shared_state { std::move(built.state_->cache), state_->bindings });
    }
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
              return segment.is_plus() && segment.param_name() == name;
          });
    if (!parameter_exists) {
        return make_wildcard_usage_error("Parameter '" + name + "' not found in pattern '" + topic_pattern() + "'");
    }

    topic_pattern_path bound = *this;
    if (bound.state_ == nullptr) { bound.state_ = std::make_shared<shared_state>(); }
    else {
        bound.state_
            = std::make_shared<shared_state>(shared_state { std::move(bound.state_->cache), bound.state_->bindings });
    }
    const auto existing
        = std::find_if(bound.state_->bindings.begin(), bound.state_->bindings.end(), [&name](const auto& binding) {
              return binding.first == name;
          });
    if (existing != bound.state_->bindings.end()) { existing->second = std::move(value); }
    else { bound.state_->bindings.emplace_back(std::move(name), std::move(value)); }
    return bound;
}

bool topic_pattern_path::first_level_is_unbound_wildcard() const {
    if (segments_.empty()) { return false; }
    const topic_pattern_item& first = segments_.front();
    if (!first.is_wildcard()) { return false; }
    if (!first.param_name().has_value()) { return true; }
    return !bound_value(*first.param_name()).has_value();
}

result<topic_match, topic_match_error> topic_pattern_path::match_internal(
    const std::shared_ptr<const topic_path>& topic
) const {
    const std::vector<std::string_view>& topic_segments = topic->segments();

    // MQTT §4.7.2: a filter whose first level is an UNBOUND wildcard must not match a topic
    // whose first level begins with `$`. A parameter bound to a `$`-value is a concrete
    // literal — the trie stores it as an exact child — so judging on the parsed levels alone
    // would make the two matchers disagree. Deeper wildcards after a literal first level
    // (`$SYS/#`) are unaffected: the guard is strictly level-0.
    if (!topic_segments.empty() && is_dollar_topic(topic_segments.front()) && first_level_is_unbound_wildcard()) {
        return dollar_topic_exclusion_error { topic->text() };
    }

    std::vector<std::pair<std::size_t, std::size_t>> param_ranges;
    std::vector<std::pair<std::string, std::pair<std::size_t, std::size_t>>> named_param_ranges;

    std::size_t topic_index = 0;
    for (std::size_t index = 0; index < segments_.size(); ++index) {
        const topic_pattern_item& segment = segments_[index];
        if (segment.is_hash()) {
            const std::pair<std::size_t, std::size_t> span { topic_index, topic_segments.size() };
            param_ranges.push_back(span);
            if (segment.param_name().has_value()) { named_param_ranges.emplace_back(*segment.param_name(), span); }
            if (index + 1 < segments_.size()) { return unexpected_hash_segment_error {}; }
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
    if (state_ != nullptr && state_->cache != nullptr) {
        if (auto cached = state_->cache->get(topic->text())) { return cached; }
        auto computed = match_internal(topic);
        if (!computed) { return computed.error(); }
        auto shared = std::make_shared<const topic_match>(std::move(computed).value());
        state_->cache->put(topic->text(), shared);
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

} // namespace ba7lya::topic_engine
