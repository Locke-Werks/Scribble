find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Svg)

qt_standard_project_setup()

set(GUI_SOURCES
    main.cpp
    MainWindow.cpp
    PipelineController.cpp
    QueueModel.cpp
    ProgressDelegate.cpp
    TranscriptModel.cpp
    SegmentDelegate.cpp
    TranscriptView.cpp
    SpeakerModel.cpp
    SpeakerPanel.cpp
    DuplicatesDialog.cpp
    SettingsDialog.cpp
    GpuRuntimeDialog.cpp
    LogDock.cpp
    SpeakerPalette.cpp
)

# Headers are listed so AUTOMOC processes the Q_OBJECT types, including the
# header-only ones (GuiEventSink, ScribeEvent) that have no matching .cpp.
set(GUI_HEADERS
    MainWindow.hpp
    PipelineController.hpp
    QueueModel.hpp
    ProgressDelegate.hpp
    TranscriptModel.hpp
    SegmentDelegate.hpp
    TranscriptView.hpp
    SpeakerModel.hpp
    SpeakerPanel.hpp
    DuplicatesDialog.hpp
    SettingsDialog.hpp
    GpuRuntimeDialog.hpp
    LogDock.hpp
    SpeakerPalette.hpp
    GuiEventSink.hpp
    ScribeEvent.hpp
    FormatUtil.hpp
)

qt_add_executable(ScribeEveryoneGui WIN32 ${GUI_SOURCES} ${GUI_HEADERS})

set_target_properties(ScribeEveryoneGui PROPERTIES
    OUTPUT_NAME ScribeEveryone
    WIN32_EXECUTABLE ON
)

target_include_directories(ScribeEveryoneGui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(ScribeEveryoneGui PRIVATE
    scribe_core
    scribe_flags
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Svg
)

if(WIN32)
    target_sources(ScribeEveryoneGui PRIVATE ${CMAKE_SOURCE_DIR}/resources/scribe.rc)
    target_include_directories(ScribeEveryoneGui PRIVATE ${CMAKE_SOURCE_DIR}/resources)
    target_compile_definitions(ScribeEveryoneGui PRIVATE
        SCRIBE_RC_VERSION=${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0
        SCRIBE_RC_VERSION_STRING="${PROJECT_VERSION}"
        SCRIBE_RC_DESCRIPTION="ScribeEveryone"
        SCRIBE_RC_INTERNAL_NAME="ScribeEveryone"
        SCRIBE_RC_ORIGINAL_NAME="ScribeEveryone.exe"
    )
endif()

# Stage the Qt runtime next to the binary so it launches from the build tree and
# can be zipped as-is. windeployqt ships beside qmake in the Qt bin directory.
if(WIN32)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ScribeEveryoneGui POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-compiler-runtime
                    "$<TARGET_FILE:ScribeEveryoneGui>"
            COMMENT "Deploying Qt runtime with windeployqt"
            VERBATIM
        )
    endif()
endif()
