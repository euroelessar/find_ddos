project(find_ddos)
cmake_minimum_required(VERSION 2.8)

find_package(Elliptics 2.24 REQUIRED)
find_package(Boost REQUIRED COMPONENTS thread program_options system)

include_directories(${ELLIPTICS_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})

aux_source_directory(. SRC_LIST)
add_executable(${PROJECT_NAME} ${SRC_LIST})
target_link_libraries(${PROJECT_NAME} ${ELLIPTICS_LIBRARIES} ${Boost_LIBRARIES})
set(CMAKE_CXX_FLAGS -std=c++0x)

