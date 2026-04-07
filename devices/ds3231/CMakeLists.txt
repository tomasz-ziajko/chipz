add_library(chipz_ds3231 INTERFACE)
target_link_libraries(chipz_ds3231 INTERFACE chipz::core)
target_sources(chipz_ds3231 INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/chipz/devices/ds3231.hpp
)
add_library(chipz::ds3231 ALIAS chipz_ds3231)

if(CHIPZ_BUILD_TESTS)
    add_executable(chipz_test_ds3231 test_ds3231.cpp)
    target_link_libraries(chipz_test_ds3231 PRIVATE
        chipz::ds3231
        chipz_test_support
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
    )
    gtest_discover_tests(chipz_test_ds3231)
endif()
