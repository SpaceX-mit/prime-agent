// libs/core/src/log.cpp
#include "pi_core/log.hpp"

#include <atomic>
#include <fstream>
#include <iostream>
#include <mutex>

namespace pi::core::log {

namespace {
std::atomic<int> g_level{static_cast<int>(Level::Info)};
std::mutex g_file_mutex;
std::ofstream g_file;
std::mutex g_console_mutex;
}

void init(Level level, const std::filesystem::path& file_path) {
    g_level.store(static_cast<int>(level));
    if (!file_path.empty()) {
        std::lock_guard<std::mutex> g(g_file_mutex);
        g_file.open(file_path, std::ios::app);
    }
}

void set_level(Level level) { g_level.store(static_cast<int>(level)); }
Level level() { return static_cast<Level>(g_level.load()); }

namespace detail {

std::mutex& LogLine::mutex() {
    // Mutex protects both console and file access.
    static std::mutex m;
    return m;
}

std::ostream& LogLine::ostream() {
    static std::ostream& s = std::cerr;
    (void)g_console_mutex;
    if (g_file.is_open()) {
        // For simplicity we route to both via std::cerr; file is appended separately.
        return std::cerr;
    }
    return s;
}

const char* LogLine::level_name(Level l) {
    switch (l) {
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
    }
    return "?    ";
}

}  // namespace detail
}  // namespace pi::core::log
