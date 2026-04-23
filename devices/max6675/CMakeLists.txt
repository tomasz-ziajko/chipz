# chipz_add_max6675() is provided by cmake/ChipzISR.cmake (included from root).
# Use it in your project instead of target_link_libraries(... chipz::max6675):
#
#   chipz_add_max6675(TARGET my_app
#       SPI_INSTANCE SPI1  SPI_HANDLE hspi1  SPI_IFACE g_spi1)

add_library(chipz_max6675 INTERFACE)
target_link_libraries(chipz_max6675 INTERFACE chipz::core)
target_sources(chipz_max6675 INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/chipz/devices/max6675.hpp
)
add_library(chipz::max6675 ALIAS chipz_max6675)
