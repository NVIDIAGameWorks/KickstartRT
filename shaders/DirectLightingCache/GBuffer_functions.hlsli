/*
* Copyright (c) 2022 NVIDIA CORPORATION. All rights reserved
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/
// applying viewport offset.
uint2 ToSamplePos(uint2 idx)
{
	uint x = idx.x + CB.m_Viewport_TopLeftX;
	uint y = idx.y + CB.m_Viewport_TopLeftY;
	return uint2(x, y);
}

uint2 getCheckerboardCoordinates(uint2 pixelIndex, bool invertCheckerboard) {

	pixelIndex.x *= 2;

	if (invertCheckerboard == (pixelIndex.y & 1)) {
		pixelIndex.x += 1;
	}

	return pixelIndex;
}

#if defined(GBUFFER_DEPTH_TEXTURE_AVAILABLE)
float3 GetWorldPositionFromDepth(uint2 launchIndex, uint2 samplePos, out bool isValid)
{
	float4 depthInput = t_DepthTex[samplePos];
	float3 worldPos = depthInput.xyz;

	isValid = false;

	if ((DepthType)CB.m_depthType == DepthType::RGB_WorldSpace) {
		// depth tex holds world position.
		// nothing to do

		// use 4th channel to tell if pixel is valid or not
		isValid = (depthInput.a != 0);
	}
	else if ((DepthType)CB.m_depthType == DepthType::R_ClipSpace) {
		// depth tex hold clips space z.
		float2 fLaunchIndex = (float2)launchIndex;

		float clipZ = worldPos.x; // Rch holds clip depth.
		clipZ = ((CB.m_Viewport_MaxDepth - CB.m_Viewport_MinDepth) * clipZ) + CB.m_Viewport_MinDepth;
		float2 clipXY = ((fLaunchIndex.xy + 0.5) / float2(CB.m_Viewport_Width, CB.m_Viewport_Height)) * 2.0 - 1.0;

#if defined(GRAPHICS_API_D3D)
		clipXY.y = -clipXY.y;  // D3D always flip Y direction between viewport and NDC.
#elif  defined(GRAPHICS_API_VK)
#else
#error "GRAPHICS_API_XXX undefined."
#endif

		float4 clipPos = float4(clipXY, clipZ, 1.0);

		float4 viewPos = mul(clipPos, CB.m_clipToViewMatrix);
		viewPos.xyz /= viewPos.w;
		viewPos.w = 1.0;
		float4 wPos = mul(viewPos, CB.m_viewToWorldMatrix);
		worldPos.xyz = wPos.xyz / wPos.w;

		// In clip space, 0.0 and 1.0 are usually don't have valid pixels that hits BVH
		if (clipZ > 0.0 && clipZ < 1.0)
			isValid = true;
	}

	return worldPos;
}

#endif

#if defined(GBUFFER_NORMAL_TEXTURE_AVAILABLE)
float3 GetNormal(uint2 samplePos, out bool isValid)
{
	float4 normalTex = t_NormalTex.Load(int3(samplePos, 0)).xyzw;
	return GetWorldNormal(normalTex, CB.m_normalType, CB.m_normalNormalizationFactor, CB.m_normalChMask1, CB.m_normalChMask2, (float3x3)CB.m_normalToWorldMatrix, isValid);
}
#endif


