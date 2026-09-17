# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
#idf_build_set_property(MINIMAL_BUILD ON)

set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/common)

project(wifi_station)

#target_add_binary_data(${CMAKE_PROJECT_NAME}.elf "anh/anh.jpg" TEXT)
target_add_binary_data(${CMAKE_PROJECT_NAME}.elf "html/index.html" TEXT)