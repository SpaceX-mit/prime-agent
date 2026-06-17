// libs/core/src/file_io.cpp
#include "pi_core/file_io.hpp"
#include "pi_core/error.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace pi::core::file {

static Error err_from_errno(ErrorKind kind, const std::string& ctx) {
    return Error{kind, ctx + ": " + std::strerror(errno)};
}

Result<std::string> read(std::string_view path) {
    auto bytes = read_bytes(path);
    if (!bytes) return bytes.error();
    return std::string(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size());
}

Result<std::vector<uint8_t>> read_bytes(std::string_view path) {
    std::vector<uint8_t> out;
    std::ifstream f{std::string(path), std::ios::binary | std::ios::ate};
    if (!f) return err_from_errno(ErrorKind::Io, "read_bytes: " + std::string(path));
    auto end = f.tellg();
    if (end < 0) return err_from_errno(ErrorKind::Io, "read_bytes tellg: " + std::string(path));
    out.resize(static_cast<size_t>(end));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(out.data()), out.size())) {
        return err_from_errno(ErrorKind::Io, "read_bytes read: " + std::string(path));
    }
    return out;
}

Result<void> write_atomic(std::string_view path, std::string_view content) {
    std::string p{path};
    fs::path fp(p);
    if (auto parent = fp.parent_path(); !parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
        if (ec) return Error{ErrorKind::Io, "create parent: " + parent.string() + ": " + ec.message()};
    }
    std::string tmp = p + ".tmp." + std::to_string(::getpid());
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return err_from_errno(ErrorKind::Io, "write_atomic open tmp: " + tmp);
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!f) return err_from_errno(ErrorKind::Io, "write_atomic write tmp: " + tmp);
        f.flush();
        if (!f) return err_from_errno(ErrorKind::Io, "write_atomic flush tmp: " + tmp);
    }
    if (::rename(tmp.c_str(), p.c_str()) != 0) {
        int saved = errno;
        ::unlink(tmp.c_str());
        errno = saved;
        return err_from_errno(ErrorKind::Io, "write_atomic rename: " + p);
    }
    return Result<void>::ok();
}

Result<void> append(std::string_view path, std::string_view content) {
    std::ofstream f{std::string(path), std::ios::binary | std::ios::app};
    if (!f) return err_from_errno(ErrorKind::Io, "append open: " + std::string(path));
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!f) return err_from_errno(ErrorKind::Io, "append write: " + std::string(path));
    return Result<void>::ok();
}

bool exists(std::string_view path) {
    std::error_code ec;
    return fs::is_regular_file(std::string(path), ec);
}

bool is_directory(std::string_view path) {
    std::error_code ec;
    return fs::is_directory(std::string(path), ec);
}

int64_t size(std::string_view path) {
    std::error_code ec;
    auto sz = fs::file_size(std::string(path), ec);
    return ec ? -1 : static_cast<int64_t>(sz);
}

Result<void> create_directories(std::string_view path) {
    std::error_code ec;
    fs::create_directories(std::string(path), ec);
    if (ec) return Error{ErrorKind::Io, "create_directories: " + std::string(path) + ": " + ec.message()};
    return Result<void>::ok();
}

Result<std::vector<std::string>> list_files_recursive(
    std::string_view dir,
    const std::vector<std::string>& /*ignore_globs*/,
    int /*max_depth*/) {
    // TODO(phase 5): implement glob-based ignore and depth limit
    std::vector<std::string> out;
    std::error_code ec;
    auto it = fs::recursive_directory_iterator(std::string(dir),
                                               fs::directory_options::skip_permission_denied, ec);
    if (ec) return Error{ErrorKind::Io, "list_files_recursive: " + std::string(dir) + ": " + ec.message()};
    for (const auto& e : it) {
        if (e.is_regular_file(ec)) out.push_back(e.path().string());
    }
    return out;
}

}  // namespace pi::core::file
