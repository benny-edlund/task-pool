cmake_minimum_required(VERSION 3.15...3.23)
find_package(task_pool REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(libjpeg-turbo CONFIG REQUIRED)
find_package(Curses CONFIG REQUIRED)

add_executable(example_collage main.cpp)
if (UNIX)
    target_link_libraries(example_collage PRIVATE pthread )
endif()
target_link_libraries(example_collage PRIVATE task_pool::task_pool )
target_link_libraries(example_collage PRIVATE fmt::fmt)
target_link_libraries(example_collage PRIVATE libjpeg-turbo::turbojpeg-static)
target_link_libraries(example_collage PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(example_collage PRIVATE CLI11::CLI11 )
target_link_libraries(example_collage PRIVATE CURL::libcurl)
target_link_libraries(example_collage PRIVATE ncurses::libcurses)