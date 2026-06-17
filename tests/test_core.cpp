// tests/test_core.cpp
#define TEST_FRAMEWORK_IMPLEMENT
#include "test_framework.hpp"

#include "pi_core/ansi.hpp"
#include "pi_core/env.hpp"
#include "pi_core/error.hpp"
#include "pi_core/file_io.hpp"
#include "pi_core/json.hpp"
#include "pi_core/lockfile.hpp"
#include "pi_core/log.hpp"
#include "pi_core/path.hpp"
#include "pi_core/result.hpp"
#include "pi_core/strutil.hpp"
#include "pi_core/unicode_width.hpp"

#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

using namespace pi::core;

TEST_CASE("Result<int> ok/err") {
    auto r = Result<int>::ok(42);
    CHECK(r.is_ok());
    CHECK(r.value() == 42);
    auto e = Result<int>::err(ErrorKind::InvalidArgument, "bad");
    CHECK(e.is_err());
    CHECK(e.error().kind == ErrorKind::InvalidArgument);
}

TEST_CASE("Result<void> ok/err") {
    auto r = Result<void>::ok();
    CHECK(r.is_ok());
    auto e = Result<void>::err(ErrorKind::Io, "disk full");
    CHECK(e.is_err());
}

TEST_CASE("strutil split/trim/starts_with") {
    auto parts = str::split("a,b,c", ',');
    REQUIRE(parts.size() == 3);
    CHECK(parts[0] == "a");
    CHECK(parts[2] == "c");
    CHECK(str::trim("  hi  ") == "hi");
    CHECK(str::starts_with("hello world", "hello"));
    CHECK_FALSE(str::starts_with("hello", "world"));
    CHECK(str::iequal("Hello", "hElLo"));
}

TEST_CASE("JSON roundtrip") {
    Json j;
    j["x"] = 1;
    j["y"] = {1, 2, 3};
    auto s = j.dump();
    auto p = parse(s);
    CHECK(p["x"] == 1);
    CHECK(p["y"][2] == 3);
}

TEST_CASE("file_io atomic write + read") {
    auto tmp = (fs::temp_directory_path() / "pi_test_file_io.txt").string();
    auto w = file::write_atomic(tmp, "hello\nworld");
    CHECK(w.is_ok());
    auto r = file::read(tmp);
    CHECK(r.is_ok());
    CHECK(r.value() == "hello\nworld");
    fs::remove(tmp);
}

TEST_CASE("ANSI builders") {
    CHECK(std::string(ansi::RESET) == "\x1b[0m");
    CHECK(ansi::fg(1) == "\x1b[31m");
    CHECK(ansi::rgb_fg(255, 0, 0) == "\x1b[38;2;255;0;0m");
}

TEST_CASE("lockfile acquire/release") {
    auto tmp = (fs::temp_directory_path() / "pi_test_lock").string();
    lockfile::FileLock lk(tmp);
    auto guard1 = lk.try_acquire();
    CHECK(guard1.has_value());
    auto guard2 = lk.try_acquire();
    CHECK_FALSE(guard2.has_value());
    guard1.reset();
    auto guard3 = lk.try_acquire();
    CHECK(guard3.has_value());
    fs::remove(tmp + ".lock");
}

TEST_CASE("path expand_home") {
    setenv("HOME", "/home/test", 1);
    CHECK(path::expand_home("~/x") == "/home/test/x");
    CHECK(path::expand_home("/abs/x") == "/abs/x");
}

TEST_CASE("unicode width") {
    CHECK(unicode::display_width("hello") == 5);
    CHECK(unicode::display_width("中文") == 4);
    CHECK(unicode::display_width("a中b") == 4);
}

TEST_CASE("env get_or") {
    setenv("PI_TEST_VAR", "yes", 1);
    CHECK(env::get_or("PI_TEST_VAR", "no") == "yes");
    CHECK(env::get_or("PI_UNSET_VAR", "default") == "default");
}
