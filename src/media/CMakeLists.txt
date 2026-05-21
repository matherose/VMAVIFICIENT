add_library(vmav_media STATIC
    media_probe.c)

add_library(vmav::media ALIAS vmav_media)

target_link_libraries(vmav_media
    PRIVATE
        vmav::compile_options
        vmav::tp::avformat
        vmav::tp::avcodec
        vmav::tp::avutil
    PUBLIC
        vmav::foundations)

target_include_directories(vmav_media
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include)
