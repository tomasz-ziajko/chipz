add_library(chipz_tja1145 INTERFACE)
target_link_libraries(chipz_tja1145 INTERFACE chipz::core)
target_sources(chipz_tja1145 INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/chipz/devices/tja1145.hpp
)
add_library(chipz::tja1145 ALIAS chipz_tja1145)
