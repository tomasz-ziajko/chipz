# Test configuration for chipz

# Include Google Test
include(GoogleTest)

# Test sources
set(TEST_SOURCES
    test_peripheral.cpp
    test_ds3231.cpp
    test_hd44780.cpp
    test_max6675.cpp
)

# Create test executable
add_executable(chipz_tests ${TEST_SOURCES})

# Link against chipz and Google Test
target_link_libraries(chipz_tests
    PRIVATE
        chipz::chipz
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
)

# Discover tests
gtest_discover_tests(chipz_tests)
