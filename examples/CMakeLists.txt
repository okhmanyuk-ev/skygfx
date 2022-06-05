cmake_minimum_required(VERSION 3.10)
project(examples)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


set(LIBS_FOLDER "libs")

# sky-gfx

add_subdirectory(../ ${CMAKE_CURRENT_BINARY_DIR}/skygfx)
set_property(TARGET sky-gfx PROPERTY FOLDER ${LIBS_FOLDER})

# glfw

set(GLFW_INSTALL OFF)
set(GLFW_BUILD_DOCS OFF)
add_subdirectory(lib/glfw)
set_property(TARGET glfw PROPERTY FOLDER ${LIBS_FOLDER}/glfw)
set_property(TARGET update_mappings PROPERTY FOLDER ${LIBS_FOLDER}/glfw)

# triangle

add_executable(triangle triangle/main.cpp)
target_link_libraries(triangle glfw sky-gfx)

# texture

add_executable(texture texture/main.cpp)
target_link_libraries(texture glfw sky-gfx)
target_include_directories(texture PUBLIC lib/stb)
set_target_properties(texture PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
