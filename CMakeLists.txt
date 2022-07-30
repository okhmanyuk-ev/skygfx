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

macro(set_option option value)
  set(${option} ${value} CACHE INTERNAL "" FORCE)
endmacro()

if(WIN32)
	target_compile_definitions(${PROJECT_NAME} PRIVATE -DNOMINMAX)
	target_compile_definitions(${PROJECT_NAME} PRIVATE -DSKYGFX_HAS_D3D11)
	target_compile_definitions(${PROJECT_NAME} PRIVATE -DSKYGFX_HAS_OPENGL)
	target_compile_definitions(${PROJECT_NAME} PRIVATE -DSKYGFX_HAS_VULKAN)
elseif(APPLE)
	target_compile_definitions(${PROJECT_NAME} PRIVATE -DSKYGFX_HAS_METAL)

	target_link_libraries(${PROJECT_NAME}
		"-ObjC"
		"-framework Metal"
	)
endif()

# glew

if (NOT ANDROID)
	if (NOT TARGET glew)
		file(GLOB GLEW_SRC 
			lib/glew/src/glew.c
			lib/glew/include/GL/glew.h
		)
		add_library(glew STATIC ${GLEW_SRC})
		target_include_directories(glew PUBLIC lib/glew/include)
		target_compile_definitions(glew PRIVATE -DGLEW_STATIC)
		set_property(TARGET glew PROPERTY FOLDER ${LIBS_FOLDER})
	endif()
	target_link_libraries(${PROJECT_NAME} glew)
endif()

# glslang

set_option(ENABLE_HLSL OFF)
set_option(SKIP_GLSLANG_INSTALL ON)
set_option(ENABLE_CTEST OFF)
set_option(ENABLE_GLSLANG_BINARIES OFF)
set_option(ENABLE_SPVREMAPPER OFF)
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

set_option(SPIRV_CROSS_SKIP_INSTALL ON)
set_option(SPIRV_CROSS_ENABLE_TESTS OFF)
set_option(SPIRV_CROSS_CLI OFF)
set_option(SPIRV_CROSS_ENABLE_C_API OFF)
set_option(SPIRV_CROSS_ENABLE_CPP OFF)
set_option(SPIRV_CROSS_ENABLE_UTIL OFF)
add_subdirectory(lib/spirv-cross)
target_link_libraries(${PROJECT_NAME}
	spirv-cross-core
	spirv-cross-glsl
	spirv-cross-hlsl
	spirv-cross-msl
	spirv-cross-reflect
)
set_property(TARGET spirv-cross-core PROPERTY FOLDER ${LIBS_FOLDER}/spirv-cross)
set_property(TARGET spirv-cross-glsl PROPERTY FOLDER ${LIBS_FOLDER}/spirv-cross)
set_property(TARGET spirv-cross-hlsl PROPERTY FOLDER ${LIBS_FOLDER}/spirv-cross)
set_property(TARGET spirv-cross-msl PROPERTY FOLDER ${LIBS_FOLDER}/spirv-cross)
set_property(TARGET spirv-cross-reflect PROPERTY FOLDER ${LIBS_FOLDER}/spirv-cross)

# spirv-reflect

file(GLOB SPIRV_REFLECT_SRC 
	lib/spirv-reflect/spirv_reflect.cpp 
	lib/spirv-reflect/spirv_reflect.h
)
target_include_directories(${PROJECT_NAME} PUBLIC lib/spirv-reflect)
add_library(spirv-reflect STATIC ${SPIRV_REFLECT_SRC})
target_link_libraries(${PROJECT_NAME} spirv-reflect)
set_property(TARGET spirv-reflect PROPERTY FOLDER ${LIBS_FOLDER})

# glm

if (NOT TARGET glm)
	set(GLM_QUIET ON)
	set(BUILD_SHARED_LIBS OFF)
	set(GLM_TEST_ENABLE OFF)
	add_subdirectory(lib/glm)
	target_compile_definitions(${PROJECT_NAME} PUBLIC -DGLM_ENABLE_EXPERIMENTAL)
	target_include_directories(${PROJECT_NAME} PUBLIC lib/glm)
	target_compile_definitions(glm INTERFACE -D_CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(${PROJECT_NAME} glm)

# vulkan

add_library(vulkan INTERFACE)
target_link_libraries(vulkan INTERFACE ${PROJECT_SOURCE_DIR}/lib/vulkan/lib/vulkan-1.lib)
target_include_directories(vulkan INTERFACE lib/vulkan/include)
if(WIN32)
	target_compile_definitions(vulkan INTERFACE -DVK_USE_PLATFORM_WIN32_KHR)
endif()
target_link_libraries(${PROJECT_NAME} vulkan)

# metal-cpp

target_include_directories(${PROJECT_NAME} PUBLIC lib/metal-cpp)
target_include_directories(${PROJECT_NAME} PUBLIC lib/metal-cpp-extensions)
