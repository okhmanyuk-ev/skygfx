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

# triangle

add_executable(01_triangle triangle/main.cpp)
target_link_libraries(01_triangle glfw skygfx)

# uniform_buffer

add_executable(02_uniform_buffer uniform/main.cpp)
target_link_libraries(02_uniform_buffer glfw skygfx)

# texture

add_executable(03_texture texture/main.cpp ${RESOURCES})
target_link_libraries(03_texture glfw skygfx)
target_include_directories(03_texture PUBLIC lib/stb)
set_target_properties(03_texture PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_target_properties(03_texture PROPERTIES RESOURCE ${RESOURCES})

# texture_mipmap

add_executable(04_texture_mipmap texture_mipmap/main.cpp ${RESOURCES})
target_link_libraries(04_texture_mipmap glfw skygfx)
target_include_directories(04_texture_mipmap PUBLIC lib/stb)
set_target_properties(04_texture_mipmap PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_target_properties(04_texture_mipmap PROPERTIES RESOURCE ${RESOURCES})

# cube

add_executable(05_cube cube/main.cpp)
target_link_libraries(05_cube glfw skygfx)

# textured cube

add_executable(06_textured_cube textured_cube/main.cpp ${RESOURCES})
target_link_libraries(06_textured_cube glfw skygfx)
target_include_directories(06_textured_cube PUBLIC lib/stb)
set_target_properties(06_textured_cube PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_target_properties(06_textured_cube PROPERTIES RESOURCE ${RESOURCES})

# light

add_executable(07_light light/main.cpp ${RESOURCES})
target_link_libraries(07_light glfw skygfx)
target_include_directories(07_light PUBLIC lib/stb)
set_target_properties(07_light PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_target_properties(07_light PROPERTIES RESOURCE ${RESOURCES})

# render target

add_executable(08_render_target render_target/main.cpp)
target_link_libraries(08_render_target glfw skygfx)
