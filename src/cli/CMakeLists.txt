add_executable(scribble_cli main.cpp)

set_target_properties(scribble_cli PROPERTIES OUTPUT_NAME scribble)

target_link_libraries(scribble_cli PRIVATE scribble_core scribble_flags)

if(WIN32)
    target_sources(scribble_cli PRIVATE ${CMAKE_SOURCE_DIR}/resources/scribble.rc)
    target_include_directories(scribble_cli PRIVATE ${CMAKE_SOURCE_DIR}/resources)
    target_compile_definitions(scribble_cli PRIVATE
        SCRIBBLE_RC_VERSION=${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0
        SCRIBBLE_RC_VERSION_STRING="${PROJECT_VERSION}"
        SCRIBBLE_RC_DESCRIPTION="Scribble command line"
        SCRIBBLE_RC_INTERNAL_NAME="scribble"
        SCRIBBLE_RC_ORIGINAL_NAME="scribble.exe"
    )
endif()

