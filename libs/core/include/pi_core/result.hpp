// pi_core/result.hpp - Result<T, E> for fallible operations
#pragma once

#include "error.hpp"
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace pi::core {

/// `Result<T>` is either a value or an Error.
/// Mirrors Rust's `Result<T, E>` with E = Error.
template <typename T>
class [[nodiscard]] Result {
public:
    /// Successful result.
    template <typename U,
              typename = std::enable_if_t<std::is_constructible_v<T, U&&>>>
    static Result ok(U&& value) {
        return Result(std::in_place_index<0>, std::forward<U>(value));
    }
    /// Successful result (void specialization).
    static Result ok() requires std::is_void_v<T> { return Result(); }

    /// Failure result.
    static Result err(ErrorKind k, std::string msg) {
        return Result(std::in_place_index<1>, Error{k, std::move(msg)});
    }
    static Result err(Error e) {
        return Result(std::in_place_index<1>, std::move(e));
    }

    /// Implicit constructor from Error: enables `return Error{...};` from
    /// functions returning Result<T>. Wraps as the Err variant.
    Result(Error e) : data_(std::in_place_index<1>, std::move(e)) {}

    /// Implicit constructor from T: enables `return value;` to return Ok(value).
    Result(T value) : data_(std::in_place_index<0>, std::move(value)) {}

    bool is_ok() const noexcept { return data_.index() == 0; }
    bool is_err() const noexcept { return data_.index() == 1; }
    explicit operator bool() const noexcept { return is_ok(); }

    T& value() & {
        if (!is_ok()) throw InternalError("Result::value on Err: " + error().to_string());
        return std::get<0>(data_);
    }
    const T& value() const& {
        if (!is_ok()) throw InternalError("Result::value on Err: " + error().to_string());
        return std::get<0>(data_);
    }
    T&& value() && {
        if (!is_ok()) throw InternalError("Result::value on Err: " + error().to_string());
        return std::move(std::get<0>(data_));
    }

    const Error& error() const& { return std::get<1>(data_); }
    Error& error() & { return std::get<1>(data_); }
    Error&& error() && { return std::move(std::get<1>(data_)); }

    /// Unwrap or return a default.
    T value_or(T default_value) const& requires(!std::is_void_v<T>) {
        return is_ok() ? std::get<0>(data_) : std::move(default_value);
    }

private:
    template <std::size_t I, typename U>
    Result(std::in_place_index_t<I>, U&& v)
        : data_(std::in_place_index<I>, std::forward<U>(v)) {}
    Result() requires std::is_void_v<T> {}

    std::variant<T, Error> data_{};
};

template <>
class Result<void> {
public:
    static Result ok() { return Result{}; }
    static Result err(ErrorKind k, std::string msg) {
        return Result{Error{k, std::move(msg)}};
    }
    static Result err(Error e) { return Result{std::move(e)}; }

    bool is_ok() const noexcept { return !err_.has_value(); }
    bool is_err() const noexcept { return err_.has_value(); }
    explicit operator bool() const noexcept { return is_ok(); }

    void value() const {
        if (err_) throw InternalError("Result::value on Err: " + err_->to_string());
    }
    const Error& error() const { return *err_; }

    /// Implicit construction from Error.
    Result(Error e) : err_(std::move(e)) {}

private:
    Result() = default;
    std::optional<Error> err_;
};

/// Helper macro for early return on error.
#define PI_TRY(expr) \
    do { \
        auto _pi_result = (expr); \
        if (!_pi_result) return _pi_result.error(); \
    } while (0)

/// Helper macro for assigning the value or returning the error.
#define PI_TRY_ASSIGN(var, expr) \
    auto _pi_result_##__LINE__ = (expr); \
    if (!_pi_result_##__LINE__) return _pi_result_##__LINE__.error(); \
    auto& var = _pi_result_##__LINE__.value()

}  // namespace pi::core
