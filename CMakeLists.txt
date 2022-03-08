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
project(KickstartRT VERSION 0.9.0)

if (WIN32)
    option(KickstartRT_SDK_WITH_DX12 "Enable the DX12 version of SDK" ON)
    option(KickstartRT_SDK_WITH_Interop_DX11 "Enable the DX11 interop layer of SDK" ON)
	option(KickstartRT_SDK_WITH_NRD "Include NRD as part of the SDK. Supported for all backends." ON)
    set(KickstartRT_DXC_DXIL_CUSTOM_PATH "" CACHE STRING "custom/path/to/dxc/that/supports/DXIL")
    set(KickstartRT_DXC_SPIRV_CUSTOM_PATH "" CACHE STRING "custom/path/to/dxc/that/supports/SPIRV")
    set(KickstartRT_VULKAN_SDK_CUSTOM_PATH "" CACHE STRING "custom/path/to/vulkan/SDK")
    set(KickstartRT_PIX_EVENT_RUNTIME_SDK_PATH "" CACHE STRING "path/to/PIX/Event/Runtime/SDK")
else()
    option(KickstartRT_SDK_WITH_DX12 "Enable the DX12 version of SDK" OFF)
    option(KickstartRT_SDK_WITH_Interop_DX11 "Enable the DX11 interop layer of SDK" OFF)
	option(KickstartRT_SDK_WITH_NRD "Include NRD as part of the SDK. Supported for all backends." ON)
    set(KickstartRT_DXC_SPIRV_CUSTOM_PATH "" CACHE STRING "custom/path/to/dxc/that/supports/SPIRV")
    set(KickstartRT_VULKAN_SDK_CUSTOM_PATH "" CACHE STRING "custom/path/to/vulkan/SDK")
    set(KickstartRT_PIX_EVENT_RUNTIME_SDK_PATH "" CACHE STRING "path/to/PIX/Event/Runtime/SDK")
endif()

option(KickstartRT_SDK_WITH_VULKAN "Enable the Vulkan version of SDK" ON)

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

set(CMAKE_SUPPRESS_REGENERATION true)

set(SDK_NAME KickstartRT)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(CMakeDependentOption)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /MP")
endif()

string(APPEND CMAKE_CXX_FLAGS_DEBUG " -D_DEBUG")

set(CMAKE_CXX_FLAGS_DEBUGVK ${CMAKE_CXX_FLAGS_DEBUG})
set(CMAKE_C_FLAGS_DEBUGVK ${CMAKE_C_FLAGS_DEBUG})
set(CMAKE_EXE_LINKER_FLAGS_DEBUGVK ${CMAKE_EXE_LINKER_FLAGS_DEBUG})
set(CMAKE_SHARED_LINKER_FLAGS_DEBUGVK ${CMAKE_SHARED_LINKER_FLAGS_DEBUG})

set(CMAKE_CXX_FLAGS_RELEASEVK ${CMAKE_CXX_FLAGS_RELEASE})
set(CMAKE_C_FLAGS_RELEASEVK ${CMAKE_C_FLAGS_RELEASE})
set(CMAKE_EXE_LINKER_FLAGS_RELEASEVK ${CMAKE_EXE_LINKER_FLAGS_RELEASE})
set(CMAKE_SHARED_LINKER_FLAGS_RELEASEVK ${CMAKE_SHARED_LINKER_FLAGS_RELEASE})

# Set PIX Event runtime SDK path if the environment variable found.
if (KickstartRT_PIX_EVENT_RUNTIME_SDK_PATH STREQUAL "")
    if (DEFINED ENV{PIX_EVENT_RUNTIME_SDK})
        set(KickstartRT_PIX_EVENT_RUNTIME_SDK_PATH $ENV{PIX_EVENT_RUNTIME_SDK})
    endif()
endif()
if (NOT KickstartRT_PIX_EVENT_RUNTIME_SDK_PATH STREQUAL "")
    string(REPLACE "\\" "/" KickstartRT_PIX_EVENT_RUNTIME_SDK_PATH ${KickstartRT_PIX_EVENT_RUNTIME_SDK_PATH})
    get_directory_property(hasParent PARENT_DIRECTORY)
    if (hasParent)
        # Sample app need to copy PIX DLL to run it.
        set(KickstartRT_PIX_EVENT_RUNTIME_SDK_PATH ${KickstartRT_PIX_EVENT_RUNTIME_SDK_PATH} PARENT_SCOPE)
    endif()
