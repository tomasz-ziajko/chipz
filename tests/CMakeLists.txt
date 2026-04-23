add_executable(chipz_tests
    core/test_communication_interface.cpp
    core/test_chip.cpp
    interfaces/test_i2c_interface.cpp
    interfaces/test_spi_interface.cpp
    interfaces/test_uart_interface.cpp
)

target_link_libraries(chipz_tests PRIVATE
    chipz_test_support
    GTest::gtest_main
    GTest::gmock
)

gtest_discover_tests(chipz_tests)
