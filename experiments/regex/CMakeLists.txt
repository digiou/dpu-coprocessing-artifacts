cmake_minimum_required(VERSION 3.21)
project(regex-re2)

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
    message("-- Release mode - ON")
    set(CMAKE_CXX_FLAGS "-g -pthread -O3 -march=native")
ENDIF ()

# Define the project folder
add_definitions(-DPROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

# Error if lz4 is not found
find_package(re2 CONFIG REQUIRED)
message("-- re2: dependencies OK")

# Add source files to the library
add_executable(regex-re2
        regex_re2.cpp
)

# Link libraries
target_link_libraries(regex-re2 PUBLIC
    re2::re2
)

# ------------------------------------------------------------------------------
# Conditionally link vectorscan (ARM64) or hyperscan (x86_64) from system packages
# ------------------------------------------------------------------------------
# CMAKE_SYSTEM_PROCESSOR typically gives "x86_64" or "aarch64" on Linux.
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    message("-- Detected x86_64: looking for Hyperscan (libhs).")
    find_library(HS_LIB NAMES hs PATHS /usr/lib /usr/local/lib /lib)
    if(NOT HS_LIB)
        message(FATAL_ERROR "Could not find the 'hs' library (Hyperscan) for x86_64.")
    endif()

    set(HYPERSCAN_INCLUDE_DIRS "/usr/include" "/usr/local/include")
    # Some distros put Hyperscan headers in /usr/include/hs - adapt if needed.

elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    message("-- Detected aarch64: looking for vectorscan (libvectorscan).")
    find_library(HS_LIB NAMES hs PATHS /usr/lib /usr/local/lib /lib)
    if(NOT HS_LIB)
        message(FATAL_ERROR "Could not find the 'vectorscan' library for aarch64.")
    endif()

    set(HYPERSCAN_INCLUDE_DIRS "/usr/include" "/usr/local/include")
    # Adjust if your distribution puts vectorscan headers elsewhere.

else()
    # Fallback or handle other arches as needed
    message(WARNING 
        "Unknown architecture: ${CMAKE_SYSTEM_PROCESSOR}. "
        "Will attempt to use Hyperscan library named 'hs' by default.")
    find_library(HS_LIB NAMES hs PATHS /usr/lib /usr/local/lib /lib)
    if(NOT HS_LIB)
        message(FATAL_ERROR "Could not find 'hs' library for unknown architecture.")
    endif()
    set(HYPERSCAN_INCLUDE_DIRS "/usr/include" "/usr/local/include")
endif()

message("-- Found library at: ${HS_LIB}")

# ------------------------------------------------------------------------------
# Add your "regex-vectorscan" (or "regex-hyperscan") executable
# ------------------------------------------------------------------------------
add_executable(regex-vectorscan regex_vectorscan.cpp)

# If your code #include <hs/hs.h> with no special subdir, the standard /usr/include
# is likely enough. If not, you can point to the right location:
target_include_directories(regex-vectorscan PUBLIC ${HYPERSCAN_INCLUDE_DIRS})

# Finally, link the appropriate library we found
target_link_libraries(regex-vectorscan PUBLIC ${HS_LIB})

