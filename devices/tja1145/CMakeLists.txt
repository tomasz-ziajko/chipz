# chipz_add_tja1145() is provided by cmake/ChipzISR.cmake (included from root).
# Use it in your project instead of target_link_libraries(... chipz::tja1145):
#
#   chipz_add_tja1145(TARGET my_app
#       SPI_INSTANCE SPI2  SPI_HANDLE hspi2  SPI_IFACE g_spi2
#       [WAKE_EXTI <pin>]
#       [CAN_INSTANCE CAN1  CAN_HANDLE hcan1])

add_library(chipz_tja1145 INTERFACE)
target_link_libraries(chipz_tja1145 INTERFACE chipz::core)
target_sources(chipz_tja1145 INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/chipz/devices/tja1145.hpp
)
add_library(chipz::tja1145 ALIAS chipz_tja1145)
