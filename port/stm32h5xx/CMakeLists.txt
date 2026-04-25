add_library(chipz_port_stm32h5xx STATIC
    chipz_isrs.cpp
    config.cpp
    fault_handlers.cpp
)
target_include_directories(chipz_port_stm32h5xx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chipz_port_stm32h5xx
    PUBLIC  chipz::core
    PRIVATE stm32cubemx
)
add_library(chipz::port_stm32h5xx ALIAS chipz_port_stm32h5xx)
