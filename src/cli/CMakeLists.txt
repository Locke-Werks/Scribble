add_executable(scribe_cli main.cpp)

set_target_properties(scribe_cli PROPERTIES OUTPUT_NAME scribe)

target_link_libraries(scribe_cli PRIVATE scribe_core scribe_flags)

if(WIN32)
    target_sources(scribe_cli PRIVATE ${CMAKE_SOURCE_DIR}/resources/scribe.rc)
    target_include_directories(scribe_cli PRIVATE ${CMAKE_SOURCE_DIR}/resources)
    target_compile_definitions(scribe_cli PRIVATE
        SCRIBE_RC_VERSION=${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0
        SCRIBE_RC_VERSION_STRING="${PROJECT_VERSION}"
        SCRIBE_RC_DESCRIPTION="ScribeEveryone command line"
        SCRIBE_RC_INTERNAL_NAME="scribe"
        SCRIBE_RC_ORIGINAL_NAME="scribe.exe"
    )
endif()

