add_library(chipz_hd44780 INTERFACE)
target_link_libraries(chipz_hd44780 INTERFACE chipz::core)
target_sources(chipz_hd44780 INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/chipz/devices/hd44780.hpp
)
add_library(chipz::hd44780 ALIAS chipz_hd44780)

if(CHIPZ_BUILD_TESTS)
    add_executable(chipz_test_hd44780 test_hd44780.cpp)
    target_link_libraries(chipz_test_hd44780 PRIVATE
        chipz::hd44780
        chipz_test_support
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
    )
    gtest_discover_tests(chipz_test_hd44780)
endif()
