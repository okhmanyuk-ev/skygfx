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

add_executable(texture texture/main.cpp ${RESOURCES})
target_link_libraries(texture glfw sky-gfx)
target_include_directories(texture PUBLIC lib/stb)
set_target_properties(texture PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_target_properties(texture PROPERTIES RESOURCE ${RESOURCES})

# uniform_buffer

add_executable(uniform_buffer uniform/main.cpp ${RESOURCES})
target_link_libraries(uniform_buffer glfw sky-gfx)
target_include_directories(uniform_buffer PUBLIC lib/stb)
set_target_properties(uniform_buffer PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_target_properties(texture PROPERTIES RESOURCE ${RESOURCES})

# cube

add_executable(cube cube/main.cpp)
target_link_libraries(cube glfw sky-gfx)

# textured cube

add_executable(textured_cube textured_cube/main.cpp ${RESOURCES})
target_link_libraries(textured_cube glfw sky-gfx)
target_include_directories(textured_cube PUBLIC lib/stb)
set_target_properties(textured_cube PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_target_properties(texture PROPERTIES RESOURCE ${RESOURCES})

# light

add_executable(light light/main.cpp ${RESOURCES})
target_link_libraries(light glfw sky-gfx)
target_include_directories(light PUBLIC lib/stb)
set_target_properties(light PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_target_properties(texture PROPERTIES RESOURCE ${RESOURCES})

# render target

add_executable(render_target render_target/main.cpp)
target_link_libraries(render_target glfw sky-gfx)
