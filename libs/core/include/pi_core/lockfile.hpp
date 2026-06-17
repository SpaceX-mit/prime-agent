// pi_core/lockfile.hpp - Cross-process file locking
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace pi::core::lockfile {

/// A held file lock. Releases on destruction.
class Lock {
public:
    Lock() noexcept = default;
    Lock(Lock&&) noexcept;
    Lock& operator=(Lock&&) noexcept;
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    ~Lock();

    bool held() const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return held(); }

    void release() noexcept;

private:
    friend class FileLock;
    int fd_ = -1;
};

/// FileLock manages POSIX flock(2)-based file locks for cross-process serialization.
/// All locks are advisory (non-mandatory).
class FileLock {
public:
    /// Construct a (not-yet-held) lock for `path`.
    /// The lock file is `<path>.lock` unless `use_path_direct` is true.
    explicit FileLock(std::string_view path, bool use_path_direct = false);
    ~FileLock() = default;

    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

    /// Try to acquire the lock without blocking. Returns nullopt on failure.
    std::optional<Lock> try_acquire() noexcept;

    /// Block up to `timeout` trying to acquire the lock.
    /// Returns nullopt on timeout / failure.
    std::optional<Lock> acquire(std::chrono::milliseconds timeout = std::chrono::seconds(30));

    /// Run `fn` while holding the lock. Releases the lock afterwards.
    /// Returns std::nullopt if the lock could not be acquired within `timeout`.
    template <typename Fn>
    auto with_lock(Fn&& fn, std::chrono::milliseconds timeout = std::chrono::seconds(30))
        -> std::optional<decltype(fn())> {
        auto guard = acquire(timeout);
        if (!guard) return std::nullopt;
        return fn();
    }

    const std::string& path() const noexcept { return path_; }

private:
    std::string path_;
};

}  // namespace pi::core::lockfile
