#pragma once

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

// Simple test framework macros - reusable across all test files
#define TEST(name) void name()

// Assertions must not rely on assert(): CI builds with CMAKE_BUILD_TYPE=Release
// defines NDEBUG, which would turn every check below into a no-op.
#define ASSERT_FAIL(expr_text)                                                 \
    do {                                                                       \
        std::ostringstream oss;                                                \
        oss << "assertion failed: " << (expr_text) << " (" << __FILE__ << ":"  \
            << __LINE__ << ")";                                                \
        throw std::runtime_error(oss.str());                                   \
    } while (0)

#define ASSERT_TRUE(x)                                                         \
    do {                                                                       \
        if (!(x)) {                                                            \
            ASSERT_FAIL(#x);                                                   \
        }                                                                      \
    } while (0)

#define ASSERT_FALSE(x)                                                        \
    do {                                                                       \
        if (x) {                                                               \
            ASSERT_FAIL("!(" #x ")");                                          \
        }                                                                      \
    } while (0)

#define ASSERT_EQ(a, b)                                                        \
    do {                                                                       \
        if (!((a) == (b))) {                                                   \
            ASSERT_FAIL(#a " == " #b);                                         \
        }                                                                      \
    } while (0)

#define ASSERT_STREQ(a, b)                                                     \
    do {                                                                       \
        if (std::string(a) != std::string(b)) {                                \
            ASSERT_FAIL(#a " == " #b);                                         \
        }                                                                      \
    } while (0)

#define RUN_TEST(name)                                                         \
    do {                                                                       \
        std::cout << "Running " << #name << "... ";                            \
        name();                                                                \
        std::cout << "✓ PASSED" << std::endl;                                  \
    } while (0)
