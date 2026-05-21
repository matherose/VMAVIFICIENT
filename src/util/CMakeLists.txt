add_library(vmav_util STATIC
    str_utils.c
    pathbuf.c
    json_io.c)

add_library(vmav::util ALIAS vmav_util)

target_link_libraries(vmav_util
    PRIVATE
        vmav::compile_options
        vmav::tp::cjson
    PUBLIC
        vmav::foundations)

target_include_directories(vmav_util
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include)
