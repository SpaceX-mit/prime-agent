// pi_core/log.hpp - Simple logging
#pragma once

#include <filesystem>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

namespace pi::core::log {

enum class Level { Debug = 0, Info, Warn, Error };

/// Initialize the global logger.
/// If `file_path` is set, log lines are also appended to that file (synchronously).
/// If `level` is set, lower-level messages are dropped.
void init(Level level = Level::Info, const std::filesystem::path& file_path = {});

void set_level(Level level);
Level level();

namespace detail {
class LogLine {
public:
    LogLine(Level lvl, std::string_view file, int line) : lvl_(lvl) {
        // Strip leading path components from file.
        auto slash = file.find_last_of("/\\");
        src_ = slash == std::string_view::npos ? std::string(file)
                                              : std::string(file.substr(slash + 1));
        line_no_ = line;
    }
    ~LogLine() {
        if (lvl_ < level()) return;
        std::lock_guard<std::mutex> g(mutex());
        auto& os = ostream();
        os << "[" << level_name(lvl_) << "] " << src_ << ":" << line_no_ << " "
           << ss_.str() << std::endl;
    }
    template <typename T>
    LogLine& operator<<(const T& v) { ss_ << v; return *this; }

private:
    static std::mutex& mutex();
    static std::ostream& ostream();
    static const char* level_name(Level l);
    Level lvl_;
    std::string src_;
    int line_no_;
    std::ostringstream ss_;
};
}  // namespace detail

}  // namespace pi::core::log

#define PI_LOG_DEBUG ::pi::core::log::detail::LogLine(::pi::core::log::Level::Debug, __FILE__, __LINE__)
#define PI_LOG_INFO  ::pi::core::log::detail::LogLine(::pi::core::log::Level::Info,  __FILE__, __LINE__)
#define PI_LOG_WARN  ::pi::core::log::detail::LogLine(::pi::core::log::Level::Warn,  __FILE__, __LINE__)
#define PI_LOG_ERROR ::pi::core::log::detail::LogLine(::pi::core::log::Level::Error, __FILE__, __LINE__)
