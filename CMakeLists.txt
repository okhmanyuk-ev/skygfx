cmake_minimum_required(VERSION 3.10)
project(sky-gfx)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(example
	examples/main.cpp
)

set(GLFW_INSTALL OFF)
set(GLFW_BUILD_DOCS OFF)
add_subdirectory(examples/glfw)
target_link_libraries(example glfw)