#pragma once

#include <iostream>
#include <string>
#include <vector>

namespace testing {

inline int totalTests = 0;
inline int passedTests = 0;

#define ASSERT_TRUE(condition) \
    do { \
        ++testing::totalTests; \
        if (condition) { \
            ++testing::passedTests; \
        } else { \
            std::cerr << "Assertion failed: (" << #condition << "), function " << __FUNCTION__ \
                      << ", file " << __FILE__ << ", line " << __LINE__ << "." << std::endl; \
        } \
    } while (0)

#define ASSERT_EQ(val1, val2) ASSERT_TRUE((val1) == (val2))
#define ASSERT_NE(val1, val2) ASSERT_TRUE((val1) != (val2))

inline void PrintResults() {
    std::cout << "==========================\n";
    std::cout << "Tests passed: " << passedTests << " / " << totalTests << "\n";
    if (passedTests == totalTests) {
        std::cout << "SUCCESS\n";
    } else {
        std::cout << "FAILURE\n";
    }
    std::cout << "==========================\n";
}

} // namespace testing
