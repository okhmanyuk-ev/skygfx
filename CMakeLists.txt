cmake_minimum_required(VERSION 3.10)
project(sky-gfx)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB ALL_SRC
	src/skygfx/*.cpp
	src/skygfx/*.h
)

source_group("src" FILES ${ALL_SRC})

add_library(${PROJECT_NAME} STATIC
	${ALL_SRC}
)

target_include_directories(${PROJECT_NAME} PUBLIC src)

if(WIN32) # TODO: should be available not only in win32 (macos, linux also should be)
	file(GLOB GLEW_SRC 
		lib/glew/src/glew.c
		lib/glew/include/GL/glew.h
	)
	add_library(glew STATIC ${GLEW_SRC})
	target_link_libraries(${PROJECT_NAME} glew)
	target_include_directories(glew PUBLIC lib/glew/include)
	target_compile_definitions(glew PRIVATE -DGLEW_STATIC)
endif()

# examples

add_executable(example
	examples/main.cpp
)

set(GLFW_INSTALL OFF)
set(GLFW_BUILD_DOCS OFF)
add_subdirectory(examples/glfw)
target_link_libraries(example 
	glfw
	sky-gfx
)

if(WIN32)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
endif()