set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(EXAMPLES_FOLDER "skygfx-examples")

if(APPLE)
	set(CMAKE_MACOSX_BUNDLE YES)
endif()

set(RESOURCES
	assets
)

set(LIBS_FOLDER "libs")

# glfw

if(NOT TARGET glfw)
	set(GLFW_INSTALL OFF)
	set(GLFW_BUILD_DOCS OFF)
	add_subdirectory(lib/glfw)
	set_property(TARGET glfw PROPERTY FOLDER ${LIBS_FOLDER}/glfw)
	set_property(TARGET update_mappings PROPERTY FOLDER ${LIBS_FOLDER}/glfw)
endif()

# imgui

if(NOT TARGET imgui)
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
	set_property(TARGET imgui PROPERTY FOLDER ${LIBS_FOLDER})
endif()

# 01 - triangle

add_executable(01_triangle 01_triangle/main.cpp)
target_link_libraries(01_triangle glfw skygfx)
set_property(TARGET 01_triangle PROPERTY FOLDER ${EXAMPLES_FOLDER})

# 02 - uniform_buffer

add_executable(02_uniform_buffer 02_uniform/main.cpp)
target_link_libraries(02_uniform_buffer glfw skygfx)
set_property(TARGET 02_uniform_buffer PROPERTY FOLDER ${EXAMPLES_FOLDER})

# 03 - texture

add_executable(03_texture 03_texture/main.cpp ${RESOURCES})
target_link_libraries(03_texture glfw skygfx)
target_include_directories(03_texture PUBLIC lib/stb)
set_target_properties(03_texture PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(03_texture PROPERTIES RESOURCE ${RESOURCES})
set_property(TARGET 03_texture PROPERTY FOLDER ${EXAMPLES_FOLDER})

# 04 - texture_mipmap

add_executable(04_texture_mipmap 04_texture_mipmap/main.cpp ${RESOURCES})
target_link_libraries(04_texture_mipmap glfw skygfx)
target_include_directories(04_texture_mipmap PUBLIC lib/stb)
set_target_properties(04_texture_mipmap PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(04_texture_mipmap PROPERTIES RESOURCE ${RESOURCES})
set_property(TARGET 04_texture_mipmap PROPERTY FOLDER ${EXAMPLES_FOLDER})

# 05 - cube

add_executable(05_cube 05_cube/main.cpp)
target_link_libraries(05_cube glfw skygfx)
set_property(TARGET 05_cube PROPERTY FOLDER ${EXAMPLES_FOLDER})

# 06 - textured cube

add_executable(06_textured_cube 06_textured_cube/main.cpp ${RESOURCES})
target_link_libraries(06_textured_cube glfw skygfx)
target_include_directories(06_textured_cube PUBLIC lib/stb)
set_target_properties(06_textured_cube PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(06_textured_cube PROPERTIES RESOURCE ${RESOURCES})
set_property(TARGET 06_textured_cube PROPERTY FOLDER ${EXAMPLES_FOLDER})

# 07 - light

add_executable(07_light 07_light/main.cpp ${RESOURCES})
target_link_libraries(07_light glfw skygfx)
target_include_directories(07_light PUBLIC lib/stb)
set_target_properties(07_light PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(07_light PROPERTIES RESOURCE ${RESOURCES})
set_property(TARGET 07_light PROPERTY FOLDER ${EXAMPLES_FOLDER})

# 08 - render target

add_executable(08_render_target 08_render_target/main.cpp)
target_link_libraries(08_render_target glfw skygfx)
set_property(TARGET 08_render_target PROPERTY FOLDER ${EXAMPLES_FOLDER})

# 09 - multiple vertex buffers

add_executable(09_multiple_vertex_buffers 09_multiple_vertex_buffers/main.cpp)
target_link_libraries(09_multiple_vertex_buffers glfw skygfx)
set_property(TARGET 09_multiple_vertex_buffers PROPERTY FOLDER ${EXAMPLES_FOLDER})

# 10 - instancing

add_executable(10_instancing 10_instancing/main.cpp)
target_link_libraries(10_instancing glfw skygfx)
set_property(TARGET 10_instancing PROPERTY FOLDER ${EXAMPLES_FOLDER})

# 11 - imgui

add_executable(11_imgui 11_imgui/main.cpp)
target_link_libraries(11_imgui glfw skygfx imgui)
set_property(TARGET 11_imgui PROPERTY FOLDER ${EXAMPLES_FOLDER})

# 12 - bloom

add_executable(12_bloom 12_bloom/main.cpp ${RESOURCES})
target_link_libraries(12_bloom glfw skygfx imgui)
target_include_directories(12_bloom PUBLIC lib/stb)
set_target_properties(12_bloom PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_target_properties(12_bloom PROPERTIES RESOURCE ${RESOURCES})
set_property(TARGET 12_bloom PROPERTY FOLDER ${EXAMPLES_FOLDER})

# 13 - raytraced triangle

add_executable(13_raytraced_triangle 13_raytraced_triangle/main.cpp ${RESOURCES})
target_link_libraries(13_raytraced_triangle glfw skygfx)
set_property(TARGET 13_raytraced_triangle PROPERTY FOLDER ${EXAMPLES_FOLDER})

# 14 - raytraced cube

add_executable(14_raytraced_cube 14_raytraced_cube/main.cpp ${RESOURCES})
target_link_libraries(14_raytraced_cube glfw skygfx imgui)
target_include_directories(14_raytraced_cube PUBLIC lib/stb)
set_target_properties(14_raytraced_cube PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_target_properties(14_raytraced_cube PROPERTIES RESOURCE ${RESOURCES})
set_property(TARGET 14_raytraced_cube PROPERTY FOLDER ${EXAMPLES_FOLDER})

# 15 - raytraced shadows

add_executable(15_raytraced_shadows 15_raytraced_shadows/main.cpp ${RESOURCES})
target_link_libraries(15_raytraced_shadows glfw skygfx imgui)
target_include_directories(15_raytraced_shadows PUBLIC lib/stb)
set_target_properties(15_raytraced_shadows PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_target_properties(15_raytraced_shadows PROPERTIES RESOURCE ${RESOURCES})
set_property(TARGET 15_raytraced_shadows PROPERTY FOLDER ${EXAMPLES_FOLDER})

# 16 - scratch api triangle

add_executable(16_scratch_api_triangle 16_scratch_api_triangle/main.cpp)
target_link_libraries(16_scratch_api_triangle glfw skygfx)
set_property(TARGET 16_scratch_api_triangle PROPERTY FOLDER ${EXAMPLES_FOLDER})
