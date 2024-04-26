cmake_minimum_required(VERSION 3.16...3.23)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_EXTENSIONS ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(ENABLE_DEVELOPER_MODE OFF CACHE BOOL "Enable 'developer mode'")
set(ENABLE_COVERAGE OFF CACHE BOOL "Enable 'coverage mode'")
set(OPT_WARNINGS_AS_ERRORS_DEVELOPER_DEFAULT TRUE)

# Set the project name and language
project(
  task_pool_examples
  VERSION 0.0
  DESCRIPTION " "
  HOMEPAGE_URL "%%https://github.com/benny-edlund/task-pool%%"
  LANGUAGES CXX )

set(GIT_SHA "Unknown" CACHE STRING "SHA this build was generated from")
string( SUBSTRING "${GIT_SHA}" 0 8 GIT_SHORT_SHA)

add_subdirectory(collage)
add_subdirectory(image_processing)
add_subdirectory(webserver)