// pi_core/error.hpp - Error types
#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <utility>

namespace pi::core {

/// Categories of errors that can occur in pi's libraries.
enum class ErrorKind {
    None = 0,
    InvalidArgument,
    NotFound,
    Permission,
    Io,
    Timeout,
    Cancelled,
    Network,
    Tls,
    Http,
    Protocol,
    JsonFormat,
    Config,
    Auth,
    NotImplemented,
    Overflow,
    Internal,
};

/// Error code information.
struct ErrorCategory {
    static constexpr const char* name(ErrorKind k) noexcept {
        switch (k) {
            case ErrorKind::None:            return "none";
            case ErrorKind::InvalidArgument: return "invalid_argument";
            case ErrorKind::NotFound:        return "not_found";
            case ErrorKind::Permission:      return "permission";
            case ErrorKind::Io:              return "io";
            case ErrorKind::Timeout:         return "timeout";
            case ErrorKind::Cancelled:       return "cancelled";
            case ErrorKind::Network:         return "network";
            case ErrorKind::Tls:             return "tls";
            case ErrorKind::Http:            return "http";
            case ErrorKind::Protocol:        return "protocol";
            case ErrorKind::JsonFormat:      return "json";
            case ErrorKind::Config:          return "config";
            case ErrorKind::Auth:            return "auth";
            case ErrorKind::NotImplemented:  return "not_implemented";
            case ErrorKind::Overflow:        return "overflow";
            case ErrorKind::Internal:        return "internal";
        }
        return "unknown";
    }
};

/// An Error value. Cheap to copy.
struct Error {
    ErrorKind kind{ErrorKind::None};
    std::string message;

    constexpr Error() noexcept = default;
    constexpr Error(ErrorKind k, std::string msg) noexcept
        : kind(k), message(std::move(msg)) {}

    constexpr bool ok() const noexcept { return kind == ErrorKind::None; }
    constexpr explicit operator bool() const noexcept { return !ok(); }

    std::string to_string() const {
        return std::string(ErrorCategory::name(kind)) + ": " + message;
    }
};

inline Error make_error(ErrorKind k, std::string_view msg) {
    return Error(k, std::string(msg));
}

/// Exception for unexpected internal failures (programming bugs).
/// Library code should not catch these; they signal bugs.
class InternalError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}  // namespace pi::core
