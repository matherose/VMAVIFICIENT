add_subdirectory(foundations)

add_executable(vmavificient main.c)

target_link_libraries(vmavificient
    PRIVATE
        vmav::compile_options
        vmav::foundations)

target_include_directories(vmavificient
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include)
