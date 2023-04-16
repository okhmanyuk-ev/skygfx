cmake_minimum_required(VERSION 3.10)
project(examples)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
	set(CMAKE_MACOSX_BUNDLE YES)
endif()

set(RESOURCES
	assets
)

set(LIBS_FOLDER "libs")

# skygfx

add_subdirectory(../ ${CMAKE_CURRENT_BINARY_DIR}/skygfx)
set_property(TARGET skygfx PROPERTY FOLDER ${LIBS_FOLDER})

# glfw

set(GLFW_INSTALL OFF)
set(GLFW_BUILD_DOCS OFF)
add_subdirectory(lib/glfw)
set_property(TARGET glfw PROPERTY FOLDER ${LIBS_FOLDER}/glfw)
set_property(TARGET update_mappings PROPERTY FOLDER ${LIBS_FOLDER}/glfw)

# imgui

file(GLOB IMGUI_SRC 
	lib/imgui/*.cpp 
	lib/imgui/*.h
	utils/imgui_helper.cpp
	lib/imgui/backends/imgui_impl_glfw.cpp
)
add_library(imgui STATIC ${IMGUI_SRC})
target_include_directories(imgui PUBLIC
	lib/imgui
	lib/imgui/backends
)
target_link_libraries(imgui skygfx glfw)
set_property(TARGET imgui PROPERTY FOLDER ${LIBS_FOLDER})

# 00 - tests

add_executable(00_tests 00_tests/main.cpp)
target_link_libraries(00_tests glfw skygfx)

# 01 - triangle

add_executable(01_triangle 01_triangle/main.cpp)
target_link_libraries(01_triangle glfw skygfx)

# 02 - uniform_buffer

add_executable(02_uniform_buffer 02_uniform/main.cpp)
target_link_libraries(02_uniform_buffer glfw skygfx)

# 03 - texture

add_executable(03_texture 03_texture/main.cpp ${RESOURCES})
target_link_libraries(03_texture glfw skygfx)
target_include_directories(03_texture PUBLIC lib/stb)
set_target_properties(03_texture PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_target_properties(03_texture PROPERTIES RESOURCE ${RESOURCES})

# 04 - texture_mipmap

add_executable(04_texture_mipmap 04_texture_mipmap/main.cpp ${RESOURCES})
target_link_libraries(04_texture_mipmap glfw skygfx)
target_include_directories(04_texture_mipmap PUBLIC lib/stb)
set_target_properties(04_texture_mipmap PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_target_properties(04_texture_mipmap PROPERTIES RESOURCE ${RESOURCES})

# 05 - cube

add_executable(05_cube 05_cube/main.cpp)
target_link_libraries(05_cube glfw skygfx)

# 06 - textured cube

add_executable(06_textured_cube 06_textured_cube/main.cpp ${RESOURCES})
target_link_libraries(06_textured_cube glfw skygfx)
target_include_directories(06_textured_cube PUBLIC lib/stb)
set_target_properties(06_textured_cube PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_target_properties(06_textured_cube PROPERTIES RESOURCE ${RESOURCES})

# 07 - light

add_executable(07_light 07_light/main.cpp ${RESOURCES})
target_link_libraries(07_light glfw skygfx)
target_include_directories(07_light PUBLIC lib/stb)
set_target_properties(07_light PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_target_properties(07_light PROPERTIES RESOURCE ${RESOURCES})

# 08 - render target

add_executable(08_render_target 08_render_target/main.cpp)
target_link_libraries(08_render_target glfw skygfx)

# 09 - imgui

add_executable(09_imgui 09_imgui/main.cpp)
target_link_libraries(09_imgui glfw skygfx imgui)

# 10 - bloom

add_executable(10_bloom 10_bloom/main.cpp ${RESOURCES})
target_link_libraries(10_bloom glfw skygfx imgui)
target_include_directories(10_bloom PUBLIC lib/stb)
set_target_properties(10_bloom PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_target_properties(10_bloom PROPERTIES RESOURCE ${RESOURCES})

# 11 - raytraced triangle

add_executable(11_raytraced_triangle 11_raytraced_triangle/main.cpp ${RESOURCES})
target_link_libraries(11_raytraced_triangle glfw skygfx)
