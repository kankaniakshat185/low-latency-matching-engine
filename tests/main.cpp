#include "test_framework.h"

// Forward declaration of the test runner
void runAllTests();

int main() {
    runAllTests();
    testing::PrintResults();
    return testing::passedTests == testing::totalTests ? 0 : 1;
}
