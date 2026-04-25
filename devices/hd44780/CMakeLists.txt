add_library(chipz_hd44780 INTERFACE)
target_link_libraries(chipz_hd44780 INTERFACE chipz::core)
target_sources(chipz_hd44780 INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/chipz/devices/hd44780.hpp
)
add_library(chipz::hd44780 ALIAS chipz_hd44780)
