cmake_minimum_required(VERSION 3.10)
project(skygfx)

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
	target_compile_definitions(${PROJECT_NAME} PRIVATE -DSKYGFX_PLATFORM_WINDOWS)
	target_compile_definitions(${PROJECT_NAME} PRIVATE -DNOMINMAX)
	target_compile_definitions(${PROJECT_NAME} PRIVATE -DSKYGFX_HAS_D3D11)
	target_compile_definitions(${PROJECT_NAME} PRIVATE -DSKYGFX_HAS_D3D12)
	target_compile_definitions(${PROJECT_NAME} PRIVATE -DSKYGFX_HAS_OPENGL)
	target_compile_definitions(${PROJECT_NAME} PRIVATE -DSKYGFX_HAS_VULKAN)
elseif(APPLE)
	if(CMAKE_OSX_SYSROOT STREQUAL iphoneos)
		set(IOS True)
	endif()

	target_compile_definitions(${PROJECT_NAME} PRIVATE -DSKYGFX_HAS_METAL)
	target_compile_definitions(${PROJECT_NAME} PRIVATE -DSKYGFX_HAS_OPENGL)

	target_link_libraries(${PROJECT_NAME}
		"-ObjC"
		"-framework IOSurface"
		"-framework Metal"
		"-framework MetalKit"
		"-framework GLKit"
		"-framework Foundation"
		"-framework QuartzCore"
	)
	
	set(OBJC_SOURCES
		src/skygfx/backend_gl.cpp
		src/skygfx/backend_mtl.cpp
	)

	set_source_files_properties(${OBJC_SOURCES} PROPERTIES COMPILE_FLAGS "-x objective-c++ -fembed-bitcode")
	
	if(IOS)
		target_compile_definitions(${PROJECT_NAME} PRIVATE -DSKYGFX_PLATFORM_IOS)
		add_definitions(-DGLES_SILENCE_DEPRECATION)
	
		target_link_libraries(${PROJECT_NAME}
			"-framework OpenGLES"
			"-framework UIKit"
		)
	else()
		target_compile_definitions(${PROJECT_NAME} PRIVATE -DSKYGFX_PLATFORM_MACOS)
		add_definitions(-DGL_SILENCE_DEPRECATION)

		target_link_libraries(${PROJECT_NAME}
			"-framework OpenGL"
			"-framework Cocoa"
		)
	endif()
elseif(EMSCRIPTEN)
	target_compile_definitions(${PROJECT_NAME} PRIVATE -DSKYGFX_PLATFORM_EMSCRIPTEN)
	target_compile_definitions(${PROJECT_NAME} PRIVATE -DSKYGFX_HAS_OPENGL)
	target_link_options(${PROJECT_NAME} PUBLIC -sUSE_WEBGL2)
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

if (NOT EMSCRIPTEN)
	add_library(vulkan INTERFACE)
	target_include_directories(vulkan INTERFACE lib/vulkan/include)
	if(WIN32)
		target_compile_definitions(vulkan INTERFACE -DVK_USE_PLATFORM_WIN32_KHR)
		target_compile_definitions(vulkan INTERFACE -DVULKAN_HPP_DISPATCH_LOADER_DYNAMIC=0)
	elseif(APPLE)
		target_compile_definitions(vulkan INTERFACE -DVULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL=0)
		if(IOS)
			target_compile_definitions(vulkan INTERFACE -DVK_USE_PLATFORM_IOS_MVK)
		else()
			target_compile_definitions(vulkan INTERFACE -DVK_USE_PLATFORM_MACOS_MVK)
		endif()
	endif()
	target_link_libraries(${PROJECT_NAME} vulkan)
endif()

# d3dx12

if(WIN32)	
	file(GLOB D3DX12_SRC
		lib/d3dx12/d3dx12/*.cpp 
		lib/d3dx12/d3dx12/*.h
	)
	add_library(d3dx12 STATIC ${D3DX12_SRC})
	target_include_directories(d3dx12 PUBLIC lib/d3dx12)
	target_link_libraries(${PROJECT_NAME} d3dx12)
	set_property(TARGET d3dx12 PROPERTY FOLDER ${LIBS_FOLDER})
endif()
