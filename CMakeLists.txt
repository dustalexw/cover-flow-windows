cmake_minimum_required(VERSION 3.21)
project(CoverFlow VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Network Concurrent Multimedia Qml Quick QuickControls2 Test)
qt_standard_project_setup()

find_package(PkgConfig REQUIRED)
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswresample libavutil)

# Everything except main() lives in a static library so tests can link it.
add_library(coverflow_core STATIC
    src/core/Model.h
    src/core/LibraryScanner.h src/core/LibraryScanner.cpp
    src/core/LibraryStore.h src/core/LibraryStore.cpp
    src/core/Artwork.h src/core/Artwork.cpp
    src/core/Lyrics.h src/core/Lyrics.cpp
    src/core/MusicLookupClient.h src/core/MusicLookupClient.cpp
    src/core/Paths.h src/core/Paths.cpp
    src/core/Palette.h src/core/Palette.cpp
    src/audio/Decoder.h src/audio/Decoder.cpp
    src/audio/PlaybackEngine.h src/audio/PlaybackEngine.cpp
    src/audio/Player.h src/audio/Player.cpp
)
target_include_directories(coverflow_core PUBLIC src)
target_link_libraries(coverflow_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Concurrent Qt6::Multimedia
    PkgConfig::TAGLIB PkgConfig::FFMPEG)
target_compile_definitions(coverflow_core PUBLIC COVERFLOW_VERSION="${PROJECT_VERSION}")

qt_add_executable(CoverFlow WIN32
    src/main.cpp
    src/ui/AppController.h src/ui/AppController.cpp
    src/ui/AlbumModel.h src/ui/AlbumModel.cpp
    src/ui/ArtworkImageProvider.h src/ui/ArtworkImageProvider.cpp
)
if(WIN32)
    target_sources(CoverFlow PRIVATE packaging/coverflow.rc)
endif()

set_source_files_properties(qml/Theme.qml qml/Glyphs.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
# Serve QML files from the module root so they see the module's singletons.
file(GLOB CF_QML_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml)
foreach(f ${CF_QML_FILES})
    get_filename_component(n ${f} NAME)
    set_source_files_properties(${f} PROPERTIES QT_RESOURCE_ALIAS ${n})
endforeach()

# Qt 6.4's qmlcachegen miscompiles some inline-component lookups (the app hangs at load).
set(CF_QML_EXTRA)
if(Qt6_VERSION VERSION_LESS 6.5)
    set(CF_QML_EXTRA NO_CACHEGEN)
endif()
qt_add_qml_module(CoverFlow
    ${CF_QML_EXTRA}
    URI CoverFlow
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        qml/Main.qml
        qml/CoverFlowView.qml
        qml/AlbumGridView.qml
        qml/AlbumDetail.qml
        qml/PlaybackBar.qml
        qml/AlbumViewWindow.qml
        qml/LyricsPanel.qml
        qml/FindLyricsDialog.qml
        qml/FindArtworkDialog.qml
        qml/QueuePanel.qml
        qml/EditAlbumDialog.qml
        qml/SeekBar.qml
        qml/TransportControls.qml
        qml/NowPlayingBars.qml
        qml/MarqueeText.qml
        qml/Theme.qml
        qml/Glyphs.qml
    RESOURCES
        resources/icon-256.png
)

set_target_properties(CoverFlow PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_link_libraries(CoverFlow PRIVATE coverflow_core Qt6::Quick Qt6::QuickControls2 Qt6::Qml)

include(GNUInstallDirs)
install(TARGETS CoverFlow BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

enable_testing()
add_subdirectory(tests)
