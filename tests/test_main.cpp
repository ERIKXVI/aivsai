// Minimal self-contained test runner (no external framework needed).
// Usage: pm_tests [text]  runs only the tests whose name contains `text`.
#include "tests/TestFramework.hpp"

int main(int argc, char** argv) { return pm::test::runAll(argc > 1 ? argv[1] : nullptr); }
