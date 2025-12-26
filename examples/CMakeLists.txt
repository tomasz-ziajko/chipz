# Examples for chipz library

# Basic usage example
add_executable(basic_usage basic_usage.cpp)
target_link_libraries(basic_usage PRIVATE chipz::chipz)

# Wrapping C code example (demonstrates integration with C HAL)
add_executable(wrapping_c_code wrapping_c_code.cpp)
target_link_libraries(wrapping_c_code PRIVATE chipz::chipz)

# Buffer management example (demonstrates smart buffer growth)
add_executable(buffer_management buffer_management.cpp)
target_link_libraries(buffer_management PRIVATE chipz::chipz)

# DS3231 example
add_executable(ds3231_example ds3231_example.cpp)
target_link_libraries(ds3231_example PRIVATE chipz::chipz)

# HD44780 example
add_executable(hd44780_example hd44780_example.cpp)
target_link_libraries(hd44780_example PRIVATE chipz::chipz)

# MAX6675 example
add_executable(max6675_example max6675_example.cpp)
target_link_libraries(max6675_example PRIVATE chipz::chipz)
