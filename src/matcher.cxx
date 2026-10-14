///
/// @file matcher.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Implementation of topic names and match results.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///
// SPDX-License-Identifier: MIT

#include "matcher.hxx"

#include <utility>

#include "error.hxx"

namespace ba7lya::topic {

namespace {

/// @brief Splits `text` on `/`, returning one view per level.
/// @details The split is total: `a/b` yields two levels and `a/` yields `"a"` and `""`, matching the
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

} // namespace ba7lya::topic
