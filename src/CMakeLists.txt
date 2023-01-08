project(lib-task-pool)
add_library(task_pool task_pool.cpp task_pool.h)

if (UNIX)
	target_link_libraries(task_pool PRIVATE pthread)
endif()
target_link_libraries(
  task_pool
  PRIVATE project_options project_warnings)

target_include_directories(task_pool PRIVATE "${CMAKE_BINARY_DIR}/configured_files/include")
