add_library(vmav_commands STATIC
    cmd_version.c
    cmd_help.c
    cmd_doctor.c
    cmd_encode.c
    cmd_analyze.c
    cmd_search.c)

add_library(vmav::commands ALIAS vmav_commands)

target_link_libraries(vmav_commands
    PRIVATE
        vmav::compile_options
    PUBLIC
        vmav::foundations)

target_include_directories(vmav_commands
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR})
