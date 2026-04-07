# Combined test binary — all device tests in one executable alongside core tests
add_executable(chipz_tests
    test_peripheral.cpp
    ${CMAKE_SOURCE_DIR}/devices/ds3231/test_ds3231.cpp
    ${CMAKE_SOURCE_DIR}/devices/max6675/test_max6675.cpp
    ${CMAKE_SOURCE_DIR}/devices/hd44780/test_hd44780.cpp
)
target_link_libraries(chipz_tests PRIVATE
    chipz::chipz
    chipz_test_support
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)
gtest_discover_tests(chipz_tests)
