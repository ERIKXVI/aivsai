#pragma once
// Tiny unit-test framework: TEST_CASE registers a function, CHECK records failures and REQUIRE
// also ends the test. An exception escaping a test fails that test and the run goes on.

#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <utility>
#include <vector>

namespace pm::test {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

/// Runs every test whose name contains `filter` (all of them when it is null or empty).
inline int runAll(const char* filter = nullptr) {
    int ran = 0, failedTests = 0;
    for (const auto& t : registry()) {
        if (filter && *filter && !std::strstr(t.name, filter)) continue;
        ++ran;
        int before = failures();
        try {
            t.fn();
        } catch (const std::exception& e) {
            std::printf("    uncaught exception: %s\n", e.what());
            ++failures();
        } catch (...) {
            std::printf("    uncaught non-standard exception\n");
            ++failures();
        }
        bool ok = failures() == before;
        if (!ok) ++failedTests;
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", t.name);
    }
    if (ran == 0 && filter && *filter) {
        std::printf("no test name contains \"%s\"\n", filter);
        return 1;
    }
    std::printf("\n%d tests, %d failed\n", ran, failedTests);
    return failedTests == 0 ? 0 : 1;
}

} // namespace pm::test

#define PM_CONCAT_INNER(a, b) a##b
#define PM_CONCAT(a, b) PM_CONCAT_INNER(a, b)
#define TEST_CASE(name)                                                                         \
    static void PM_CONCAT(pm_test_fn_, __LINE__)();                                             \
    static ::pm::test::Registrar PM_CONCAT(pm_test_reg_, __LINE__)(name, &PM_CONCAT(pm_test_fn_, __LINE__)); \
    static void PM_CONCAT(pm_test_fn_, __LINE__)()

#define CHECK(expr)                                                                             \
    do {                                                                                        \
        if (!(expr)) {                                                                          \
            std::printf("    %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #expr);           \
            ++::pm::test::failures();                                                           \
        }                                                                                       \
    } while (0)

// Like CHECK, but stops the current test (returns from the enclosing function) on failure.
// Use it when the following lines would dereference or index what was just checked.
#define REQUIRE(expr)                                                                           \
    do {                                                                                        \
        if (!(expr)) {                                                                          \
            std::printf("    %s:%d: REQUIRE(%s) failed\n", __FILE__, __LINE__, #expr);         \
            ++::pm::test::failures();                                                           \
            return;                                                                             \
        }                                                                                       \
    } while (0)
