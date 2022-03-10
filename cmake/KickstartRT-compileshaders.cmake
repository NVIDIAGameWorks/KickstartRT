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

# generates a build target that will compile shaders for a given config file
#
# usage: ${SDK_NAME}_compile_shaders(TARGET <generated build target name>
#                                    CONFIG <shader-config-file>
#                                    [DXIL <dxil-output-path>]
#                                    [SPIRV_DXC <spirv-output-path>])

function(${SDK_NAME}_compile_shaders)
    set(options "")
    set(oneValueArgs TARGET CONFIG FOLDER DXIL SPIRV_DXC RESOURCEFILE)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(params "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT params_TARGET)
        message(FATAL_ERROR "${SDK_NAME}_compile_shaders: TARGET argument missing")
    endif()
    if (NOT params_CONFIG)
        message(FATAL_ERROR "${SDK_NAME}_compile_shaders: CONFIG argument missing")
    endif()
    if (NOT params_RESOURCEFILE)
        message(FATAL_ERROR "${SDK_NAME}_compile_shaders: RESOURCEFILE argument missing")
    endif()

    # just add the source files to the project as documents, they are built by the script
    set_source_files_properties(${params_SOURCES} PROPERTIES VS_TOOL_OVERRIDE "None") 

    add_custom_target(${params_TARGET}
        DEPENDS ${SDK_NAME}_ShaderCompiler
        SOURCES ${params_SOURCES})

    foreach(include_dir ${${SDK_NAME}_SHADER_INCLUDE_DIR})
        set(SHADER_INCLUDE_PATHS ${SHADER_INCLUDE_PATHS} -I ${include_dir})
    endforeach() 

    foreach(shader_define ${${SDK_NAME}_SHADER_DEFINES})
        set(GLOBAL_SHADER_DEFINES ${GLOBAL_SHADER_DEFINES} -D ${shader_define})
    endforeach() 

    if (params_DXIL)
        set(CFLAGS_DXIL "$<IF:$<CONFIG:Debug>,-Zi -Qembed_debug,-Zi -Zss -Fd ${params_DXIL}_pdb/>")
    endif()
    if (params_SPIRV_DXC)
        set(CFLAGS_SPIRV "$<IF:$<CONFIG:Debug>,-Zi -Qembed_debug,-Qstrip_debug -Qstrip_reflect>")
    endif()

    if (params_DXIL)
        if (NOT KickstartRT_DXC_DXIL_EXECUTABLE)
            message(FATAL_ERROR "${SDK_NAME}_compile_shaders: DXC not found --- please set KickstartRT_DXC_DXIL_EXECUTABLE to the full path to the DXC binary")
        endif()

        add_custom_command(TARGET ${params_TARGET} PRE_BUILD
                          COMMAND ${SDK_NAME}_ShaderCompiler
                                   --infile ${params_CONFIG}
                                   --parallel
                                   --out ${params_DXIL}
                                   --platform dxil
                                   --cflags "${CFLAGS_DXIL}"
                                   ${SHADER_INCLUDE_PATHS}
                                   ${GLOBAL_SHADER_DEFINES}
                                   --compiler ${KickstartRT_DXC_DXIL_EXECUTABLE}
                                   --res ${params_RESOURCEFILE}_DXIL.rc)
    endif()

    if (params_SPIRV_DXC)
        if (NOT KickstartRT_DXC_SPIRV_EXECUTABLE)
            message(FATAL_ERROR "${SDK_NAME}_compile_shaders: DXC for SPIR-V not found --- please set KickstartRT_DXC_SPIRV_EXECUTABLE to the full path to the DXC binary")
        endif()

        add_custom_command(TARGET ${params_TARGET} PRE_BUILD
                          COMMAND ${SDK_NAME}_ShaderCompiler
                                   --infile ${params_CONFIG}
                                   --parallel
                                   --out ${params_SPIRV_DXC}
                                   --platform spirv
                                   --cflags "${CFLAGS_SPIRV}"
                                   ${SHADER_INCLUDE_PATHS}
                                   ${GLOBAL_SHADER_DEFINES}
                                   --compiler ${KickstartRT_DXC_SPIRV_EXECUTABLE}
                                   --res ${params_RESOURCEFILE}_SPIRV.rc)
    endif()

    if(params_FOLDER)
        set_target_properties(${params_TARGET} PROPERTIES FOLDER ${params_FOLDER})
    endif()
endfunction()

# Generates a build target that will compile shaders for a given config file for all enabled ${SDK_NAME} platforms.
#
# The shaders will be placed into subdirectories of ${OUTPUT_BASE}, with names compatible with
# the FindDirectoryWithShaderBin framework function.

function(${SDK_NAME}_compile_shaders_all_platforms)
    set(options "")
    set(oneValueArgs TARGET CONFIG FOLDER OUTPUT_BASE RESOURCEFILE)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(params "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT params_TARGET)
        message(FATAL_ERROR "${SDK_NAME}_compile_shaders_all_platforms: TARGET argument missing")
    endif()
    if (NOT params_CONFIG)
        message(FATAL_ERROR "${SDK_NAME}_compile_shaders_all_platforms: CONFIG argument missing")
    endif()
    if (NOT params_OUTPUT_BASE)
        message(FATAL_ERROR "${SDK_NAME}_compile_shaders_all_platforms: OUTPUT_BASE argument missing")
    endif()
    if (NOT params_RESOURCEFILE)
        message(FATAL_ERROR "${SDK_NAME}_compile_shaders_all_platforms: RESOURCEFILE argument missing")
    endif()

    list(APPEND args TARGET ${params_TARGET})
    list(APPEND args CONFIG ${params_CONFIG})
    list(APPEND args FOLDER ${params_FOLDER})
    if (KickstartRT_SDK_WITH_DX12)
        list(APPEND args DXIL ${params_OUTPUT_BASE}/dxil)
    endif()

    if (KickstartRT_SDK_WITH_VULKAN)
        list(APPEND args SPIRV_DXC ${params_OUTPUT_BASE}/spirv)
    endif()
    list(APPEND args RESOURCEFILE ${params_RESOURCEFILE})
    list(APPEND args SOURCES ${params_SOURCES})

    cmake_language(CALL ${SDK_NAME}_compile_shaders ${args})

#    cmake_language(CALL ${SDK_NAME}_compile_shaders
#                        TARGET ${params_TARGET}
#                        CONFIG ${params_CONFIG}
#                        FOLDER ${params_FOLDER}
#                        DXIL ${params_OUTPUT_BASE}/dxil
#                        SPIRV_DXC ${params_OUTPUT_BASE}/spirv
#                        ${args}
#                        RESOURCEFILE ${params_RESOURCEFILE}
#                        SOURCES ${params_SOURCES})

endfunction()
