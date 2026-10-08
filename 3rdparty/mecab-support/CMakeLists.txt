# Static Windows adapter for the pinned upstream source; no engine symbols are exported.
set(MECAB_REVISION 61b90ba6e669dc2d7d533d4a80d206f3b31d52b1)
set(MECAB_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../mecab-${MECAB_REVISION}/mecab")
set(MECAB_BUILD_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/source")
file(GLOB MECAB_FILES "${MECAB_SOURCE}/src/*.cpp" "${MECAB_SOURCE}/src/*.h")
foreach(source IN LISTS MECAB_FILES)
    get_filename_component(name "${source}" NAME)
    configure_file("${source}" "${MECAB_BUILD_SOURCE}/${name}" COPYONLY)
endforeach()
file(READ "${MECAB_BUILD_SOURCE}/mecab.h" header)
string(REPLACE "#ifdef _WIN32\n#include <windows.h>" "#if defined(_WIN32) && !defined(MECAB_STATIC)\n#include <windows.h>" header "${header}")
file(WRITE "${MECAB_BUILD_SOURCE}/mecab.h" "${header}")
file(READ "${MECAB_BUILD_SOURCE}/common.h" common)
string(REPLACE "#ifdef _WIN32\n#ifdef __GNUC__\n#define WPATH_FORCE(path) (MeCab::Utf8ToWide(path).c_str())"
    "#ifdef _WIN32\n#define WPATH_FORCE(path) (MeCab::Utf8ToWide(path).c_str())\n#ifdef __GNUC__" common "${common}")
file(WRITE "${MECAB_BUILD_SOURCE}/common.h" "${common}")
set(MECAB_RUNTIME_SOURCES char_property.cpp connector.cpp context_id.cpp dictionary.cpp
    dictionary_compiler.cpp dictionary_generator.cpp dictionary_rewriter.cpp eval.cpp feature_index.cpp
    iconv_utils.cpp lbfgs.cpp learner.cpp learner_tagger.cpp libmecab.cpp nbest_generator.cpp
    param.cpp string_buffer.cpp tagger.cpp tokenizer.cpp utils.cpp viterbi.cpp writer.cpp)
list(TRANSFORM MECAB_RUNTIME_SOURCES PREPEND "${MECAB_BUILD_SOURCE}/")
add_library(mecab-static STATIC ${MECAB_RUNTIME_SOURCES})
target_include_directories(mecab-static PUBLIC "${MECAB_BUILD_SOURCE}")
target_compile_definitions(mecab-static PUBLIC MECAB_STATIC PRIVATE
    DIC_VERSION=102 VERSION="0.996" PACKAGE="mecab" MECAB_DEFAULT_RC=""
    HAVE_WINDOWS_H HAVE_UNSIGNED_LONG_LONG_INT MECAB_USE_THREAD MECAB_USE_UTF8_ONLY
    UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
target_compile_options(mecab-static PRIVATE /W0 /utf-8 /FIfunctional)
set_target_properties(mecab-static PROPERTIES CXX_STANDARD 14)
add_executable(mecab-dict-index "${MECAB_BUILD_SOURCE}/mecab-dict-index.cpp")
target_link_libraries(mecab-dict-index PRIVATE mecab-static)
add_executable(mecab-probe "${MECAB_BUILD_SOURCE}/mecab.cpp")
target_link_libraries(mecab-probe PRIVATE mecab-static)
target_compile_definitions(mecab-dict-index PRIVATE UNICODE _UNICODE)
target_compile_definitions(mecab-probe PRIVATE UNICODE _UNICODE)
