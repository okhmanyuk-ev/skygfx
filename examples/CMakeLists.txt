cmake_minimum_required(VERSION 3.10)
project(examples)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
endif()

set(LIBS_FOLDER "libs")

# glfw

set(GLFW_INSTALL OFF)
set(GLFW_BUILD_DOCS OFF)
add_subdirectory(lib/glfw)
set_property(TARGET glfw PROPERTY FOLDER ${LIBS_FOLDER}/glfw)
set_property(TARGET update_mappings PROPERTY FOLDER ${LIBS_FOLDER}/glfw)

# sky-gfx

add_subdirectory(../ ${CMAKE_CURRENT_BINARY_DIR}/skygfx)
set_property(TARGET sky-gfx PROPERTY FOLDER ${LIBS_FOLDER})

# triangle

add_executable(triangle triangle/main.cpp)
target_link_libraries(triangle glfw sky-gfx)
