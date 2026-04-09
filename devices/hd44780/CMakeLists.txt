# chipz_add_hd44780() is provided by cmake/ChipzISR.cmake (included from root).
# Use it in your project instead of target_link_libraries(... chipz::hd44780):
#
#   chipz_add_hd44780(TARGET my_app
#       [SPI_INSTANCE SPI1  SPI_HANDLE hspi1  SPI_IFACE g_spi1])
#
# SPI params are optional — HD44780 is output-only; omit them if the
# driver is connected via GPIO bit-banging without async transfers.

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
