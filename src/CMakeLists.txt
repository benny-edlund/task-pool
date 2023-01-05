#find_package(fmt CONFIG )
#find_package(SPDLOG CONFIG REQUIRED)

add_library(task_pool task_pool.cpp)

target_link_libraries(
  task_pool
  PUBLIC project_options project_warnings)
  #PRIVATE fmt::fmt spdlog::spdlog)

target_include_directories(task_pool PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(task_pool PRIVATE "${CMAKE_BINARY_DIR}/configured_files/include")
