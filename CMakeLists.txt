# SPDX-License-Identifier: AGPL-3.0-or-later
# (C) 2023 Bernhard Rosenkränzer <bero@lindev.ch>

cmake_minimum_required(VERSION 3.26)
project(repodata-tools VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 20)

find_package(Qt6 6.1.0 CONFIG REQUIRED COMPONENTS Core Xml)
add_definitions(${QT_DEFINITIONS})
add_definitions(-DQT_DISABLE_DEPRECATED_BEFORE=0x060600)

find_package(PkgConfig REQUIRED)
pkg_search_module(LIBARCHIVE REQUIRED libarchive)

# FIXME why doesn't anything set QT_INCLUDES?
message("${QT6_INCLUDES} ${QT_INCLUDES}")
include_directories(/usr/include/qt6 /usr/include/qt6/QtCore /usr/include/qt6/QtGui /usr/include/qt6/QtWidgets /usr/include/qt6/QtXml)

add_library(rpmpp Archive.cpp String.cpp FileName.cpp Rpm.cpp Compression.cpp DesktopFile.cpp)
target_include_directories(rpmpp PUBLIC ${LIBARCHIVE_INCLUDE_DIRS})
target_compile_options(rpmpp PUBLIC ${LIBARCHIVE_CFLAGS_OTHER})
target_link_libraries(rpmpp rpmio rpm Qt6::Core Qt6::Xml ${LIBARCHIVE_LIBRARIES})

add_executable(createmd createmd.cpp Sha256.cpp)
target_link_libraries(createmd rpmpp)
