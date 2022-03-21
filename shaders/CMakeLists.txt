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

include(../cmake/${SDK_NAME}-compileshaders.cmake)

file(GLOB ${SDK_NAME}_shared_group_shaders
	"Shared/*.h"
	"Shared/*.hlsl"
	"Shared/*.hlsli"
)
source_group("Shared" FILES ${${SDK_NAME}_shared_group_shaders})

file(GLOB ${SDK_NAME}_denoising_group_shaders
	"Denoising/*.h"
	"Denoising/*.hlsl"
	"Denoising/*.hlsli"
	"Denoising/NRD/*.h"
	"Denoising/NRD/*.hlsl"
	"Denoising/NRD/*.hlsli"
)
source_group("Denoising" FILES ${${SDK_NAME}_denoising_group_shaders})

file(GLOB ${SDK_NAME}_DirectLightingCache_group_shaders
	"DirectLightingCache/*.h"
	"DirectLightingCache/*.hlsl"
	"DirectLightingCache/*.hlsli"
)
source_group("DirectLightingCache" FILES ${${SDK_NAME}_DirectLightingCache_group_shaders})

cmake_language(CALL ${SDK_NAME}_compile_shaders_all_platforms
	EXCLUDE_FROM_ALL
	TARGET ${SDK_NAME}_shaders
	CONFIG ${CMAKE_CURRENT_LIST_DIR}/shaders.cfg
	FOLDER ${SDK_NAME}
	OUTPUT_BASE ${CMAKE_CURRENT_BINARY_DIR}
	RESOURCEFILE ${CMAKE_CURRENT_BINARY_DIR}/ShaderResource
	SOURCES shaders.cfg ${${SDK_NAME}_shared_group_shaders} ${${SDK_NAME}_denoising_group_shaders} ${${SDK_NAME}_DirectLightingCache_group_shaders}
)
