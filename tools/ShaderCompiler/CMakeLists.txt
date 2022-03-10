#
#  Copyright (c) 2022 NVIDIA CORPORATION. All rights reserved
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(SRC_FILES
    "ShaderCompiler.cpp"
    "Options.cpp"
    "Options.h"
)

set(COMMON_SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/CRC.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/CRC.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/ShaderBlobEntry.h"
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(${SDK_NAME}_ShaderCompiler "${SRC_FILES}" "${COMMON_SRC_FILES}")
target_include_directories(${SDK_NAME}_ShaderCompiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
target_include_directories(${SDK_NAME}_ShaderCompiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_link_libraries(${SDK_NAME}_ShaderCompiler cxxopts Threads::Threads)

if(MSVC)
	target_compile_definitions(${SDK_NAME}_ShaderCompiler PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

set_property(TARGET ${SDK_NAME}_ShaderCompiler PROPERTY FOLDER "Tools")
