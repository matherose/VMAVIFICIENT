add_library(vmav_integrations STATIC
    tmdb_client.c
    tmdb_match.c)

add_library(vmav::integrations ALIAS vmav_integrations)

target_link_libraries(vmav_integrations
    PRIVATE
        vmav::compile_options
        vmav::tp::curl
    PUBLIC
        vmav::foundations
        vmav::util
        vmav::tp::cjson)

target_include_directories(vmav_integrations
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include)
