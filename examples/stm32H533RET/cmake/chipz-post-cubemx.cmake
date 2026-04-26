# Applied after add_subdirectory(cmake/stm32cubemx) to undo things that conflict
# with the chipz port layer. Edit this file instead of the generated CMakeLists.txt.

# stm32h5xx_it.c defines weak IRQ stubs that conflict with the strong symbols in
# port/stm32h5xx/chipz_isrs.cpp. Mark it as header-only so it is listed as a
# source (CubeMX expects it) but never compiled.
get_target_property(_cubemx_srcs ${CMAKE_PROJECT_NAME} SOURCES)
foreach(_src IN LISTS _cubemx_srcs)
    if(_src MATCHES "stm32h5xx_it\\.c$")
        set_source_files_properties("${_src}" PROPERTIES HEADER_FILE_ONLY TRUE)
        break()
    endif()
endforeach()
