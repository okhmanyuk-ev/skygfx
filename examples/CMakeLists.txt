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

add_executable(triangle triangle/main.cpp)
target_link_libraries(triangle glfw skygfx)

# uniform_buffer

add_executable(uniform_buffer uniform/main.cpp)
target_link_libraries(uniform_buffer glfw skygfx)

# texture

add_executable(texture texture/main.cpp ${RESOURCES})
target_link_libraries(texture glfw skygfx)
target_include_directories(texture PUBLIC lib/stb)
set_target_properties(texture PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_target_properties(texture PROPERTIES RESOURCE ${RESOURCES})

# texture_mipmap

add_executable(texture_mipmap texture_mipmap/main.cpp ${RESOURCES})
target_link_libraries(texture_mipmap glfw skygfx)
target_include_directories(texture_mipmap PUBLIC lib/stb)
set_target_properties(texture_mipmap PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_target_properties(texture_mipmap PROPERTIES RESOURCE ${RESOURCES})

# cube

add_executable(cube cube/main.cpp)
target_link_libraries(cube glfw skygfx)

# textured cube

add_executable(textured_cube textured_cube/main.cpp ${RESOURCES})
target_link_libraries(textured_cube glfw skygfx)
target_include_directories(textured_cube PUBLIC lib/stb)
set_target_properties(textured_cube PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_target_properties(textured_cube PROPERTIES RESOURCE ${RESOURCES})

# light

add_executable(light light/main.cpp ${RESOURCES})
target_link_libraries(light glfw skygfx)
target_include_directories(light PUBLIC lib/stb)
set_target_properties(light PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_target_properties(light PROPERTIES RESOURCE ${RESOURCES})

# render target

add_executable(render_target render_target/main.cpp)
target_link_libraries(render_target glfw skygfx)
