cmake_minimum_required(VERSION 3.16)

project(fetchdeeznuts VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Concurrent)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Concurrent)

# Find libgit2
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBGIT2 REQUIRED libgit2)

set(PROJECT_SOURCES
        main.cpp
        fetchdeeznutzwindow.cpp
        fetchdeeznutzwindow.h
        resources.qrc
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(fetchdeeznuts
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET fetchdeeznuts APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(fetchdeeznuts SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(fetchdeeznuts
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(fetchdeeznuts PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Concurrent)
target_include_directories(fetchdeeznuts PRIVATE ${LIBGIT2_INCLUDE_DIRS})
target_link_libraries(fetchdeeznuts PRIVATE ${LIBGIT2_LIBRARIES})
target_compile_options(fetchdeeznuts PRIVATE ${LIBGIT2_CFLAGS_OTHER})

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.fetchdeeznuts)
endif()
set_target_properties(fetchdeeznuts PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)

# Find ImageMagick for icon generation
#find_program(IMAGEMAGICK_CONVERT convert)
#if(IMAGEMAGICK_CONVERT)
#    # Create PNG icons from SVG
#    add_custom_target(generate_icons
#        COMMAND ${IMAGEMAGICK_CONVERT} nuts_icon.svg -resize 16x16 fetchdeeznuts-16.png
#        COMMAND ${IMAGEMAGICK_CONVERT} nuts_icon.svg -resize 24x24 fetchdeeznuts-24.png
#        COMMAND ${IMAGEMAGICK_CONVERT} nuts_icon.svg -resize 32x32 fetchdeeznuts-32.png
#        COMMAND ${IMAGEMAGICK_CONVERT} nuts_icon.svg -resize 48x48 fetchdeeznuts-48.png
#        COMMAND ${IMAGEMAGICK_CONVERT} nuts_icon.svg -resize 64x64 fetchdeeznuts-64.png
#        COMMAND ${IMAGEMAGICK_CONVERT} nuts_icon.svg -resize 128x128 fetchdeeznuts-128.png
#        COMMAND ${IMAGEMAGICK_CONVERT} nuts_icon.svg -resize 256x256 fetchdeeznuts-256.png
#        DEPENDS nuts_icon.svg
#        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
#        COMMENT "Generating PNG icons from SVG"
#    )
#    add_dependencies(fetchdeeznuts generate_icons)
#else()
#    message(STATUS "ImageMagick not found - PNG icons will not be generated")
#endif()

# Install the executable
install(TARGETS fetchdeeznuts
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install desktop file
install(FILES fetchdeeznuts-install.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
    RENAME fetchdeeznuts.desktop
)

# Install icons
install(FILES nuts_icon.svg
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
    RENAME fetchdeeznuts.svg
)

# Install PNG icons if they exist
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/fetchdeeznuts-16.png)
    install(FILES fetchdeeznuts-16.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/16x16/apps
        RENAME fetchdeeznuts.png
    )
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/fetchdeeznuts-24.png)
    install(FILES fetchdeeznuts-24.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/24x24/apps
        RENAME fetchdeeznuts.png
    )
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/fetchdeeznuts-32.png)
    install(FILES fetchdeeznuts-32.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/32x32/apps
        RENAME fetchdeeznuts.png
    )
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/fetchdeeznuts-48.png)
    install(FILES fetchdeeznuts-48.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/48x48/apps
        RENAME fetchdeeznuts.png
    )
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/fetchdeeznuts-64.png)
    install(FILES fetchdeeznuts-64.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/64x64/apps
        RENAME fetchdeeznuts.png
    )
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/fetchdeeznuts-128.png)
    install(FILES fetchdeeznuts-128.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/128x128/apps
        RENAME fetchdeeznuts.png
    )
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/fetchdeeznuts-256.png)
    install(FILES fetchdeeznuts-256.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps
        RENAME fetchdeeznuts.png
    )
endif()

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(fetchdeeznuts)
endif()
