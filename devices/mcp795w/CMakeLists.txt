# chipz_add_mcp795w() is provided by cmake/ChipzISR.cmake (included from root).
# Use it in your project instead of target_link_libraries(... chipz::mcp795w):
#
#   chipz_add_mcp795w(TARGET my_app
#       SPI_INSTANCE SPI1  SPI_HANDLE hspi1  SPI_IFACE g_spi1
#       [ALARM_EXTI <pin>])

add_library(chipz_mcp795w INTERFACE)
target_link_libraries(chipz_mcp795w INTERFACE chipz::core)
target_sources(chipz_mcp795w INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/chipz/devices/mcp795w.hpp
)
add_library(chipz::mcp795w ALIAS chipz_mcp795w)
