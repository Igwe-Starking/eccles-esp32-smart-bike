# Eccles Bike Control - ESP-IDF port
# This is the top level project CMakeLists.txt, the standard esp-idf project entry point.
# It pulls in the idf build system, declares this as project "EcclesBikeControl", and lets
# idf.py discover "main" and "components/eccles" automatically (EXTRA_COMPONENT_DIRS is not
# needed since components/ is one of idf's default component search paths).

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "1.0.0")
project(EcclesBikeControl)

# automatically builds the spiffs partition image from spiffs_image/ (which holds
# StaticModel.eccles, the packaged tts audio bundle) and flashes it alongside the app
# whenever "idf.py flash" is run, replacing platformio's separate "uploadfs" data/ step
littlefs_create_partition_image(littlefs littlefs_image FLASH_IN_PROJECT)