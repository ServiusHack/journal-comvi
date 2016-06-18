project(journal-comvi CXX)
cmake_minimum_required(VERSION 3.1)

list(APPEND SRC_LIST "main.cpp")
list(APPEND SRC_LIST "ncurses.cpp")
list(APPEND SRC_LIST "outputter.cpp")
list(APPEND SRC_LIST "remote_journal_reader.cpp")
list(APPEND SRC_LIST "local_journal_reader.cpp")

add_executable(${PROJECT_NAME} ${SRC_LIST})

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wnon-virtual-dtor -Wcast-align -Wunused -Woverloaded-virtual -pedantic)

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED TRUE)

find_package(Curses REQUIRED)
include_directories(${CURSES_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} ${CURSES_LIBRARIES})

find_package(Boost REQUIRED COMPONENTS system date_time program_options)
include_directories(${Boost_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} ${Boost_LIBRARIES})

find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} "pthread")

find_library(SYSTEMD_LIBRARY NAMES systemd REQUIRED)
target_link_libraries(${PROJECT_NAME} ${SYSTEMD_LIBRARY})
