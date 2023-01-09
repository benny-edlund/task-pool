 project(lib-task-pool)
add_library(task_pool task_pool.cpp task_pool.h)

if (UNIX)
	target_link_libraries(task_pool PRIVATE pthread)
endif()
target_link_libraries(
  task_pool
  PRIVATE project_options project_warnings)
if(CMAKE_COMPILER_IS_GNUCXX) # this SHOUILD be controlled by OPT_ENABLE_COVERAGE but doesnt seemt work
  if (CMAKE_BUILD_TYPE MATCHES "Debug")
    include(CodeCoverage)
    append_coverage_compiler_flags()
  endif()
endif()
target_include_directories(task_pool PRIVATE "${CMAKE_BINARY_DIR}/configured_files/include")
