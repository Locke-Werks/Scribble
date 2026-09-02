find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Svg)

qt_standard_project_setup()

set(GUI_SOURCES
    main.cpp
    ThemeQt.cpp
    ThemeWidgets.cpp
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
    ThemeQt.hpp
    ThemeWidgets.hpp
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

# The bundled design-language faces are compiled in rather than shipped beside
# the binary: a missing font file next to an installed exe would silently drop
# the application back to Segoe UI.
qt_add_executable(ScribbleGui WIN32 ${GUI_SOURCES} ${GUI_HEADERS} resources.qrc)

set_target_properties(ScribbleGui PROPERTIES
    # Not "Scribble", which would collide with the CLI. Windows filenames are
    # case-insensitive, so scribble.exe and Scribble.exe are one file, and
    # whichever target linked last silently won.
    OUTPUT_NAME ScribbleDesktop
    WIN32_EXECUTABLE ON
)

target_include_directories(ScribbleGui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(ScribbleGui PRIVATE
    scribble_core
    scribble_flags
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Svg
)

if(WIN32)
    target_sources(ScribbleGui PRIVATE ${CMAKE_SOURCE_DIR}/resources/scribble.rc)
    target_include_directories(ScribbleGui PRIVATE ${CMAKE_SOURCE_DIR}/resources)
    target_compile_definitions(ScribbleGui PRIVATE
        SCRIBBLE_RC_VERSION=${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0
        SCRIBBLE_RC_VERSION_STRING="${PROJECT_VERSION}"
        SCRIBBLE_RC_DESCRIPTION="Scribble"
        SCRIBBLE_RC_INTERNAL_NAME="Scribble"
        SCRIBBLE_RC_ORIGINAL_NAME="ScribbleDesktop.exe"
    )
endif()

# Stage the Qt runtime next to the binary so it launches from the build tree and
# can be zipped as-is. windeployqt ships beside qmake in the Qt bin directory.
if(WIN32)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ScribbleGui POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-compiler-runtime
                    "$<TARGET_FILE:ScribbleGui>"
            COMMENT "Deploying Qt runtime with windeployqt"
            VERBATIM
        )
    endif()
endif()
