cmake_minimum_required(VERSION 3.15...3.23)

project(CmakeConfigPackageTests LANGUAGES CXX)
#find_package(task-pool CONFIG REQUIRED) # for intro, project_options, ... <<< does not work

# ---- Dependencies ----

find_package(Catch2 CONFIG REQUIRED)


include(Catch)

add_library(catch_main OBJECT catch_main.cpp)
target_link_libraries(catch_main PUBLIC Catch2::Catch2)
target_link_libraries(catch_main PRIVATE project_options)

add_executable(test_taskpool tests.cpp)
include_directories(${lib-task-pool_SOURCE_DIR})
target_link_libraries(test_taskpool PRIVATE task_pool)
target_link_libraries(test_taskpool PRIVATE project_warnings project_options catch_main)
if(CMAKE_COMPILER_IS_GNUCXX)
    find_program(GCOVR "gcovr")
    if (CMAKE_BUILD_TYPE MATCHES "Debug" AND GCOVR)
	    include(CodeCoverage)
	    append_coverage_compiler_flags()
	    #set(GCOVR_PATH "/usr/bin/gcovr")
        setup_target_for_coverage_gcovr_xml(
                NAME xml_coverage_taskpool
                EXECUTABLE test_taskpool
                DEPENDENCIES test_taskpool task_pool
        )
        # Works
        setup_target_for_coverage_gcovr_html(
                NAME html_coverage_taskpool
                EXECUTABLE test_taskpool
                DEPENDENCIES test_taskpool task_pool
        )
    endif()
endif()

# automatically discover tests that are defined in catch based test files you can modify the unittests. Set TEST_PREFIX
# to whatever you want, or use different for different binaries

if (MSVC)
catch_discover_tests(
  test_taskpool
  TEST_PREFIX
  "unittests."
  #REPORTER     Seems windows doesnt like the REPORTER option
  #xml
  OUTPUT_DIR
  .
  OUTPUT_PREFIX
  "unittests."
  OUTPUT_SUFFIX
  .xml)
else() 
catch_discover_tests(
  test_taskpool
  TEST_PREFIX
  "unittests."
  REPORTER
  xml
  OUTPUT_DIR
  .
  OUTPUT_PREFIX
  "unittests."
  OUTPUT_SUFFIX
  .xml)
endif()
