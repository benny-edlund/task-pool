
set(HEADER_LIST 
  "${CMAKE_CURRENT_SOURCE_DIR}/public/task_pool/fallbacks.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/public/task_pool/traits.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/public/task_pool/task_pool.h" )

add_library(task_pool SHARED task_pool.cpp "${HEADER_LIST}")
target_compile_definitions(task_pool PRIVATE TASKPOOL_COMPILE)
target_compile_options(task_pool PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
set_target_properties(task_pool PROPERTIES PUBLIC_HEADER "${HEADER_LIST}")

target_include_directories(task_pool PUBLIC  
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public>  
    $<INSTALL_INTERFACE:include>  # <prefix>/include/mylib
)
target_include_directories(task_pool PRIVATE "${CMAKE_BINARY_DIR}/configured_files/include")

if (UNIX)
	target_link_libraries(task_pool PRIVATE pthread)
endif()
target_link_libraries(
  task_pool
  PRIVATE project_options project_warnings)
