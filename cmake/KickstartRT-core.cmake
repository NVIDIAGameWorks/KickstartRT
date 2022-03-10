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
if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    message(FATAL_ERROR "Require at least gcc-9.0 for C++17 support")
endif()

set (THIRD_PARTY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
set (EXTERNALS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/externals)

if (NOT KickstartRT_PIX_EVENT_RUNTIME_SDK_PATH STREQUAL "")
    file(GLOB THIRD_PARTY_PIX_INCLUDE
        "${KickstartRT_PIX_EVENT_RUNTIME_SDK_PATH}/include/WinPixEventRuntime/*"
    )
    find_library(PIX_LIB WinPixEventRuntime PATHS ${KickstartRT_PIX_EVENT_RUNTIME_SDK_PATH}/bin/x64 NO_DEFAULT_PATH)
    if (NOT EXISTS ${PIX_LIB})
        message(FATAL_ERROR "Can't find PIX runtime SDK '${KickstartRT_PIX_EVENT_RUNTIME_SDK_PATH}/bin/x64'")
    endif()
endif()

file(GLOB ${SDK_NAME}_core_src
    src/*.cpp
)

file(GLOB ${SDK_NAME}_core_include
    src/*.h
)

file(GLOB ${SDK_NAME}_core_graphics_api_src
    src/GraphicsAPI/*.cpp
)
file(GLOB ${SDK_NAME}_core_graphics_api_include
    src/GraphicsAPI/*.h
)

file(GLOB ${SDK_NAME}_core_common_src
    src/common/*.cpp
)
file(GLOB ${SDK_NAME}_core_common_include
    src/common/*.h
)
file(GLOB ${SDK_NAME}_core_interface
    include/*.h
)

file(GLOB THIRD_PARTY_NRD_INCLUDE
    "${THIRD_PARTY_ROOT}/nrd/include/*"
)

file(GLOB VULKAN_SDK_INCLUDE_FILES
    ${KickstartRT_VULKAN_SDK_INCLUDE_PATH}/vulkan/*
)

set(${SDK_NAME}_core_resources_DX12
    ${CMAKE_CURRENT_BINARY_DIR}/shaders/ShaderResource_DXIL.rc
    ${CMAKE_CURRENT_BINARY_DIR}/resources/BlueNoiseTexture.rc
)

set(${SDK_NAME}_core_resources_VULKAN
    ${CMAKE_CURRENT_BINARY_DIR}/shaders/ShaderResource_SPIRV.rc
    ${CMAKE_CURRENT_BINARY_DIR}/resources/BlueNoiseTexture.rc
)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/resources)

file(TOUCH ${${SDK_NAME}_core_resources_DX12})
file(TOUCH ${${SDK_NAME}_core_resources_VULKAN})

set (BN_R8_TexturePath ${CMAKE_CURRENT_SOURCE_DIR}/resources/BN_128x128x64_R8.bin)
configure_file(resources/_BlueNoiseTexture.rc ${CMAKE_CURRENT_BINARY_DIR}/resources/BlueNoiseTexture.rc)

if (NOT WIN32)
    # A proxy rc file to ensure the shaders are correctly linked into the application
    set (SHADER_PATH ${CMAKE_CURRENT_BINARY_DIR}/shaders/spirv)
    configure_file(shaders/_ShaderResource_SPIRV.rc ${CMAKE_CURRENT_BINARY_DIR}/shaders/ShaderResource_SPIRV.rc)
endif()

if (WIN32)
    set_source_files_properties(${${SDK_NAME}_core_resources_DX12} PROPERTIES LANGUAGE RC)
    set_source_files_properties(${${SDK_NAME}_core_resources_VULKAN} PROPERTIES LANGUAGE RC)
endif()

source_group("Header Files\\Common" FILES ${${SDK_NAME}_core_common_include})
source_group("Source Files\\Common" FILES ${${SDK_NAME}_core_common_src})
source_group("Header Files\\Interface" FILES ${${SDK_NAME}_core_interface})
source_group("Resource Files" FILES ${${SDK_NAME}_core_resources_DX12})
source_group("Resource Files" FILES ${${SDK_NAME}_core_resources_VULKAN})
source_group("Header Files\\GraphicsAPI" FILES ${${SDK_NAME}_core_graphics_api_include})
source_group("Source Files\\GraphicsAPI" FILES ${${SDK_NAME}_core_graphics_api_src})

source_group("Header Files\\Thirdparty\\PIX" FILES ${THIRD_PARTY_PIX_INCLUDE})

source_group("Header Files\\Thirdparty\\NRD" FILES ${THIRD_PARTY_NRD_INCLUDE})

source_group("Header Files\\VulkanSDK" FILES ${VULKAN_SDK_INCLUDE_FILES})

if (KickstartRT_SDK_WITH_DX12)
    add_library(${SDK_NAME}_core_DX12 SHARED 
        ${${SDK_NAME}_core_src}
        ${${SDK_NAME}_core_include}
        ${${SDK_NAME}_core_graphics_api_src}
        ${${SDK_NAME}_core_graphics_api_include}
        ${${SDK_NAME}_core_common_include}
        ${${SDK_NAME}_core_common_src}
        ${${SDK_NAME}_core_interface}
        ${THIRD_PARTY_PIX_INCLUDE}
        ${THIRD_PARTY_NRD_INCLUDE}
        ${${SDK_NAME}_core_resources_DX12}
    )
    set_target_properties(${SDK_NAME}_core_DX12 PROPERTIES PUBLIC_HEADER "${${SDK_NAME}_core_interface}")
    set_target_properties(${SDK_NAME}_core_DX12 PROPERTIES OUTPUT_NAME ${SDK_NAME}_D3D12)
    set_target_properties(${SDK_NAME}_core_DX12 PROPERTIES CXX_STANDARD 17)

    target_link_libraries(${SDK_NAME}_core_DX12 PRIVATE
        "d3d12.lib"
        "dxguid.lib"
    )

    target_include_directories(${SDK_NAME}_core_DX12 PRIVATE
        src
        include
    )

    if (KickstartRT_SDK_WITH_NRD)
        target_link_libraries(${SDK_NAME}_core_DX12 PRIVATE
            "NRD"
        )
        target_include_directories(${SDK_NAME}_core_DX12 PRIVATE
            "${THIRD_PARTY_ROOT}/nrd/include"
        )
    endif()

    if (NOT KickstartRT_PIX_EVENT_RUNTIME_SDK_PATH STREQUAL "")
        target_link_libraries(${SDK_NAME}_core_DX12 PRIVATE
            "${PIX_LIB}"
        )
        target_include_directories(${SDK_NAME}_core_DX12 PRIVATE
            "${KickstartRT_PIX_EVENT_RUNTIME_SDK_PATH}/include"
        )
        target_compile_definitions(${SDK_NAME}_core_DX12 PRIVATE USE_PIX)
    endif()

    target_compile_definitions(${SDK_NAME}_core_DX12 PRIVATE GRAPHICS_API_D3D12)
    
    if (KickstartRT_SDK_WITH_NRD)
        target_compile_definitions(${SDK_NAME}_core_DX12 PRIVATE KickstartRT_SDK_WITH_NRD=1)
    else()
        target_compile_definitions(${SDK_NAME}_core_DX12 PRIVATE KickstartRT_SDK_WITH_NRD=0)
    endif()

    target_compile_options(${SDK_NAME}_core_DX12 PRIVATE "$<$<CONFIG:Release>:/MT>")
    target_compile_options(${SDK_NAME}_core_DX12 PRIVATE "$<$<CONFIG:Debug>:/MTd>")
    target_compile_options(${SDK_NAME}_core_DX12 PRIVATE "$<$<CONFIG:Release>:/Zi>")
    target_link_options(${SDK_NAME}_core_DX12 PRIVATE "$<$<CONFIG:Release>:/DEBUG>")
    target_link_options(${SDK_NAME}_core_DX12 PRIVATE "$<$<CONFIG:Release>:/OPT:REF>")
    target_link_options(${SDK_NAME}_core_DX12 PRIVATE "$<$<CONFIG:Release>:/OPT:ICF>")

    # TODO: is this the right way to configure compile options?
    if (KickstartRT_SDK_WITH_NRD)
        target_compile_options("NRD" PRIVATE "$<$<CONFIG:Release>:/MT>")
        target_compile_options("NRD" PRIVATE "$<$<CONFIG:Debug>:/MTd>")
        target_compile_options("NRD" PRIVATE "$<$<CONFIG:Release>:/Zi>")
    endif()

    add_dependencies(${SDK_NAME}_core_DX12 ${SDK_NAME}_shaders)

    target_compile_options(${SDK_NAME}_core_DX12 PRIVATE
        -D KickstartRT_DECLSPEC=__declspec\(dllexport\)
    )
    target_compile_options(${SDK_NAME}_core_DX12 PRIVATE /W4)

    set_target_properties(${SDK_NAME}_core_DX12 PROPERTIES FOLDER ${SDK_NAME})
endif()


if (KickstartRT_SDK_WITH_VULKAN)
    set(VULKAN_RESOURCE_BINARIES ${${SDK_NAME}_core_resources_VULKAN})

    # Handle resource files for non Win32
    if (NOT WIN32)
        include(cmake/KickstartRT-binary-resource.cmake)

        set(RESOURCE_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR})
        set(RESOURCE_HEADER "${RESOURCE_INCLUDE_DIR}/${SDK_NAME}_binary_resource.h")
        set(RESOURCE_BINARIES "")

        process_resource_binaries("${${SDK_NAME}_core_resources_VULKAN}" ${RESOURCE_HEADER} RESOURCE_BINARIES)
        set(VULKAN_RESOURCE_BINARIES ${RESOURCE_BINARIES})
    endif()

    add_library(${SDK_NAME}_core_VULKAN SHARED 
        ${${SDK_NAME}_core_src}
        ${${SDK_NAME}_core_include}
        ${${SDK_NAME}_core_graphics_api_src}
        ${${SDK_NAME}_core_graphics_api_include}
        ${${SDK_NAME}_core_common_include}
        ${${SDK_NAME}_core_common_src}
        ${${SDK_NAME}_core_interface}
        "${VULKAN_RESOURCE_BINARIES}"
        ${THIRD_PARTY_NRD_INCLUDE}
    )

    if (NOT WIN32)
        target_include_directories(${SDK_NAME}_core_VULKAN PRIVATE ${RESOURCE_INCLUDE_DIR} )
    endif()

    set_target_properties(${SDK_NAME}_core_VULKAN PROPERTIES PUBLIC_HEADER "${${SDK_NAME}_core_interface}")
    set_target_properties(${SDK_NAME}_core_VULKAN PROPERTIES OUTPUT_NAME ${SDK_NAME}_VK)
    set_target_properties(${SDK_NAME}_core_VULKAN PROPERTIES CXX_STANDARD 17)

    target_link_libraries(${SDK_NAME}_core_VULKAN PRIVATE "${KickstartRT_VULKAN_SDK_LIB}")
    if (KickstartRT_SDK_WITH_NRD)
        target_link_libraries(${SDK_NAME}_core_VULKAN PRIVATE
            "NRD"
        )
    endif()

    # 'BEFORE' so that vulkan over-rides the system install on linux
    target_include_directories(${SDK_NAME}_core_VULKAN BEFORE PRIVATE
        src
        include
        "${KickstartRT_VULKAN_SDK_INCLUDE_PATH}/"
        "${THIRD_PARTY_ROOT}/nrd/include"
    )

    target_compile_definitions(${SDK_NAME}_core_VULKAN PRIVATE GRAPHICS_API_VK)
    if (KickstartRT_SDK_WITH_NRD)
        target_compile_definitions(${SDK_NAME}_core_VULKAN PRIVATE KickstartRT_SDK_WITH_NRD=1)
    else()
        target_compile_definitions(${SDK_NAME}_core_VULKAN PRIVATE KickstartRT_SDK_WITH_NRD=0)
    endif()
    if (WIN32)
        target_compile_options(${SDK_NAME}_core_VULKAN PRIVATE "$<$<CONFIG:Release>:/MT>")
        target_compile_options(${SDK_NAME}_core_VULKAN PRIVATE "$<$<CONFIG:Debug>:/MTd>")
        target_compile_options(${SDK_NAME}_core_VULKAN PRIVATE "$<$<CONFIG:Release>:/Zi>")

        target_link_options(${SDK_NAME}_core_VULKAN PRIVATE "$<$<CONFIG:Release>:/DEBUG>")
        target_link_options(${SDK_NAME}_core_VULKAN PRIVATE "$<$<CONFIG:Release>:/OPT:REF>")
        target_link_options(${SDK_NAME}_core_VULKAN PRIVATE "$<$<CONFIG:Release>:/OPT:ICF>")

        # TODO: is this the right way to configure compile options?
        if (KickstartRT_SDK_WITH_NRD)
            target_compile_options("NRD" PRIVATE "$<$<CONFIG:Release>:/MT>")
            target_compile_options("NRD" PRIVATE "$<$<CONFIG:Debug>:/MTd>")
            target_compile_options("NRD" PRIVATE "$<$<CONFIG:Release>:/Zi>")
        endif()

        target_compile_options(${SDK_NAME}_core_VULKAN PRIVATE
            -D KickstartRT_DECLSPEC=__declspec\(dllexport\)
        )
        target_compile_options(${SDK_NAME}_core_VULKAN PRIVATE /W4)
    else()
        target_compile_options(${SDK_NAME}_core_VULKAN PRIVATE 
            -Wall -Wextra 
            -Wno-unused-variable 
            -Wno-unused-parameter 
            -Wno-pessimizing-move 
            -Wno-redundant-move 
            -Wno-unused-function
            -Wno-strict-aliasing
        )
        target_compile_options(${SDK_NAME}_core_VULKAN PRIVATE 
            -DKickstartRT_DECLSPEC 
        )    
    endif()

    add_dependencies(${SDK_NAME}_core_VULKAN ${SDK_NAME}_shaders)

    set_target_properties(${SDK_NAME}_core_VULKAN PROPERTIES FOLDER ${SDK_NAME})
endif()



