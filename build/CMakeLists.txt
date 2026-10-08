cmake_minimum_required(VERSION 3.31.6)
project(foo_openlyrics_cmake)

set(CMAKE_DEBUG_POSTFIX "")
set(BUILD_SHARED_LIBS off)
set(BUILD_STATIC_LIBS on)
set(BUILD_EXAMPLES off)

set(ENABLE_EXAMPLES off)
set(ENABLE_APP off)
set(ENABLE_DOC off)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../3rdparty/nghttp2-1.67.1 ${CMAKE_CURRENT_BINARY_DIR}/nghttp2)

set(BUILD_MISC_DOCS off)
set(BUILD_TESTING off)
set(BUILD_CURL_EXE off)
set(BUILD_LIBCURL_DOCS off)
set(BUILD_STATIC_CURL off)
set(CURL_USE_LIBPSL off)
set(CURL_USE_SCHANNEL on)
set(ENABLE_CURL_MANUAL off)
set(HTTP_ONLY on)
set(USE_NGHTTP2 on)
set(NGHTTP2_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../3rdparty/nghttp2-1.67.1/lib/includes")
set(NGHTTP2_LIBRARY "unused") # We're only building a static lib for curl so linking with nghttp2 isn't a concern here
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../3rdparty/curl-8.16.0 ${CMAKE_CURRENT_BINARY_DIR}/libcurl)
target_compile_definitions(libcurl_object PRIVATE NGHTTP2_STATICLIB)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../3rdparty/mecab-support ${CMAKE_CURRENT_BINARY_DIR}/mecab)

set(MINIZ_SOURCE ${CMAKE_CURRENT_LIST_DIR}/../3rdparty/miniz-3.0.2)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/miniz_export.h" "#pragma once\n#define MINIZ_EXPORT\n")
add_library(miniz STATIC ${MINIZ_SOURCE}/miniz.c ${MINIZ_SOURCE}/miniz_zip.c
    ${MINIZ_SOURCE}/miniz_tinfl.c ${MINIZ_SOURCE}/miniz_tdef.c)
target_include_directories(miniz PUBLIC ${MINIZ_SOURCE} ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(miniz PUBLIC MINIZ_NO_STDIO MINIZ_NO_ARCHIVE_WRITING_APIS)
target_compile_options(miniz PRIVATE /W0)
