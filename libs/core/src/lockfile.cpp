// libs/core/src/lockfile.cpp
#include "pi_core/lockfile.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <sys/file.h>

namespace pi::core::lockfile {

Lock::Lock(Lock&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
Lock& Lock::operator=(Lock&& o) noexcept {
    if (this != &o) {
        release();
        fd_ = o.fd_;
        o.fd_ = -1;
    }
    return *this;
}

Lock::~Lock() { release(); }

void Lock::release() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

FileLock::FileLock(std::string_view path, bool use_path_direct)
    : path_(use_path_direct ? std::string(path) : std::string(path) + ".lock") {}

std::optional<Lock> FileLock::try_acquire() noexcept {
    int fd = ::open(path_.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd < 0) return std::nullopt;
    struct flock fl{};
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    // Prefer OFD locks (Linux 3.15+): per-open-file-description, not per-process.
    // This makes same-process re-locking fail like cross-process locking does,
    // matching the semantics of proper-lockfile's flock() based locks.
#if defined(F_OFD_SETLK)
    if (::fcntl(fd, F_OFD_SETLK, &fl) != 0) {
        int saved = errno;
        ::close(fd);
        errno = saved;
        return std::nullopt;
    }
#else
    if (::fcntl(fd, F_SETLK, &fl) != 0) {
        int saved = errno;
        ::close(fd);
        errno = saved;
        return std::nullopt;
    }
#endif
    Lock lk;
    lk.fd_ = fd;
    return lk;
}
std::optional<Lock> FileLock::acquire(std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        if (auto lk = try_acquire(); lk) return lk;
        if (std::chrono::steady_clock::now() >= deadline) return std::nullopt;
        struct timespec ts{0, 50 * 1000 * 1000};  // 50ms
        ::nanosleep(&ts, nullptr);
    }
}

}  // namespace pi::core::lockfile
