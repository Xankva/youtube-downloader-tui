cmake_minimum_required(VERSION 3.16)
project(ytd VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Dependencies ----

# nlohmann/json (header-only)
set(NLOHMANN_JSON_DIR "${CMAKE_SOURCE_DIR}/deps")
if(NOT EXISTS "${NLOHMANN_JSON_DIR}/nlohmann/json.hpp")
    message(FATAL_ERROR "nlohmann/json.hpp not found in ${NLOHMANN_JSON_DIR}")
endif()

# FTXUI – compile vendored sources directly
set(FTXUI_SRC_DIR "${CMAKE_SOURCE_DIR}/deps/ftxui")
file(GLOB_RECURSE FTXUI_SOURCES
    "${FTXUI_SRC_DIR}/src/ftxui/component/*.cpp"
    "${FTXUI_SRC_DIR}/src/ftxui/dom/*.cpp"
    "${FTXUI_SRC_DIR}/src/ftxui/screen/*.cpp"
)
# Remove test and fuzzer files
list(FILTER FTXUI_SOURCES EXCLUDE REGEX "_test\\.cpp$|_fuzzer\\.cpp$")

# ---- Include directories ----

set(INCLUDES
    "${CMAKE_SOURCE_DIR}/include"
    "${FTXUI_SRC_DIR}/include"
    "${FTXUI_SRC_DIR}/src"
    "${NLOHMANN_JSON_DIR}"
)

# ---- Sources ----

set(APP_SOURCES
    "${CMAKE_SOURCE_DIR}/src/app.cpp"
    "${CMAKE_SOURCE_DIR}/src/config.cpp"
    "${CMAKE_SOURCE_DIR}/src/download.cpp"
    "${CMAKE_SOURCE_DIR}/src/history.cpp"
    "${CMAKE_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_SOURCE_DIR}/src/queue.cpp"
)

# ---- Library / Executable ----

# Build ftxui object library (compiled once, linked into the executable)
add_library(ftxui_objects OBJECT ${FTXUI_SOURCES})
target_include_directories(ftxui_objects SYSTEM PUBLIC ${INCLUDES})
set_target_properties(ftxui_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(ytd ${APP_SOURCES})
target_include_directories(ytd PRIVATE ${INCLUDES})
target_link_libraries(ytd PRIVATE ftxui_objects)

# Platform-specific settings
if(WIN32)
    # FTXUI expects these on Windows
    target_compile_definitions(ftxui_objects PRIVATE UNICODE _UNICODE)
    target_compile_definitions(ytd PRIVATE
        UNICODE _UNICODE
        _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX
    )
    target_link_libraries(ytd PRIVATE ws2_32)
else()
    target_compile_definitions(ftxui_objects PRIVATE FTXUI_MICRO_ATTRIBUTE=)
    target_compile_definitions(ytd PRIVATE FTXUI_MICRO_ATTRIBUTE=)
    target_link_libraries(ytd PRIVATE pthread)
    target_link_libraries(ytd PRIVATE rt)
endif()

# ---- Install ----

install(TARGETS ytd RUNTIME DESTINATION bin)
install(FILES "${CMAKE_SOURCE_DIR}/ytd.desktop" DESTINATION share/applications)

# ---- Packaging ----

set(CPACK_PACKAGE_NAME "ytd")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Terminal UI YouTube downloader")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
if(CPACK_GENERATOR STREQUAL "")
    set(CPACK_GENERATOR "TGZ" CACHE STRING "Package generator" FORCE)
endif()
include(CPack)
