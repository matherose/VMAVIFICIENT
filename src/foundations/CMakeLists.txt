add_library(vmav_foundations STATIC
    vmav_result.c
    vmav_os_posix.c
    vmav_os_win.c
    vmav_log.c
    vmav_subproc_posix.c
    vmav_subproc_win.c
    vmav_ui.c
    vmav_cli.c)

add_library(vmav::foundations ALIAS vmav_foundations)

target_link_libraries(vmav_foundations
    PRIVATE
        vmav::compile_options)

target_include_directories(vmav_foundations
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include)
