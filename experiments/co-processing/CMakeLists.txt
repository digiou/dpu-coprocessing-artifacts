cmake_minimum_required(VERSION 3.21)
project(co-processing)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Suppress formatting watning
add_compile_options(-Wno-format)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

set(LOG_LEVEL 2 CACHE STRING "Set the log level for the project")
add_compile_definitions(LOG_LEVEL=${LOG_LEVEL})

IF(CMAKE_BUILD_TYPE MATCHES Debug)
    message("-- Debug mode - ON")
    set(CMAKE_CXX_FLAGS "-g -pthread -O0 -march=native")
ELSE()
    set(CMAKE_CXX_FLAGS "-g -pthread -O3 -march=native")
ENDIF ()

# Define the project folder
add_definitions(-DPROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

# Add compile definitions for experimental DOCA API
add_compile_definitions(DOCA_ALLOW_EXPERIMENTAL_API=1)

# Detect system architecture
if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "amd64")
    set(DOCA_LIB_PATH "/opt/mellanox/doca/lib/x86_64-linux-gnu")
elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    set(DOCA_LIB_PATH "/opt/mellanox/doca/lib/aarch64-linux-gnu")
else()
    message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

# Set the DOCA include path
set(DOCA_INCLUDE_PATH "/opt/mellanox/doca/include")

# Add DOCA_LIB_PATH as an additional search path for libraries
find_library(DOCA_ARGP_LIB NAMES doca-argp libdoca_argp.so PATHS ${DOCA_LIB_PATH})
find_library(DOCA_COMMON_LIB NAMES doca-common libdoca_common.so PATHS ${DOCA_LIB_PATH})
find_library(DOCA_COMPRESS_LIB NAMES doca-compress libdoca_compress.so PATHS ${DOCA_LIB_PATH})

# Error if any library is not found
if(NOT DOCA_COMMON_LIB OR NOT DOCA_COMPRESS_LIB)
    message(FATAL_ERROR "One or more DOCA libraries were not found. Check library names and paths in ${DOCA_LIB_PATH}")
endif()
message("-- DOCA: dependencies OK")

# Error if zlib is not found
find_package(ZLIB REQUIRED)
message("-- ZLIB: dependencies OK")

# Error if lz4 is not found
find_package(lz4 CONFIG REQUIRED)
message("-- LZ4: dependencies OK")

# Error if re2 is not found
find_package(re2 CONFIG REQUIRED)
message("-- RE2: dependencies OK")

# Define the compression binary
add_executable(co-processing-compress
    co_processor_compress.cpp
    src/zpipe.cpp
    src/doca_compress.cpp
)

target_link_libraries(co-processing-compress PUBLIC
    ZLIB::ZLIB
    ${DOCA_COMMON_LIB}
    ${DOCA_COMPRESS_LIB}
)

# Define the decompression binary for Deflate
add_executable(co-processing-decompress-deflate
    co_processor_decompress_deflate.cpp
    src/zpipe.cpp
    src/doca_decompress_deflate.cpp
)

target_link_libraries(co-processing-decompress-deflate PUBLIC
    ZLIB::ZLIB
    ${DOCA_COMMON_LIB}
    ${DOCA_COMPRESS_LIB}
)

# Define the decompression binary for LZ4
add_executable(co-processing-decompress-lz4
    co_processor_decompress_lz4.cpp
    src/lz4_pipe.cpp
    src/doca_decompress_lz4.cpp
)

target_link_libraries(co-processing-decompress-lz4 PUBLIC
    lz4::lz4
    ${DOCA_COMMON_LIB}
    ${DOCA_COMPRESS_LIB}
)

# Define the decompression binary for regex
add_executable(co-processing-regex
    co_processor_regex.cpp
    src/re2_pipe.cpp
    # src/doca_regex.cpp
)

target_link_libraries(co-processing-regex PUBLIC
    re2::re2
    ${DOCA_COMMON_LIB}
    ${DOCA_COMPRESS_LIB}
)

# target_include_directories(co-processing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
# target_include_directories(co-processing PUBLIC ${DOCA_INCLUDE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_include_directories(co-processing-compress PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_include_directories(co-processing-compress PUBLIC ${DOCA_INCLUDE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_include_directories(co-processing-decompress-deflate PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_include_directories(co-processing-decompress-deflate PUBLIC ${DOCA_INCLUDE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_include_directories(co-processing-decompress-lz4 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_include_directories(co-processing-decompress-lz4 PUBLIC ${DOCA_INCLUDE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_include_directories(co-processing-regex PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_include_directories(co-processing-regex PUBLIC ${DOCA_INCLUDE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/inc)