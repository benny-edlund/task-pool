include(GenerateExportHeader)

find_package(Tracy REQUIRED)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

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

if ( CMAKE_COMPILER_IS_GNUCXX AND ENABLE_COVERAGE )
  target_compile_options(task_pool PRIVATE -g -coverage )
  target_compile_options(task_pool_static PRIVATE -g -coverage )
endif()

if ( ENABLE_DEVELOPER_MODE )
  target_compile_options(task_pool PRIVATE -fsanitize=address,undefined,leak )
  target_link_options(task_pool PRIVATE -fsanitize=address,undefined,leak )
endif()

if(MSVC)
  set_target_properties(task_pool PROPERTIES VS_DEBUGGER_ENVIRONMENT "PATH=$(VC_ExecutablePath_x64);%PATH%")
endif()

# if(TRACY_PROFILING_ENABLED)
  target_link_libraries(task_pool PRIVATE Tracy::TracyClient)
  target_include_directories(task_pool PRIVATE ${Tracy_INCLUDE_DIRS})
  target_compile_definitions(task_pool PRIVATE ${Tracy_DEFINITIONS})

  target_link_libraries(task_pool_static PRIVATE Tracy::TracyClient)
  target_include_directories(task_pool_static PRIVATE ${Tracy_INCLUDE_DIRS})
  target_compile_definitions(task_pool_static PRIVATE ${Tracy_DEFINITIONS})

  # endif()


install(TARGETS task_pool_static task_pool )
install(DIRECTORY public/task_pool DESTINATION include )
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/task_pool/api.h DESTINATION include/task_pool )