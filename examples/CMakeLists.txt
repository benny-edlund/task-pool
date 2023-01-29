cmake_minimum_required(VERSION 3.15...3.23)

find_package(fmt CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(libjpeg-turbo CONFIG REQUIRED)

add_executable(example_one one.cpp)
target_link_libraries(example_one PRIVATE fmt::fmt)
target_link_libraries(example_one PRIVATE task_pool_static)
target_link_libraries(example_one PRIVATE project_warnings project_options )