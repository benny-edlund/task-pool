include(GenerateExportHeader)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

set(HEADER_LIST 
	${CMAKE_CURRENT_BINARY_DIR}/task_pool/api.h
	${CMAKE_CURRENT_SOURCE_DIR}/public/task_pool/fallbacks.h
	${CMAKE_CURRENT_SOURCE_DIR}/public/task_pool/traits.h
	${CMAKE_CURRENT_SOURCE_DIR}/public/task_pool/task_pool.h 
)

# Static library
add_library(task_pool_static task_pool.cpp "${HEADER_LIST}")
set_target_properties(task_pool_static PROPERTIES PUBLIC_HEADER "${HEADER_LIST}")
target_include_directories(
  task_pool_static 
  PUBLIC  
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public> 
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  $<INSTALL_INTERFACE:include> 
)
target_include_directories(task_pool_static PRIVATE "${CMAKE_BINARY_DIR}/configured_files/include")
target_link_libraries( task_pool_static PRIVATE project_options project_warnings)
target_compile_definitions( task_pool_static PUBLIC TASKPOOL_STATIC_DEFINE )
if (UNIX)
  target_link_libraries(task_pool_static PRIVATE pthread)
endif()



# Shared library
add_library(task_pool SHARED task_pool.cpp "${HEADER_LIST}")
set_target_properties(task_pool PROPERTIES PUBLIC_HEADER "${HEADER_LIST}")
target_include_directories(
  task_pool 
  PUBLIC  
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public> 
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  $<INSTALL_INTERFACE:include> 
)
target_include_directories(task_pool PRIVATE "${CMAKE_BINARY_DIR}/configured_files/include")
target_link_libraries( task_pool PRIVATE project_options project_warnings)
if (UNIX)
target_link_libraries(task_pool PRIVATE pthread)
endif()

generate_export_header(
  task_pool
  BASE_NAME TASKPOOL
  EXPORT_FILE_NAME task_pool/api.h
  EXPORT_MACRO_NAME TASKPOOL_API
  NO_EXPORT_MACRO_NAME TASKPOOL_HIDDEN
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/task_pool/api.h DESTINATION ${INCLUDE_INSTALL_DIR}/task_pool/api.h )