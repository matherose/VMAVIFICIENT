add_library(vmav_media STATIC
    media_probe.c
    media_hdr.c
    media_tracks.c
    media_crop.c
    media_analysis.c
    media_naming.c
    media_rpu.c)

add_library(vmav::media ALIAS vmav_media)

target_link_libraries(vmav_media
    PRIVATE
        vmav::compile_options
        vmav::tp::avformat
        vmav::tp::avfilter
        vmav::tp::avcodec
        vmav::tp::avutil
        vmav::tp::dovi
    PUBLIC
        vmav::foundations
        vmav::util)

target_include_directories(vmav_media
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include)
