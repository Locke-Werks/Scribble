cmake_minimum_required(VERSION 3.24)

project(Scribble
    VERSION 0.2.0
    DESCRIPTION "Batch transcription with diarization and corpus-wide speaker identity"
    LANGUAGES C CXX
)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(SCRIBBLE_CUDA        "Build GPU inference backends"       ON)
option(SCRIBBLE_BUILD_GUI   "Build the Qt6 desktop application"  ON)
option(SCRIBBLE_BUILD_CLI   "Build the command line application" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Dependencies come first and are left entirely alone.
#
# Nothing above this line may set CMAKE_CXX_STANDARD, add_compile_options or
# add_compile_definitions. Those apply to every target in the tree, including
# the third-party ones: C++20 removes std::result_of, which simple-sentencepiece
# still uses, and a global /MP gets forwarded to nvcc and breaks its temp file
# handling. Project settings belong on the scribble_flags target below.
include(cmake/Dependencies.cmake)

add_library(scribble_flags INTERFACE)
target_compile_features(scribble_flags INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(scribble_flags INTERFACE /utf-8 /permissive- /W3)
    target_compile_definitions(scribble_flags INTERFACE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
    )
    # ggml enables OpenMP, which drags in MSVC's VCOMP.lib. That import library
    # carries its own /DEFAULTLIB:LIBCMT directive regardless of the runtime
    # everything else was built against, so linking /MD produces LNK4098 and a
    # mixed CRT. Excluding the static CRT explicitly is the documented remedy,
    # and it is safe here only because every other target is on /MD.
    target_link_options(scribble_flags INTERFACE /NODEFAULTLIB:LIBCMT)
endif()

add_subdirectory(src/core)

if(SCRIBBLE_BUILD_CLI)
    add_subdirectory(src/cli)
endif()

if(SCRIBBLE_BUILD_GUI)
    add_subdirectory(src/gui)
endif()

message(STATUS "")
message(STATUS "Scribble ${PROJECT_VERSION}")
message(STATUS "  CUDA backends : ${SCRIBBLE_CUDA}")
message(STATUS "  CLI           : ${SCRIBBLE_BUILD_CLI}")
message(STATUS "  GUI           : ${SCRIBBLE_BUILD_GUI}")
message(STATUS "")
