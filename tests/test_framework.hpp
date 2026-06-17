// tests/test_framework.hpp
// Minimal single-header test framework (doctest-style API surface).
// Provides: TEST_CASE, SUBCASE, CHECK, REQUIRE, INFO, CAPTURE, etc.
//
// Each .cpp file that wants to be an executable should include this header
// with TEST_FRAMEWORK_IMPLEMENT defined in exactly ONE of the .cpp files.

#pragma once

#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace tf {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

struct Reg {
    Reg(const char* n, std::function<void()> f) {
        registry().push_back({n, std::move(f)});
    }
};

struct Context {
    int passed = 0;
    int failed = 0;
    std::string current_case;
    std::string current_subcase;
};

inline Context& ctx() { static Context c; return c; }

inline int run_all() {
    for (auto& c : registry()) {
        ctx().current_case = c.name;
        ctx().current_subcase.clear();
        try {
            c.fn();
        } catch (const std::exception& e) {
            ctx().failed++;
            std::cerr << "[FAIL] " << c.name << " threw: " << e.what() << "\n";
        } catch (...) {
            ctx().failed++;
            std::cerr << "[FAIL] " << c.name << " threw unknown exception\n";
        }
    }
    std::cerr << "\n--- Summary ---\n";
    std::cerr << "passed: " << ctx().passed << "\n";
    std::cerr << "failed: " << ctx().failed << "\n";
    return ctx().failed == 0 ? 0 : 1;
}

inline void report_pass(const std::string& where) {
    (void)where;
    ctx().passed++;
}
inline void report_fail(const std::string& where, const std::string& expr,
                        const std::string& msg) {
    ctx().failed++;
    std::cerr << "[FAIL] " << ctx().current_case;
    if (!ctx().current_subcase.empty()) std::cerr << " > " << ctx().current_subcase;
    std::cerr << "\n  at " << where << "\n  CHECK: " << expr;
    if (!msg.empty()) std::cerr << "\n  info: " << msg;
    std::cerr << "\n";
}

}  // namespace tf

#define TEST_FRAMEWORK_CONCAT2(a, b) a##b
#define TEST_FRAMEWORK_CONCAT(a, b) TEST_FRAMEWORK_CONCAT2(a, b)

#define TEST_CASE(name)                                                      \
    static void TEST_FRAMEWORK_CONCAT(_tf_fn_, __LINE__)();                  \
    static ::tf::Reg TEST_FRAMEWORK_CONCAT(_tf_reg_, __LINE__)(              \
        name, TEST_FRAMEWORK_CONCAT(_tf_fn_, __LINE__));                     \
    static void TEST_FRAMEWORK_CONCAT(_tf_fn_, __LINE__)()

#define SUBCASE(name)                                                        \
    ctx().current_subcase = name;                                            \
    if (true)

#define CHECK(expr)                                                          \
    do {                                                                     \
        if (!(expr)) {                                                       \
            std::ostringstream _oss;                                         \
            _oss << #expr;                                                   \
            ::tf::report_fail(__FILE__ ":" + std::to_string(__LINE__),       \
                              _oss.str(), "");                               \
        } else {                                                             \
            ::tf::report_pass("");                                           \
        }                                                                    \
    } while (0)

#define CHECK_FALSE(expr)                                                    \
    do {                                                                     \
        if (expr) {                                                          \
            std::ostringstream _oss;                                         \
            _oss << "!(" #expr ")";                                          \
            ::tf::report_fail(__FILE__ ":" + std::to_string(__LINE__),       \
                              _oss.str(), "");                               \
        } else {                                                             \
            ::tf::report_pass("");                                           \
        }                                                                    \
    } while (0)

#define REQUIRE(expr)                                                        \
    do {                                                                     \
        if (!(expr)) {                                                       \
            std::ostringstream _oss;                                         \
            _oss << #expr;                                                   \
            ::tf::report_fail(__FILE__ ":" + std::to_string(__LINE__),       \
                              _oss.str(), "REQUIRE failed; aborting");       \
            return;                                                          \
        } else {                                                             \
            ::tf::report_pass("");                                           \
        }                                                                    \
    } while (0)

#define INFO(msg) ctx_info << msg
#define CAPTURE(x) ctx_info << #x << "=" << (x)

namespace tf {
inline std::ostringstream& ctx_info() {
    static std::ostringstream s;
    s.str("");
    s.clear();
    return s;
}
}

#ifdef TEST_FRAMEWORK_IMPLEMENT
int main() { return ::tf::run_all(); }
#endif
