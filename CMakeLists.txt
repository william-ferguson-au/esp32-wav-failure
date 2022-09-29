cmake_minimum_required(VERSION 3.14)

# Set the version of the software burned into esp_app_desc_t in the DROM sector.
# Needs to be an integer value.
set (PROJECT_VER "1")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

SET (CMAKE_CXX_STANDARD 11)

project(wav-sound-test)

