configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/scribe/version.hpp
    @ONLY
)

add_library(scribe_core STATIC
    types.cpp
    log.cpp
    util.cpp
    proc.cpp
    config.cpp
    paths.cpp
    db.cpp
    media.cpp
    wavio.cpp
    models.cpp
    asr.cpp
    diarize.cpp
    embed.cpp
    separate.cpp
    speakers.cpp
    render.cpp
    pipeline.cpp
)

target_include_directories(scribe_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(scribe_core
    PUBLIC
        scribe_flags
        sqlite3
    PRIVATE
        whisper
        sherpa-onnx-core
        sherpa-onnx-c-api
        sherpa-onnx-cxx-api
        tomlplusplus::tomlplusplus
)

if(WIN32)
    target_link_libraries(scribe_core PRIVATE shlwapi shell32 ole32)
endif()

if(SCRIBE_CUDA)
    target_compile_definitions(scribe_core PUBLIC SCRIBE_HAVE_CUDA=1)
endif()

target_compile_definitions(scribe_core PUBLIC SCRIBE_VERSION="${PROJECT_VERSION}")
