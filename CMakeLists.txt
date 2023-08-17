# SPDX-License-Identifier: AGPL-3.0-or-later
# (C) 2023 Bernhard Rosenkränzer <bero@lindev.ch>

cmake_minimum_required(VERSION 3.26)
project(repodata-tools VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 20)

find_package(Qt6 6.1.0 CONFIG REQUIRED COMPONENTS Core Gui Xml)
add_definitions(${QT_DEFINITIONS})
add_definitions(-DQT_DISABLE_DEPRECATED_BEFORE=0x060600)

find_package(PkgConfig REQUIRED)
pkg_search_module(LIBARCHIVE REQUIRED libarchive)

add_library(rpmpp STATIC Archive.cpp String.cpp FileName.cpp Rpm.cpp Compression.cpp DesktopFile.cpp)
target_include_directories(rpmpp PUBLIC ${LIBARCHIVE_INCLUDE_DIRS})
target_compile_options(rpmpp PUBLIC ${LIBARCHIVE_CFLAGS_OTHER})
target_link_libraries(rpmpp rpmio rpm Qt6::Core Qt6::Gui Qt6::Xml ${LIBARCHIVE_LIBRARIES})

add_executable(createmd createmd.cpp Sha256.cpp)
target_link_libraries(createmd rpmpp rpmio rpm Qt6::Core Qt6::Gui Qt6::Xml ${LIBARCHIVE_LIBRARIES})

install(TARGETS createmd DESTINATION bin)
