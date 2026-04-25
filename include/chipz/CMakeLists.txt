add_library(chipz_core INTERFACE)
target_sources(chipz_core INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${PROJECT_SOURCE_DIR}/include
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/chipz.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/chip.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/communication_interface.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/concepts.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/timer_interface.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/core.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/completion_sources/timer_completion_source.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/completion_sources/external_completion_source.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/interfaces/i2c_interface.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/interfaces/spi_interface.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/interfaces/uart_interface.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/interfaces/parallel_interface.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/network/can/can_interface.hpp
)
target_compile_features(chipz_core INTERFACE cxx_std_20)
add_library(chipz::core ALIAS chipz_core)
