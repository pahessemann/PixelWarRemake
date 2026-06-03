#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace minicatch {

using TestFn = std::function<void()>;

struct TestCase {
    std::string name;
    TestFn fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(std::string name, TestFn fn) {
        registry().push_back(TestCase{std::move(name), std::move(fn)});
    }
};

inline int run() {
    int failures = 0;
    for (const auto& test : registry()) {
        try {
            test.fn();
            std::cout << "[pass] " << test.name << '\n';
        } catch (const std::exception& ex) {
            ++failures;
            std::cerr << "[fail] " << test.name << ": " << ex.what() << '\n';
        } catch (...) {
            ++failures;
            std::cerr << "[fail] " << test.name << ": unknown exception\n";
        }
    }
    return failures == 0 ? 0 : 1;
}

} // namespace minicatch

#define MINI_CATCH_CONCAT_INNER(a, b) a##b
#define MINI_CATCH_CONCAT(a, b) MINI_CATCH_CONCAT_INNER(a, b)

#define TEST_CASE(name) \
    static void MINI_CATCH_CONCAT(test_fn_, __LINE__)(); \
    static minicatch::Registrar MINI_CATCH_CONCAT(test_reg_, __LINE__)(name, MINI_CATCH_CONCAT(test_fn_, __LINE__)); \
    static void MINI_CATCH_CONCAT(test_fn_, __LINE__)()

#define REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            throw std::runtime_error(std::string("require failed: ") + #expr); \
        } \
    } while (false)

int main() {
    return minicatch::run();
}
