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

// It starts a new line by BlockSize_X(), instead of full width of screen.

#if defined(ENABLE_CTA_SWIZZLING)

uint2 LocalGroupThreadSize()
{
	return uint2(8, 16);
}

uint BlockSize_X()
{
	return 8;
}

uint2 GroupDimension()
{
	return uint2(CB.m_CTA_Swizzle_GroupDimension_X, CB.m_CTA_Swizzle_GroupDimension_Y);
}

uint PerfectBlockSize()
{
	return BlockSize_X() * GroupDimension().y;
}

uint NumPerfectBlocks()
{
	return GroupDimension().x / BlockSize_X();
}

uint BorderBlockSize_X()
{
	return GroupDimension().x - NumPerfectBlocks() * BlockSize_X();
}

// This function will return swizzled pixel position
uint2 CTASwizzle_GetPixelPosition(in uint2 _GroupID, in uint2 _GroupThreadID, in uint2 _DispatchThreadID)
{
	uint flattenGroupID = _GroupID.x + _GroupID.y * GroupDimension().x;

	uint blockID = flattenGroupID / PerfectBlockSize();

	uint blockSize_X = BlockSize_X();	// Perfect Block
	if (blockID == NumPerfectBlocks()) {
		blockSize_X = BorderBlockSize_X();	// Border Block
	}

	uint localGroupID = flattenGroupID - blockID * PerfectBlockSize();
	uint2 localGroupPos = uint2(localGroupID % blockSize_X, localGroupID / blockSize_X);

	uint2 groupOffset = (uint2(BlockSize_X(), 0) * blockID) + localGroupPos;

	uint2 pixelPos = groupOffset * LocalGroupThreadSize() + _GroupThreadID.xy;

	return pixelPos;
}

#else
// Disabled CTA swizzling.
uint2 CTASwizzle_GetPixelPosition(in uint2 _GroupID, in uint2 _GroupThreadID, in uint2 _DispatchThreadID)
{
	return _DispatchThreadID.xy;
}

#endif

