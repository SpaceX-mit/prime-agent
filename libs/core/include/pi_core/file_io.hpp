// pi_core/file_io.hpp - File I/O helpers
#pragma once

#include "result.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pi::core::file {

/// Read an entire file as text. Binary-safe (returns bytes).
Result<std::string> read(std::string_view path);

/// Read an entire file as bytes.
Result<std::vector<uint8_t>> read_bytes(std::string_view path);

/// Write text to a file, creating parent directories if needed.
/// Atomic write: writes to "<path>.tmp.<pid>" then renames.
Result<void> write_atomic(std::string_view path, std::string_view content);

/// Append text to a file (creating if missing).
Result<void> append(std::string_view path, std::string_view content);

/// True if the path exists and is a regular file.
bool exists(std::string_view path);

/// True if the path exists and is a directory.
bool is_directory(std::string_view path);

/// File size in bytes, or -1 on error.
int64_t size(std::string_view path);

/// Create directory (and parents) with the given mode (umask-affected).
Result<void> create_directories(std::string_view path);

/// Recursively list all files under `dir`, skipping entries in `ignore_globs`.
/// Returns absolute paths.
Result<std::vector<std::string>> list_files_recursive(
    std::string_view dir,
    const std::vector<std::string>& ignore_globs = {},
    int max_depth = 64);

}  // namespace pi::core::file
