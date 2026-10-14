///
/// @file result.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Value-based fallible result, the C++ stand-in for the Rust `Result<T, E>` the engine was ported from.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///
// SPDX-License-Identifier: MIT

#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <variant>

namespace ba7lya::topic {

///
/// @brief A value or an error, never both.
/// @tparam T The success type.
/// @tparam E The error type, usually a `std::variant` of concrete error structs.
/// @details `std::expected` would be the natural spelling, but it is missing from MSVC 14.29 (VS2019), which the
/// project's CI `std::expected` would be the natural spelling, but it is missing from MSVC 14.29 (VS2019), which the
/// project's CI matrix still builds with, so the two-state result is written here instead of relying on the C++23
/// library type.
/// @note Constructing from `T` produces a value and constructing from `E` produces an error, so a function body can
/// simply Constructing from `T` produces a value and constructing from `E` produces an error, so a function body can
/// simply `return some_value;` or `return some_error;`. Accessing the wrong alternative is undefined, which mirrors the
/// Rust original's panicking accessors.
///
template<typename T, typename E>
class result {
public:
    /// @brief Wraps `value` as a success.
    /// @param value The value to hold.
    constexpr result(T value)
        : storage_(std::in_place_index<0>, std::move(value)) {}

    /// @brief Wraps `error` as a failure.
    /// @param error The error to hold.
    constexpr result(E error)
        : storage_(std::in_place_index<1>, std::move(error)) {}

    /// @brief Wraps one alternative of a variant-typed `E` as a failure.
    /// @details Without this a caller would have to spell the variant type at every return site - the ported code
    /// returns the Without this a caller would have to spell the variant type at every return site - the ported code
    /// returns the concrete error struct, as the Rust original does.
    /// @tparam G The concrete error struct, deduced from the argument.
    /// @param error The alternative to convert into `E`.
    template<typename G>
        requires(
            !std::same_as<std::remove_cvref_t<G>, T> && !std::same_as<std::remove_cvref_t<G>, result>
            && std::constructible_from<E, G>
        )
    constexpr result(G&& error)
        : storage_(std::in_place_index<1>, E { std::forward<G>(error) }) {}

    /// @brief Returns `true` when this result holds a value.
    /// @details Spelled out as well as the bool conversion because that one is `explicit`, so `has_value()` is the name
    /// a caller reaches for when writing it as a plain predicate.
    /// @return `true` for a success, `false` for a failure.
    [[nodiscard]]
    constexpr bool has_value() const noexcept {
        return storage_.index() == 0;
    }

    /// @brief Returns `true` when this result holds a value, so a result can be tested directly.
    /// @return `true` for a success, `false` for a failure.
    [[nodiscard]]
    constexpr explicit operator bool() const noexcept {
        return has_value();
    }

    /// @brief Returns the held value; undefined when this result holds an error.
    /// @return A reference to the held value.
    [[nodiscard]]
    constexpr T& value() & noexcept {
        return std::get<0>(storage_);
    }

    /// @brief Returns the held value; undefined when this result holds an error.
    /// @return A reference to the held value.
    [[nodiscard]]
    constexpr const T& value() const& noexcept {
        return std::get<0>(storage_);
    }

    /// @brief Returns the held value, moving it out; undefined when this result holds an error.
    /// @return An rvalue reference to the held value.
    [[nodiscard]]
    constexpr T&& value() && noexcept {
        return std::move(std::get<0>(storage_));
    }

    /// @brief Returns the held error; undefined when this result holds a value.
    /// @return A reference to the held error.
    [[nodiscard]]
    constexpr E& error() & noexcept {
        return std::get<1>(storage_);
    }

    /// @brief Returns the held error; undefined when this result holds a value.
    /// @return A reference to the held error.
    [[nodiscard]]
    constexpr const E& error() const& noexcept {
        return std::get<1>(storage_);
    }

    /// @brief Returns the held value, like `value()`; undefined when this result holds an error.
    /// @return A reference to the held value.
    [[nodiscard]]
    constexpr T& operator*() & noexcept {
        return value();
    }

    /// @brief Returns the held value, like `value()`; undefined when this result holds an error.
    /// @return A reference to the held value.
    [[nodiscard]]
    constexpr const T& operator*() const& noexcept {
        return value();
    }

    /// @brief Returns the held value, like `value()`; undefined when this result holds an error.
    /// @return An rvalue reference to the held value.
    [[nodiscard]]
    constexpr T&& operator*() && noexcept {
        return std::move(*this).value();
    }

    /// @brief Returns a pointer to the held value; undefined when this result holds an error.
    /// @return A pointer to the held value.
    [[nodiscard]]
    constexpr T* operator->() noexcept {
        return &value();
    }

    /// @brief Returns a pointer to the held value; undefined when this result holds an error.
    /// @return A pointer to the held value.
    [[nodiscard]]
    constexpr const T* operator->() const noexcept {
        return &value();
    }

private:
    std::variant<T, E> storage_;
};

} // namespace ba7lya::topic
