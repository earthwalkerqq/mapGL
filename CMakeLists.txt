cmake_minimum_required(VERSION 3.16)

project(test_map VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS
    Quick
    Core
    Sql
    Concurrent
    Network
    Gui
)
find_package(ZLIB REQUIRED)

add_library(orm INTERFACE)
target_include_directories(orm INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/orm
    ${CMAKE_CURRENT_SOURCE_DIR}/orm/lib
)
target_link_libraries(orm INTERFACE Qt6::Core Qt6::Sql)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(apptest_map
    main.cpp
)

qt_add_qml_module(apptest_map
    URI test_map
    QML_FILES
        Main.qml
        SOURCES sgmapview.h sgmapview.cpp
        SOURCES cameraview.h cameraview.cpp
        SOURCES mapGL.h
        SOURCES tileprovider.h
        SOURCES networktileprovider.h networktileprovider.cpp
        SOURCES fallbacktileprovider.h fallbacktileprovider.cpp
        SOURCES tilestore.h
        SOURCES tileformat.h
        SOURCES tiledecoder.h tiledecoder.cpp
        SOURCES vectortile.h vectortile.cpp
        SOURCES tilescheduler.h tilescheduler.cpp
        SOURCES tilemanager.h tilemanager.cpp
        SOURCES tileloader.h tileloader.cpp
        SOURCES servermeta.h
        SOURCES standartsourcepath.h
        SOURCES networkservice.h
        SOURCES tilestorage.h tilestorage.cpp
        SOURCES aerodromtilerasterprovider.h aerodromtilerasterprovider.cpp
        SOURCES aerodromtilevectorprovider.h aerodromtilevectorprovider.cpp
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(apptest_map PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.apptest_map
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# локальная сборка
target_compile_definitions(apptest_map
    PRIVATE MAPGL_DATA_DIR="/Users/husen/Documents/test_map/materials"
    PRIVATE MAPGL_SOURCE_DIR="/Users/husen/Documents/test_map/materials"
)

# для сборки на деплой
# target_compile_definitions(apptest_map
#     PRIVATE MAPGL_AURORA
# )

target_link_libraries(apptest_map
    PRIVATE Qt6::Quick
    PRIVATE Qt6::Core
    PRIVATE Qt6::Concurrent
    PRIVATE Qt6::Network
    PRIVATE Qt6::Gui
    PRIVATE ZLIB::ZLIB
    PRIVATE orm
)

include(GNUInstallDirs)
install(TARGETS apptest_map
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
