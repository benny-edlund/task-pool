
set(HEADER_LIST 
"${CMAKE_CURRENT_SOURCE_DIR}/public/task_pool/api.h"
"${CMAKE_CURRENT_SOURCE_DIR}/public/task_pool/fallbacks.h"
"${CMAKE_CURRENT_SOURCE_DIR}/public/task_pool/traits.h"
"${CMAKE_CURRENT_SOURCE_DIR}/public/task_pool/task_pool.h" )

# Static library
add_library(task_pool_static task_pool.cpp "${HEADER_LIST}")
target_compile_options(task_pool_static PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
set_target_properties(task_pool_static PROPERTIES PUBLIC_HEADER "${HEADER_LIST}")

target_include_directories(task_pool_static PUBLIC  
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public>  
  $<INSTALL_INTERFACE:include> 
)
target_include_directories(task_pool_static PRIVATE "${CMAKE_BINARY_DIR}/configured_files/include")

if (UNIX)
target_link_libraries(task_pool_static PRIVATE pthread)
endif()
target_link_libraries(
task_pool_static
PRIVATE project_options project_warnings)


# Shared library
add_library(task_pool SHARED task_pool.cpp "${HEADER_LIST}")
if (UNIX)
  target_compile_options(task_pool PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
endif()
set_target_properties(task_pool PROPERTIES PUBLIC_HEADER "${HEADER_LIST}")

target_include_directories(task_pool PUBLIC  
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public>  
  $<INSTALL_INTERFACE:include> 
)
target_include_directories(task_pool PRIVATE "${CMAKE_BINARY_DIR}/configured_files/include")

if (UNIX)
target_link_libraries(task_pool PRIVATE pthread)
endif()
target_link_libraries(
task_pool
PRIVATE project_options project_warnings)
