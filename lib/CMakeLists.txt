include(GenerateExportHeader)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

set(HEADER_LIST 
	${CMAKE_CURRENT_BINARY_DIR}/task_pool/api.h
	${CMAKE_CURRENT_SOURCE_DIR}/public/task_pool/fallbacks.h
	${CMAKE_CURRENT_SOURCE_DIR}/public/task_pool/traits.h
	${CMAKE_CURRENT_SOURCE_DIR}/public/task_pool/pool.h 
	${CMAKE_CURRENT_SOURCE_DIR}/public/task_pool/pipes.h 
)

# Static library
add_library(task_pool_static task_pool.cpp "${HEADER_LIST}")
target_include_directories(
  task_pool_static 
  PUBLIC  
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public> 
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)
target_include_directories(task_pool_static PRIVATE "${CMAKE_BINARY_DIR}/configured_files/include")
target_compile_definitions( task_pool_static PUBLIC task_pool_STATIC_DEFINE )
if (UNIX)
  target_link_libraries(task_pool_static PRIVATE pthread)
endif()

# Shared library
add_library(task_pool SHARED task_pool.cpp "${HEADER_LIST}")
target_include_directories(
  task_pool 
  PUBLIC  
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public> 
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
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

install(TARGETS task_pool task_pool_static
EXPORT task_pool_targets
LIBRARY DESTINATION lib
ARCHIVE DESTINATION lib
RUNTIME DESTINATION bin
INCLUDES DESTINATION include
)

install(DIRECTORY public/task_pool DESTINATION include FILES_MATCHING PATTERN "*.h")
install(FILES ${PROJECT_BINARY_DIR}/lib/task_pool/api.h DESTINATION include/task_pool)

include(CMakePackageConfigHelpers)
write_basic_package_version_file( task_poolConfigVersion.cmake VERSION "${task_pool_VERSION}" COMPATIBILITY AnyNewerVersion )
export(EXPORT task_pool_targets FILE "${CMAKE_CURRENT_BINARY_DIR}/task_poolTargets.cmake"  NAMESPACE task_pool:: )
configure_package_config_file(${CMAKE_CURRENT_LIST_DIR}/config.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/task_poolConfig.cmake" INSTALL_DESTINATION lib/cmake/task_pool)
install(EXPORT task_pool_targets FILE task_poolTargets.cmake DESTINATION lib/cmake/task_pool NAMESPACE task_pool:: )
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/task_poolConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/task_poolConfigVersion.cmake"
  DESTINATION lib/cmake/task_pool 
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
