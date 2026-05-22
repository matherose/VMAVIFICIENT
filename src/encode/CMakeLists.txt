add_library(vmav_encode STATIC
    encoder_preset.c
    audio_encode.c)

add_library(vmav::encode ALIAS vmav_encode)

target_link_libraries(vmav_encode
    PRIVATE
        vmav::compile_options
        vmav::tp::avformat
        vmav::tp::avcodec
        vmav::tp::swresample
        vmav::tp::avutil
    PUBLIC
        vmav::foundations
        vmav::util
        vmav::media)

target_include_directories(vmav_encode
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include)
