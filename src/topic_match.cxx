// SPDX-License-Identifier: MIT

#include "topic_match.hxx"

#include <utility>

#include "topic_wellformedness.hxx"

namespace ba7lya::topic_engine {

namespace {

/// Splits `text` on `/`, returning one view per level.
///
/// The split is total: `a/b` yields two levels and `a/` yields `"a"` and `""`, matching the
/// Rust original's `str::split`.
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

} // namespace

std::string empty_topic_path_error::message() const { return "Topic cannot be empty"; }

std::string null_char_topic_path_error::message() const { return "Topic must not contain the null character (U+0000)"; }

topic_path_error make_topic_path_error(const malformed& cause) {
    if (std::holds_alternative<null_char_error>(cause)) { return null_char_topic_path_error {}; }
    return empty_topic_path_error {};
}

std::string describe(const topic_path_error& error) {
    return std::visit([](const auto& concrete) { return concrete.message(); }, error);
}

topic_path::topic_path(std::string path)
    : path_(std::move(path)) {
    // Index into the stored string, not the argument: moving a short string may hand over its
    // inline buffer, which would dangle every view taken before the move.
    segments_ = split_levels(path_);
}

topic_path_result topic_path::parse(std::string path) {
    if (const auto cause = check_wellformed(path)) { return make_topic_path_error(*cause); }
    // NOLINTNEXTLINE(modernize-make-shared): the constructor is private, so make_shared cannot see it.
    return std::shared_ptr<const topic_path> { new topic_path { std::move(path) } };
}

std::string unexpected_end_of_pattern_error::message() const {
    return "Pattern ended unexpectedly while matching topic";
}

std::string unexpected_end_of_topic_error::message() const { return "Topic ended unexpectedly while matching pattern"; }

std::string unexpected_hash_segment_error::message() const { return "Hash wildcard (#) found in unexpected position"; }

std::string segment_mismatch_error::message() const {
    return "Segment mismatch at position " + std::to_string(position) + ": expected '" + expected + "', found '" + found
         + "'";
}

std::string duplicate_parameter_name_error::message() const { return "Duplicate parameter name found in pattern"; }

std::string dollar_topic_exclusion_error::message() const {
    return "Topic filter with a leading wildcard cannot match reserved $-topic '" + topic + "' (MQTT §4.7.2)";
}

std::string describe(const topic_match_error& error) {
    return std::visit([](const auto& concrete) { return concrete.message(); }, error);
}

std::string_view topic_match::range_text(std::size_t start, std::size_t end) const {
    const auto& levels = topic_->segments();
    if (start >= end || end > levels.size()) { return {}; }

    const std::string& text = topic_->text();
    const char* const base = text.data();
    const std::string_view last = levels[end - 1];
    const std::size_t begin = static_cast<std::size_t>(levels[start].data() - base);
    const std::size_t finish = static_cast<std::size_t>(last.data() - base) + last.size();
    return std::string_view { base + begin, finish - begin };
}

std::string_view topic_match::param(std::size_t index) const {
    if (index >= param_ranges_.size()) { return {}; }
    const auto [start, end] = param_ranges_[index];
    return range_text(start, end);
}

std::optional<std::string_view> topic_match::named_param(std::string_view name) const {
    for (const auto& [candidate, span] : named_param_ranges_) {
        if (candidate == name) { return range_text(span.first, span.second); }
    }
    return std::nullopt;
}

std::string topic_match::to_string() const {
    std::string rendered = "Match(" + topic_->text() + ")";
    if (!param_ranges_.empty()) { rendered += " with " + std::to_string(param_ranges_.size()) + " params"; }
    return rendered;
}

std::string to_string(const topic_match& matched) { return matched.to_string(); }

} // namespace ba7lya::topic_engine
