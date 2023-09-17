cmake_minimum_required(VERSION 3.16...3.23)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_EXTENSIONS ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(ENABLE_DEVELOPER_MODE OFF CACHE BOOL "Enable 'developer mode'")
set(ENABLE_COVERAGE OFF CACHE BOOL "Enable 'coverage mode'")
set(OPT_WARNINGS_AS_ERRORS_DEVELOPER_DEFAULT TRUE)

# Set the project name and language
project(
  task_pool
  VERSION 3.1.0
  DESCRIPTION ""
  HOMEPAGE_URL "%%https://github.com/benny-edlund/task-pool%%"
  LANGUAGES CXX )

# # This variable is set by project() in CMake 3.21+
string(
  COMPARE EQUAL
          "${CMAKE_SOURCE_DIR}"
          "${PROJECT_SOURCE_DIR}"
          PROJECT_IS_TOP_LEVEL)
if(PROJECT_IS_TOP_LEVEL)
  include(CTest)
  if(CMAKE_COMPILER_IS_GNUCXX)
    LIST(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake_modules")
  endif()
endif()

set(GIT_SHA "Unknown" CACHE STRING "SHA this build was generated from")
string( SUBSTRING "${GIT_SHA}" 0 8 GIT_SHORT_SHA)

add_subdirectory(lib)

# Adding the tests:
option(ENABLE_TESTING "Enable the tests" ${PROJECT_IS_TOP_LEVEL})
if(ENABLE_TESTING)
  enable_testing()
   add_subdirectory(test)
endif()

# If MSVC is being used, and ASAN is enabled, we need to set the debugger environment
# so that it behaves well with MSVC's debugger, and we can run the target from visual studio

if(CMAKE_SKIP_INSTALL_RULES)
  return()
elseif(NOT PROJECT_IS_TOP_LEVEL)
  return()
endif()

set(CPACK_PACKAGE_FILE_NAME
    "${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION}-${GIT_SHORT_SHA}-${CMAKE_SYSTEM_NAME}-${CMAKE_BUILD_TYPE}-${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}"
)

include(CPack)