endif()

# Find vulkan SDK path here

if (KickstartRT_SDK_WITH_VULKAN)
    if (WIN32) 
        if (KickstartRT_VULKAN_SDK_CUSTOM_PATH STREQUAL "")
            if (DEFINED ENV{VULKAN_SDK})
                set(KickstartRT_VULKAN_SDK $ENV{VULKAN_SDK})
            else()
                message(FATAL_ERROR "Please set KickstartRT_VULKAN_SDK_CUSTOM_PATH for a valid path to Vulkan SDK first.")
            endif()
        else()
            set(KickstartRT_VULKAN_SDK ${KickstartRT_VULKAN_SDK_CUSTOM_PATH})
        endif()
        if (EXISTS ${KickstartRT_VULKAN_SDK} AND IS_DIRECTORY ${KickstartRT_VULKAN_SDK})
        else()
            message(FATAL_ERROR "Can't find Vulkan SDK path. '${KickstartRT_VULKAN_SDK}'")
        endif()
    else()
        # Linux and others.
        if (KickstartRT_VULKAN_SDK_CUSTOM_PATH STREQUAL "")
            find_package(Vulkan REQUIRED) # This sets Vulkan_FOUUND, Vulkan_LIBRARY and Vulkan_INCLUDE_DIR
            # Set the SDK path to the parent dir of the include path.
            cmake_path(GET Vulkan_INCLUDE_DIRS PARENT_PATH KickstartRT_VULKAN_SDK)
        else()
            set(KickstartRT_VULKAN_SDK ${KickstartRT_VULKAN_SDK_CUSTOM_PATH})
        endif()
        if (EXISTS ${KickstartRT_VULKAN_SDK} AND IS_DIRECTORY ${KickstartRT_VULKAN_SDK})
        else()
            message(FATAL_ERROR "Can't find Vulkan SDK path. '${KickstartRT_VULKAN_SDK}'")
        endif()
    endif()

    unset(KickstartRT_VULKAN_SDK_INCLUDE_PATH)
    unset(KickstartRT_VULKAN_SDK_LIB)
    if (Vulkan_FOUND)
        # Found by find_package()
        set(KickstartRT_VULKAN_SDK_INCLUDE_PATH ${Vulkan_INCLUDE_DIRS})                     
        set(KickstartRT_VULKAN_SDK_LIB ${Vulkan_LIBRARIES})
        get_directory_property(hasParent PARENT_DIRECTORY)
        if(hasParent)
            # Put it in the parent scope so projects including this will see it
            set(KickstartRT_VULKAN_SDK_LIB ${Vulkan_LIBRARIES} PARENT_SCOPE)
        endif()
    else()
        set(KickstartRT_VULKAN_SDK_INCLUDE_PATH ${KickstartRT_VULKAN_SDK}/Include)
        if (NOT (EXISTS ${KickstartRT_VULKAN_SDK_INCLUDE_PATH} AND IS_DIRECTORY ${KickstartRT_VULKAN_SDK_INCLUDE_PATH}))
            message(FATAL_ERROR "Can't find Vulkan SDK Include path. '${KickstartRT_VULKAN_SDK_INCLUDE_PATH}'")
        endif()

        find_library(KickstartRT_VULKAN_SDK_LIB vulkan vulkan.1 vulkan-1 PATHS ${KickstartRT_VULKAN_SDK}/lib NO_DEFAULT_PATH)
        if (NOT EXISTS ${KickstartRT_VULKAN_SDK_LIB})
            message(FATAL_ERROR "Can't find Vulkan SDK Libs in '${KickstartRT_VULKAN_SDK}/lib'")
        endif()
    endif()

    # message(STATUS "KickstartRT_VULKAN_SDK_INCLUDE_PATH: ${KickstartRT_VULKAN_SDK_INCLUDE_PATH}")
    # message(STATUS "KickstartRT_VULKAN_SDK_LIB: ${KickstartRT_VULKAN_SDK_LIB}")
endif()

# Find dxc compiler here.

