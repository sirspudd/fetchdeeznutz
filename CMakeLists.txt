cmake_minimum_required(VERSION 3.16)

project(fetchdeeznutz VERSION 0.1 LANGUAGES CXX)

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Concurrent)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Concurrent)

set(PROJECT_SOURCES
        src/main.cpp
        src/gitmodels.cpp
        src/gitmodels.h
        src/gitutils.cpp
        src/gitutils.h
        src/gitfetchworker.cpp
        src/gitfetchworker.h
        src/repositorystore.cpp
        src/repositorystore.h
        src/repositorytreemodel.cpp
        src/repositorytreemodel.h
        src/repowatcher.cpp
        src/repowatcher.h
        src/remoteselectiondialog.cpp
        src/remoteselectiondialog.h
        src/repositorydialog.cpp
        src/repositorydialog.h
        src/fetchdeeznutzwindow.cpp
        src/fetchdeeznutzwindow.h
        resources/resources.qrc
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(fetchdeeznutz
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET fetchdeeznutz APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(fetchdeeznutz SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(fetchdeeznutz
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(fetchdeeznutz PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Concurrent)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.fetchdeeznutz)
endif()
set_target_properties(fetchdeeznutz PROPERTIES
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
#        COMMAND ${IMAGEMAGICK_CONVERT} nuts_icon.svg -resize 16x16 fetchdeeznutz-16.png
#        COMMAND ${IMAGEMAGICK_CONVERT} nuts_icon.svg -resize 24x24 fetchdeeznutz-24.png
#        COMMAND ${IMAGEMAGICK_CONVERT} nuts_icon.svg -resize 32x32 fetchdeeznutz-32.png
#        COMMAND ${IMAGEMAGICK_CONVERT} nuts_icon.svg -resize 48x48 fetchdeeznutz-48.png
#        COMMAND ${IMAGEMAGICK_CONVERT} nuts_icon.svg -resize 64x64 fetchdeeznutz-64.png
#        COMMAND ${IMAGEMAGICK_CONVERT} nuts_icon.svg -resize 128x128 fetchdeeznutz-128.png
#        COMMAND ${IMAGEMAGICK_CONVERT} nuts_icon.svg -resize 256x256 fetchdeeznutz-256.png
#        DEPENDS nuts_icon.svg
#        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
#        COMMENT "Generating PNG icons from SVG"
#    )
#    add_dependencies(fetchdeeznutz generate_icons)
#else()
#    message(STATUS "ImageMagick not found - PNG icons will not be generated")
#endif()

# Install the executable
install(TARGETS fetchdeeznutz
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Strip installed executable and libraries
# Note: DESTDIR is prepended to CMAKE_INSTALL_PREFIX during install
install(CODE "
    if(NOT CMAKE_STRIP)
        find_program(STRIP_TOOL strip)
        if(STRIP_TOOL)
            set(CMAKE_STRIP \"\${STRIP_TOOL}\")
        endif()
    endif()
    if(CMAKE_STRIP)
        # Handle DESTDIR - it's prepended to CMAKE_INSTALL_PREFIX
        set(DESTDIR_VALUE \"\$ENV{DESTDIR}\")
        if(DESTDIR_VALUE)
            # Remove trailing slash if present
            string(REGEX REPLACE \"/+\$\" \"\" DESTDIR_VALUE \"\${DESTDIR_VALUE}\")
            set(INSTALL_ROOT \"\${DESTDIR_VALUE}\${CMAKE_INSTALL_PREFIX}\")
        else()
            set(INSTALL_ROOT \"\${CMAKE_INSTALL_PREFIX}\")
        endif()
        set(BINARY_PATH \"\${INSTALL_ROOT}/${CMAKE_INSTALL_BINDIR}/fetchdeeznutz\")
        message(STATUS \"Checking for binary at: \${BINARY_PATH}\")
        if(EXISTS \"\${BINARY_PATH}\")
            message(STATUS \"Stripping \${BINARY_PATH}\")
            execute_process(
                COMMAND \"${CMAKE_STRIP}\" \"\${BINARY_PATH}\"
                RESULT_VARIABLE STRIP_RESULT
                OUTPUT_VARIABLE STRIP_OUTPUT
                ERROR_VARIABLE STRIP_ERROR
            )
            if(STRIP_RESULT)
                message(WARNING \"Failed to strip \${BINARY_PATH}: \${STRIP_RESULT}\")
                if(STRIP_ERROR)
                    message(WARNING \"Strip error: \${STRIP_ERROR}\")
                endif()
            else()
                message(STATUS \"Successfully stripped \${BINARY_PATH}\")
            endif()
        else()
            message(WARNING \"Binary not found at \${BINARY_PATH}\")
        endif()
        file(GLOB INSTALLED_LIBS \"\${INSTALL_ROOT}/${CMAKE_INSTALL_LIBDIR}/*.so*\")
        foreach(LIB \${INSTALLED_LIBS})
            if(EXISTS \"\${LIB}\")
                message(STATUS \"Stripping \${LIB}\")
                execute_process(
                    COMMAND \"${CMAKE_STRIP}\" \"\${LIB}\"
                    RESULT_VARIABLE STRIP_RESULT
                )
            endif()
        endforeach()
    else()
        message(WARNING \"CMAKE_STRIP not found - binaries will not be stripped\")
    endif()
")

# Install desktop file
install(FILES resources/fetchdeeznutz-install.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
    RENAME fetchdeeznutz.desktop
)

# Install icons
install(FILES resources/nuts_icon.svg
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
    RENAME fetchdeeznutz.svg
)

# Install PNG icons if they exist
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/resources/fetchdeeznutz-16.png)
    install(FILES resources/fetchdeeznutz-16.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/16x16/apps
        RENAME fetchdeeznutz.png
    )
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/resources/fetchdeeznutz-24.png)
    install(FILES resources/fetchdeeznutz-24.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/24x24/apps
        RENAME fetchdeeznutz.png
    )
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/resources/fetchdeeznutz-32.png)
    install(FILES resources/fetchdeeznutz-32.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/32x32/apps
        RENAME fetchdeeznutz.png
    )
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/resources/fetchdeeznutz-48.png)
    install(FILES resources/fetchdeeznutz-48.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/48x48/apps
        RENAME fetchdeeznutz.png
    )
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/resources/fetchdeeznutz-64.png)
    install(FILES resources/fetchdeeznutz-64.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/64x64/apps
        RENAME fetchdeeznutz.png
    )
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/resources/fetchdeeznutz-128.png)
    install(FILES resources/fetchdeeznutz-128.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/128x128/apps
        RENAME fetchdeeznutz.png
    )
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/resources/fetchdeeznutz-256.png)
    install(FILES resources/fetchdeeznutz-256.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps
        RENAME fetchdeeznutz.png
    )
endif()

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(fetchdeeznutz)
endif()
