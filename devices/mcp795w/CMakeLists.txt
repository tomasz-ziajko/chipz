add_library(chipz_mcp795w INTERFACE)
target_link_libraries(chipz_mcp795w INTERFACE chipz::core)
target_sources(chipz_mcp795w INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/chipz/devices/mcp795w.hpp
)
add_library(chipz::mcp795w ALIAS chipz_mcp795w)
