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

set(LIBS_FOLDER "libs")

# glew

file(GLOB GLEW_SRC 
	lib/glew/src/glew.c
	lib/glew/include/GL/glew.h
)
add_library(glew STATIC ${GLEW_SRC})
target_link_libraries(${PROJECT_NAME} glew)
target_include_directories(glew PUBLIC lib/glew/include)
target_compile_definitions(glew PRIVATE -DGLEW_STATIC)
set_property(TARGET glew PROPERTY FOLDER ${LIBS_FOLDER})

# glslang

option(ENABLE_HLSL OFF)
set(SKIP_GLSLANG_INSTALL ON)
option(ENABLE_CTEST OFF)
option(ENABLE_GLSLANG_BINARIES OFF)
option(ENABLE_SPVREMAPPER OFF)
add_subdirectory(lib/glslang/glslang)
target_include_directories(${PROJECT_NAME} PRIVATE lib/glslang)
target_link_libraries(${PROJECT_NAME}
	SPIRV
	glslang
)
set_property(TARGET glslang PROPERTY FOLDER ${LIBS_FOLDER}/glslang)
set_property(TARGET GenericCodeGen PROPERTY FOLDER ${LIBS_FOLDER}/glslang)
set_property(TARGET MachineIndependent PROPERTY FOLDER ${LIBS_FOLDER}/glslang)
set_property(TARGET OGLCompiler PROPERTY FOLDER ${LIBS_FOLDER}/glslang)
set_property(TARGET OSDependent PROPERTY FOLDER ${LIBS_FOLDER}/glslang)
set_property(TARGET SPIRV PROPERTY FOLDER ${LIBS_FOLDER}/glslang)

# spirv-cross

set(SPIRV_CROSS_SKIP_INSTALL ON)
set(SPIRV_CROSS_ENABLE_TESTS OFF)
set(SPIRV_CROSS_CLI OFF)
set(SPIRV_CROSS_ENABLE_MSL OFF)
set(SPIRV_CROSS_ENABLE_C_API OFF)
set(SPIRV_CROSS_ENABLE_CPP OFF)
set(SPIRV_CROSS_ENABLE_UTIL OFF)
add_subdirectory(lib/spirv-cross)
target_link_libraries(${PROJECT_NAME}
	spirv-cross-core
	spirv-cross-glsl
	spirv-cross-hlsl
	spirv-cross-reflect
)
set_property(TARGET spirv-cross-core PROPERTY FOLDER ${LIBS_FOLDER}/spirv-cross)
set_property(TARGET spirv-cross-glsl PROPERTY FOLDER ${LIBS_FOLDER}/spirv-cross)
set_property(TARGET spirv-cross-hlsl PROPERTY FOLDER ${LIBS_FOLDER}/spirv-cross)
set_property(TARGET spirv-cross-reflect PROPERTY FOLDER ${LIBS_FOLDER}/spirv-cross)

# glm

set(GLM_QUIET ON)
set(BUILD_SHARED_LIBS OFF)
set(GLM_TEST_ENABLE OFF)
add_subdirectory(lib/glm)
target_compile_definitions(${PROJECT_NAME} PUBLIC -DGLM_ENABLE_EXPERIMENTAL)
target_link_libraries(${PROJECT_NAME} glm)
target_include_directories(${PROJECT_NAME} PUBLIC lib/glm)
target_compile_definitions(glm INTERFACE -D_CRT_SECURE_NO_WARNINGS)

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