if (KickstartRT_SDK_WITH_DX12)
    # assume it's on WIN32.
    unset(KickstartRT_DXC_DXIL_EXECUTABLE CACHE)
    if (NOT KickstartRT_DXC_DXIL_CUSTOM_PATH STREQUAL "")
        # Find the exe under the custom path.
        # message("Using custom DXC path: '${KickstartRT_DXC_DXIL_CUSTOM_PATH}'")
        find_program(KickstartRT_DXC_DXIL_EXECUTABLE "${KickstartRT_DXC_DXIL_CUSTOM_PATH}/dxc")
        if (NOT KickstartRT_DXC_DXIL_EXECUTABLE)
            message(FATAL_ERROR "Can't find DXC (DXIL) in the custom path. 'KickstartRT_DXC_DXIL_CUSTOM_PATH'")
        endif()
    else ()
        # Find dxc from the installed windows SDK.
        if (DEFINED CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
            set (KickstartRT_WINDOWS_SDK_VERSION ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION})
        elseif (DEFINED ENV{WindowsSDKLibVersion})
            string (REGEX REPLACE "\\\\$" "" KickstartRT_WINDOWS_SDK_VERSION "$ENV{WindowsSDKLibVersion}")
        else()
            message(FATAL_ERROR "WindowsSDK is not installed. (CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION is not defined; WindowsSDKLibVersion is '$ENV{WindowsSDKLibVersion}')")
        endif()

        get_filename_component(KickstartRT_WINDOWS_SDK_ROOT
            "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)

        set(KickstartRT_WINDOWS_SDK_BIN "${KickstartRT_WINDOWS_SDK_ROOT}/bin/${KickstartRT_WINDOWS_SDK_VERSION}/x64")

        find_program(KickstartRT_DXC_DXIL_EXECUTABLE "${KickstartRT_WINDOWS_SDK_BIN}/dxc")
        if (NOT KickstartRT_DXC_DXIL_EXECUTABLE)
            message(FATAL_ERROR "Can't find DXC. '${KickstartRT_WINDOWS_SDK_BIN}/dxc'")
        endif()
    endif()
endif()

if (KickstartRT_SDK_WITH_VULKAN)
    unset(KickstartRT_DXC_SPIRV_EXECUTABLE CACHE)
    if (NOT KickstartRT_DXC_SPIRV_CUSTOM_PATH STREQUAL "")
        # Try to find a dxc under the custom path.
        # message("Using custom DXC(SPIRV) path: '${KickstartRT_DXC_SPIRV_CUSTOM_PATH}'")
        find_program(KickstartRT_DXC_SPIRV_EXECUTABLE "${KickstartRT_DXC_SPIRV_CUSTOM_PATH}/dxc")
        if (NOT KickstartRT_DXC_SPIRV_EXECUTABLE)
            message(FATAL_ERROR "Can't find DXC in the custom path. 'KickstartRT_DXC_SPIRV_CUSTOM_PATH'")
        endif()
    else()
        # Try to find a dxc in the Vulkan SDK first.
        find_program(KickstartRT_DXC_SPIRV_EXECUTABLE "${KickstartRT_VULKAN_SDK}/bin/dxc")
        if (NOT KickstartRT_DXC_SPIRV_EXECUTABLE)
            # Try to find dxc in the system path.
            message("Can't find DXC in the Vulkan SDK.. try to find it in the system path.")
            find_program(KickstartRT_DXC_SPIRV_EXECUTABLE "dxc")
            if (NOT KickstartRT_DXC_SPIRV_EXECUTABLE)
                message(FATAL_ERROR "Can't find DXC. Custom path can be specified using 'KickstartRT_VULKAN_SDK_CUSTOM_PATH' or 'KickstartRT_DXC_SPIRV_CUSTOM_PATH' parameter.")
            endif()
        endif()
    endif()
endif()

list(APPEND SHADER_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/shaders)

if (KickstartRT_SDK_WITH_NRD)
	set(NRD_STATIC_LIBRARY ON CACHE BOOL "")
	add_subdirectory(thirdparty/nrd)
	set_property(TARGET "NRD" PROPERTY FOLDER "thirdparty/nrd")
	set_property(TARGET "NrdShaders" PROPERTY FOLDER "thirdparty/nrd")

    list(APPEND SHADER_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/thirdparty/nrd/Include)
    list(APPEND SHADER_DEFINES KickstartRT_SDK_WITH_NRD=1)
else()
     # Shader includes must be available because the compile scans the source for "#include" to keep track of dirty state.
    list(APPEND SHADER_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/shaders/Denoising/NRD/Placeholder)
    list(APPEND SHADER_DEFINES KickstartRT_SDK_WITH_NRD=0)
endif()

set(${SDK_NAME}_SHADER_INCLUDE_DIR ${SHADER_INCLUDE_DIRS} CACHE INTERNAL "" FORCE)
set(${SDK_NAME}_SHADER_DEFINES ${SHADER_DEFINES} CACHE INTERNAL "" FORCE)

add_subdirectory(shaders)
add_subdirectory(thirdparty)
add_subdirectory(tools/ShaderCompiler)

include(cmake/${SDK_NAME}-core.cmake)

if (KickstartRT_SDK_WITH_VULKAN)
  if (WIN32)
    # to use local SDK folder in KickStart SDK.
    get_filename_component(VULKANSDK_LOCATION
                            "${CMAKE_CURRENT_SOURCE_DIR}/externals/VulkanSDK"
                            ABSOLUTE)
  else()
    get_filename_component(VULKANSDK_LOCATION
                            "$ENV{VULKAN_SDK}"
                            ABSOLUTE)                       
  endif()
endif()

set(CMAKE_INSTALL_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/package CACHE PATH "..." FORCE)

if (KickstartRT_SDK_WITH_Interop_DX11)
if (KickstartRT_SDK_WITH_DX12)
    add_subdirectory(interop_d3d11)
else()
message("D3D11 interop layer depends on D3D12 SDK. Please enable it.")
endif()
endif()

if (KickstartRT_SDK_WITH_DX12)
if (MSVC)
    install(
        FILES $<TARGET_PDB_FILE:${SDK_NAME}_core_DX12> DESTINATION "lib/$<CONFIG>" OPTIONAL
        )
endif()
install(
    TARGETS ${SDK_NAME}_core_DX12
    CONFIGURATIONS Debug
    ARCHIVE DESTINATION lib/Debug
    LIBRARY DESTINATION lib/Debug
    RUNTIME DESTINATION lib/Debug
    PUBLIC_HEADER DESTINATION include
    )
install(
        TARGETS ${SDK_NAME}_core_DX12
        CONFIGURATIONS Release
        ARCHIVE DESTINATION lib/Release
        LIBRARY DESTINATION lib/Release
        RUNTIME DESTINATION lib/Release
        PUBLIC_HEADER DESTINATION include
        )
endif()

if (KickstartRT_SDK_WITH_VULKAN)
if (MSVC)
    install(
        FILES $<TARGET_PDB_FILE:${SDK_NAME}_core_VULKAN> DESTINATION "lib/$<CONFIG>" OPTIONAL
        )
endif()
install(
        TARGETS ${SDK_NAME}_core_VULKAN
        CONFIGURATIONS Debug
        ARCHIVE DESTINATION lib/Debug
        LIBRARY DESTINATION lib/Debug
        RUNTIME DESTINATION lib/Debug
        PUBLIC_HEADER DESTINATION include
        )
install(
        TARGETS ${SDK_NAME}_core_VULKAN
        CONFIGURATIONS Release
        ARCHIVE DESTINATION lib/Release
        LIBRARY DESTINATION lib/Release
        RUNTIME DESTINATION lib/Release
        PUBLIC_HEADER DESTINATION include
        )
endif()

if (KickstartRT_SDK_WITH_Interop_DX11)
if (MSVC)
    install(
        FILES $<TARGET_PDB_FILE:${SDK_NAME}_Interop_D3D11> DESTINATION "lib/$<CONFIG>" OPTIONAL
        )
endif()
install(
        TARGETS ${SDK_NAME}_Interop_D3D11
        CONFIGURATIONS Debug
        ARCHIVE DESTINATION lib/Debug
        LIBRARY DESTINATION lib/Debug
        RUNTIME DESTINATION lib/Debug
        PUBLIC_HEADER DESTINATION include
        )
install(
        TARGETS ${SDK_NAME}_Interop_D3D11
        CONFIGURATIONS Release
        ARCHIVE DESTINATION lib/Release
        LIBRARY DESTINATION lib/Release
        RUNTIME DESTINATION lib/Release
        PUBLIC_HEADER DESTINATION include
        )
endif()

# if samples folder is exists add it to the solution.
# it won't be happend in the public package, just for internals.
if (WIN32)
    if (IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/kickstartrtx_internal_samples/")
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/kickstartrtx_internal_samples)
    endif()
endif()
