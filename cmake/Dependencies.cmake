include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------- sqlite3 ---
# Vendored amalgamation. One .c file, no system dependency, no version drift.
set(SCRIBE_SQLITE_URL  "https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip"
    CACHE STRING "SQLite amalgamation archive")
set(SCRIBE_SQLITE_SHA256 "" CACHE STRING "SHA256 of the SQLite archive")

if(SCRIBE_SQLITE_SHA256)
    FetchContent_Declare(sqlite3_amalgamation
        URL      ${SCRIBE_SQLITE_URL}
        URL_HASH SHA256=${SCRIBE_SQLITE_SHA256}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
else()
    FetchContent_Declare(sqlite3_amalgamation
        URL      ${SCRIBE_SQLITE_URL}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
endif()
FetchContent_MakeAvailable(sqlite3_amalgamation)

add_library(sqlite3 STATIC ${sqlite3_amalgamation_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${sqlite3_amalgamation_SOURCE_DIR})
target_compile_definitions(sqlite3 PUBLIC
    SQLITE_THREADSAFE=1
    SQLITE_DQS=0
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_OMIT_DEPRECATED
    SQLITE_ENABLE_MATH_FUNCTIONS
)
if(MSVC)
    target_compile_options(sqlite3 PRIVATE /wd4996 /wd4267 /wd4244)
endif()

# ----------------------------------------------------------------- toml++ ---
FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(tomlplusplus)

# ------------------------------------------------------------- whisper.cpp ---
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS      OFF CACHE BOOL "" FORCE)
if(SCRIBE_CUDA)
    set(GGML_CUDA ON CACHE BOOL "" FORCE)
    # ggml defaults to -cudart static, and cudart_static.lib is built against
    # the static CRT, which is the remaining source of the LNK4098 LIBCMT
    # conflict once sherpa-onnx is on /MD. cuBLAS is already a dynamic runtime
    # dependency, so the shared CUDA runtime costs one more small DLL and
    # leaves a single CRT across the whole image.
    set(CMAKE_CUDA_RUNTIME_LIBRARY Shared CACHE STRING "" FORCE)
endif()

FetchContent_Declare(whisper_cpp
    GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
    GIT_TAG        v1.9.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(whisper_cpp)

# ------------------------------------------------------------- sherpa-onnx ---
# Diarization, speaker embeddings and source separation. Pulls its own
# onnxruntime binaries during configure.
#
# The CRT setting is not optional. sherpa-onnx defaults to the static runtime
# (/MT) while CMake, whisper.cpp and the prebuilt Qt binaries all use the
# dynamic one, and mixing them fails at link with a wall of LNK2005 duplicate
# symbols out of libcpmt.lib. Qt cannot be rebuilt against /MT, so everything
# else moves to /MD.
set(SHERPA_ONNX_USE_STATIC_CRT OFF CACHE BOOL "" FORCE)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    CACHE STRING "" FORCE)

set(SHERPA_ONNX_ENABLE_PYTHON      OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_TESTS       OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_CHECK       OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_PORTAUDIO   OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_JNI         OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_C_API       ON  CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_WEBSOCKET   OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_ENABLE_BINARY      OFF CACHE BOOL "" FORCE)
set(SHERPA_ONNX_BUILD_C_API_EXAMPLES OFF CACHE BOOL "" FORCE)
if(SCRIBE_CUDA)
    set(SHERPA_ONNX_ENABLE_GPU ON CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(sherpa_onnx
    GIT_REPOSITORY https://github.com/k2-fsa/sherpa-onnx.git
    GIT_TAG        v1.13.6
    GIT_SHALLOW    TRUE
    PATCH_COMMAND  ${CMAKE_COMMAND}
                   -DDIR=<SOURCE_DIR>
                   -P ${CMAKE_CURRENT_LIST_DIR}/patch_sherpa_ort.cmake
)

FetchContent_MakeAvailable(sherpa_onnx)

# onnxruntime's CUDA execution provider lives in side DLLs that must sit beside
# the executable. Without them the provider does not degrade to CPU, it fails
# the whole session with a missing-module error, so they are staged at build
# time rather than left to the installer.
set(SCRIBE_ORT_LIB_DIRS
    "${FETCHCONTENT_BASE_DIR}/onnxruntime-src/lib"
    "${CMAKE_BINARY_DIR}/_deps/onnxruntime-src/lib"
)
set(SCRIBE_ORT_DLLS "")
foreach(_dir IN LISTS SCRIBE_ORT_LIB_DIRS)
    if(EXISTS "${_dir}")
        file(GLOB _found "${_dir}/onnxruntime*.dll")
        list(APPEND SCRIBE_ORT_DLLS ${_found})
        break()
    endif()
endforeach()
list(REMOVE_DUPLICATES SCRIBE_ORT_DLLS)

if(SCRIBE_ORT_DLLS)
    add_custom_target(scribe_stage_onnxruntime ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SCRIBE_ORT_DLLS}
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>"
        COMMENT "Staging onnxruntime runtime libraries"
        VERBATIM
    )
else()
    message(WARNING "onnxruntime DLLs not located; GPU inference for diarization "
                    "and Parakeet will fall back to CPU")
endif()
