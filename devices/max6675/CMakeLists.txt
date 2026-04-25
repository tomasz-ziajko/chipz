add_library(chipz_max6675 INTERFACE)
target_link_libraries(chipz_max6675 INTERFACE chipz::core)
target_sources(chipz_max6675 INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/chipz/devices/max6675.hpp
)
add_library(chipz::max6675 ALIAS chipz_max6675)
