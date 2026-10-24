#pragma once
// Tiny unit-test framework: TEST_CASE registers a function, CHECK records failures.

#include <cstdio>
#include <functional>
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

inline int runAll() {
    int failedTests = 0;
    for (const auto& t : registry()) {
        int before = failures();
        t.fn();
        bool ok = failures() == before;
        if (!ok) ++failedTests;
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", t.name);
    }
    std::printf("\n%zu tests, %d failed\n", registry().size(), failedTests);
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
