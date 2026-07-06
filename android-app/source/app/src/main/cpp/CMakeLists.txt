cmake_minimum_required(VERSION 3.22.1)

project("eccles")

add_library(${CMAKE_PROJECT_NAME} SHARED
        native-lib.cpp
        eccles/EcclesTypes.cpp
        eccles/Command.cpp
        eccles/CommandFactory.cpp
        eccles/Transport.cpp
        eccles/Engine.cpp
        eccles/ConversationAudio.cpp
        eccles/main.cpp)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE eccles)

target_link_libraries(${CMAKE_PROJECT_NAME}
        android
        log
        c++_shared
        aaudio)
