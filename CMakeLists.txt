cmake_minimum_required(VERSION 2.8)
project(dtplayer)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}")

SET(CMAKE_INCLUDE_CURRENT_DIR ON)

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR})
set(Boost_USE_MULTITHREADED      ON)
set(Boost_USE_STATIC_LIBS        ON)
set(Boost_USE_STATIC_RUNTIME    ON)

find_package( ffmpeg REQUIRED)
find_package( SDL2  REQUIRED)
find_package( faad )
find_package( Threads )

add_definitions(-D__STDC_CONSTANT_MACROS)

#dont't define this, libbt need depreated api
include_directories(${PROJECT_SOURCE_DIR})
include_directories(${FFMPEG_INCLUDE_DIR})
include_directories(${SDL2_INCLUDE_DIR})

include_directories(dtutils)
include_directories(dtplayer)
include_directories(dthost)

add_subdirectory(dtutils)
add_subdirectory(dtplayer)
add_subdirectory(dthost)
add_subdirectory(dtport)
add_subdirectory(dtdemux)
add_subdirectory(dtstream)
add_subdirectory(dtvideo)
add_subdirectory(dtaudio)


add_executable(dtmplayer dtm_player.c version.c)
target_link_libraries(dtmplayer ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(dtmplayer dtplayercore  dthost dtport dtdemux dtstream dtvideo dtaudio dtutils)


