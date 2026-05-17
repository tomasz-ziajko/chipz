add_library(chipz_pcf8574 INTERFACE)
target_link_libraries(chipz_pcf8574 INTERFACE chipz::core)
target_sources(chipz_pcf8574 INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/chipz/devices/pcf8574.hpp
)
add_library(chipz::pcf8574 ALIAS chipz_pcf8574)
