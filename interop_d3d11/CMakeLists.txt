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
cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# --------------------------------------
# D3D11 Interop Project
# --------------------------------------

if (KickstartRT_SDK_WITH_DX12)

# Setup the project
project(${SDK_NAME}_Interop_D3D11)

set (KickstartRT_SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

file(GLOB ${SDK_NAME}_Interop_D3D11_SOURCE
    src/*.cpp
)

file(GLOB ${SDK_NAME}_Interop_D3D11_INCLUDE
    src/*.h
)

file(GLOB ${SDK_NAME}_core_interface
    ${KickstartRT_SDK_ROOT}/include/*.h
)

add_library(${SDK_NAME}_Interop_D3D11 SHARED 
    ${${SDK_NAME}_Interop_D3D11_SOURCE}
    ${${SDK_NAME}_Interop_D3D11_INCLUDE}
    ${${SDK_NAME}_Interop_D3D11_COMMON_SOURCE}
    ${${SDK_NAME}_Interop_D3D11_COMMON_INCLUDE}
    ${${SDK_NAME}_core_interface}
)
source_group("Header Files\\Interface" FILES ${${SDK_NAME}_core_interface})
source_group("Header Files\\Common" FILES ${${SDK_NAME}_Interop_D3D11_COMMON_INCLUDE})
source_group("Source Files\\Common" FILES ${${SDK_NAME}_Interop_D3D11_COMMON_SOURCE})

set_target_properties(${SDK_NAME}_Interop_D3D11 PROPERTIES PUBLIC_HEADER "${${SDK_NAME}_core_interface}")
set_target_properties(${SDK_NAME}_Interop_D3D11 PROPERTIES OUTPUT_NAME ${SDK_NAME}_Interop_D3D11)
set_target_properties(${SDK_NAME}_Interop_D3D11 PROPERTIES CXX_STANDARD 17)

target_link_libraries(${SDK_NAME}_Interop_D3D11 PRIVATE
    "d3d11.lib"
    "d3d12.lib"
    "dxguid.lib"
    ${SDK_NAME}_core_DX12
)


target_include_directories(${SDK_NAME}_Interop_D3D11 PRIVATE
    src
    include
    ${KickstartRT_SDK_ROOT}/include
    ${KickstartRT_SDK_ROOT}/common/src
)
target_compile_definitions(${SDK_NAME}_Interop_D3D11 PRIVATE GRAPHICS_API_D3D12)

target_compile_options(${SDK_NAME}_Interop_D3D11 PRIVATE "$<$<CONFIG:Release>:/MT>")
target_compile_options(${SDK_NAME}_Interop_D3D11 PRIVATE "$<$<CONFIG:Debug>:/MTd>")
target_compile_options(${SDK_NAME}_Interop_D3D11 PRIVATE "$<$<CONFIG:Release>:/Zi>")
target_link_options(${SDK_NAME}_Interop_D3D11 PRIVATE "$<$<CONFIG:Release>:/DEBUG>")
target_link_options(${SDK_NAME}_Interop_D3D11 PRIVATE "$<$<CONFIG:Release>:/OPT:REF>")
target_link_options(${SDK_NAME}_Interop_D3D11 PRIVATE "$<$<CONFIG:Release>:/OPT:ICF>")

add_dependencies(${SDK_NAME}_Interop_D3D11 ${SDK_NAME}_core_DX12)

target_compile_options(${SDK_NAME}_Interop_D3D11 PRIVATE
 -D KickstartRT_Interop_D3D11_DECLSPEC=__declspec\(dllexport\)
 )
target_compile_options(${SDK_NAME}_Interop_D3D11 PRIVATE /W4)

set_target_properties(${SDK_NAME}_Interop_D3D11 PROPERTIES FOLDER ${SDK_NAME})
endif()
