add_subdirectory(foundations)
add_subdirectory(util)
add_subdirectory(encode)
add_subdirectory(integrations)
add_subdirectory(media)
add_subdirectory(commands)

add_executable(vmavificient main.c)

target_link_libraries(vmavificient
    PRIVATE
        vmav::compile_options
        vmav::commands
        vmav::encode
        vmav::integrations
        vmav::media
        vmav::util
        vmav::foundations)

target_include_directories(vmavificient
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR})
