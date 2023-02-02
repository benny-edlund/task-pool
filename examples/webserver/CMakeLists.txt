cmake_minimum_required(VERSION 3.15...3.23)

find_package(task_pool REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED)

add_executable(example_http main.cpp server.cpp)
target_link_libraries(example_http PRIVATE task_pool::task_pool )
target_link_libraries(example_http PRIVATE Boost::headers )
target_link_libraries(example_http PRIVATE fmt::fmt)
target_link_libraries(example_http PRIVATE CLI11::CLI11 )
target_link_libraries(example_http PRIVATE project_warnings project_options )

