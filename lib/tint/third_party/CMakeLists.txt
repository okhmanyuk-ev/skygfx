# Copyright 2020 The Tint Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

if (${TINT_BUILD_BENCHMARKS})
  set(BENCHMARK_ENABLE_TESTING FALSE CACHE BOOL FALSE FORCE)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/google_benchmark/src EXCLUDE_FROM_ALL)
endif()

if (${TINT_BUILD_TESTS} AND NOT TARGET gmock)
  set(gtest_force_shared_crt ON CACHE BOOL "Controls whether a shared run-time library should be used even when Google Test is built as static library" FORCE)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/googletest EXCLUDE_FROM_ALL)
endif()

if (NOT TARGET SPIRV-Headers)
    set(SPIRV_HEADERS_SKIP_EXAMPLES ON CACHE BOOL "" FORCE)
    set(SPIRV_HEADERS_SKIP_INSTALL ON CACHE BOOL "" FORCE)

    message(STATUS "Tint: using SPIRV-Headers at ${DAWN_SPIRV_HEADERS_DIR}")
    add_subdirectory(${TINT_SPIRV_HEADERS_DIR} "${CMAKE_CURRENT_BINARY_DIR}/spirv-headers")
endif()

# Needs to come before SPIR-V Tools
if ((${TINT_BUILD_SPIRV_TOOLS_FUZZER} OR ${TINT_BUILD_AST_FUZZER}) AND
    (NOT TARGET protobuf::libprotobuf OR NOT TARGET protobuf::protoc))
  set(protobuf_BUILD_TESTS OFF CACHE BOOL "Controls whether protobuf tests are built" FORCE)
  set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "Controls whether a protobuf static runtime is built" FORCE)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/protobuf/cmake)
endif()

if (NOT TARGET SPIRV-Tools)
    set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
    set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
    set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "" FORCE)

    if(${TINT_BUILD_SPV_READER} OR ${TINT_BUILD_SPV_WRITER})
        set(SPIRV_SKIP_TESTS ON CACHE BOOL "Controls whether SPIR-V tests are run" FORCE)
        set(SPIRV_WERROR OFF CACHE BOOL OFF FORCE)
        if (${TINT_BUILD_SPIRV_TOOLS_FUZZER})
            set(SPIRV_BUILD_FUZZER ON CACHE BOOL "Controls whether spirv-fuzz is built" FORCE)
        endif()
    endif()

    message(STATUS "Tint: using SPIRV-Tools at ${TINT_SPIRV_TOOLS_DIR}")
    add_subdirectory(${TINT_SPIRV_TOOLS_DIR} "${CMAKE_CURRENT_BINARY_DIR}/spirv-tools" EXCLUDE_FROM_ALL)
endif()

if(NOT TARGET glslang AND ${TINT_BUILD_GLSL_WRITER} AND ${TINT_BUILD_CMD_TOOLS})
    set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "" FORCE)
    add_subdirectory("${TINT_THIRD_PARTY_DIR}/vulkan-deps/glslang/src" "${CMAKE_CURRENT_BINARY_DIR}/glslang" EXCLUDE_FROM_ALL)
endif()

if (NOT TARGET libabsl)
    if (("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang") OR
        ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang"))
        add_compile_options(
            -Wno-array-parameter
            -Wno-deprecated-builtins
            -Wno-unknown-warning-option
        )
    endif()

    # Recommended setting for compability with future abseil releases.
    set(ABSL_PROPAGATE_CXX_STD ON)
    add_subdirectory("${TINT_THIRD_PARTY_DIR}/abseil-cpp" "${CMAKE_CURRENT_BINARY_DIR}/abseil")
endif()
