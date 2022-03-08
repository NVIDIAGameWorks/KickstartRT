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
#include "Shared/Binding.hlsli"
#include "Shared/Shared.hlsli"

// resources for generating random/LD values.
//[[vk::binding(0, 2)]]
KS_VK_BINDING(0, 2)
Texture3D<uint> t_BlueNoiseTex : register(t0, space2);

static const uint3 BlueNoiseTexDim = uint3(128, 128, 64);

//[[vk::binding(1, 2)]]
KS_VK_BINDING(1, 2)
Texture2D<uint> t_nullTex : register(t1, space2);

#define Rng_Type_XOR_Shift 100
#define Rng_BlueNoiseTexture 101

struct RNGState
{
    uint2  launchIndex;
    uint2  launchDimensions;
    uint   frameIndex;
    uint   rngType;
    uint   seed;
    float  globalRandom_F;
};

// Jenkins's "one at a time" hash function
uint one_at_a_time(uint x) {
    x += x << 10;
    x ^= x >> 6;
    x += x << 3;
    x ^= x >> 11;
    x += x << 15;
    return x;
}

// Initialize RNG for given pixel, and frame number (Xorshift-based version)
uint initRNG(uint2 pixelCoords, uint2 resolution, uint frameNumber) {
    uint seed = dot(pixelCoords, uint2(1, resolution.x)) ^ one_at_a_time(frameNumber);
    return one_at_a_time(seed);
}

// Return random float in <0; 1) range (Xorshift-based version)
float rand(inout uint rngState) {
    return uintToFloat(xorshift(rngState));
}

float2 load_blue_noise2(uint2 LaunchIndex, uint frameIndex)
{
    float4 temp = t_BlueNoiseTex[int3(LaunchIndex.x % 256, LaunchIndex.y % 256, (frameIndex / 2) % 128)];

    if ((frameIndex % 2) == 0) return temp.rg;
    else return temp.ba;
}

// Generalized golden ratio to 2d.
float2 R2(uint index)
{
    // Generalized golden ratio to 2d.
    // Solution to x^3 = x + 1
    // AKA plastic constant.
    // from http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
    float g = 1.32471795724474602596f;
    return frac(float2(float(index) / g, float(index) / (g * g)));
}

RNGState initRNG(uint2 launchIndex, uint2 launchDimensions, uint frameIndex, uint rngType, float globalRandomF, uint globalRandomU)
{
    RNGState state;
    state.launchIndex = launchIndex;
    state.launchDimensions = launchDimensions;
    state.frameIndex = frameIndex;
    state.rngType = rngType;
    state.seed = globalRandomU;
    state.globalRandom_F = globalRandomF;

    if (rngType == Rng_Type_XOR_Shift) {
        state.seed = dot(state.launchIndex, uint2(1, state.launchDimensions.x)) ^ one_at_a_time(state.frameIndex) ^ one_at_a_time(state.seed);
        state.seed = one_at_a_time(state.seed);
    }
    else {
        // blue noise use this as global offset of texture coordinate value.
        state.seed %= BlueNoiseTexDim.x;
    }

    return state;
}

// Return random float in <0; 1) range
float rand(inout RNGState rngState)
{
    float u = 0;

    if (rngState.rngType == Rng_Type_XOR_Shift) {
        u = uintToFloat(xorshift(rngState.seed));
    }
    else if (rngState.rngType == Rng_BlueNoiseTexture) {
        uint2 seedOffset = uint2(R2(rngState.seed++) * float2(BlueNoiseTexDim.xy));
        uint2 samplePos = (rngState.launchIndex + seedOffset) % BlueNoiseTexDim.xy;
        uint r8 = t_BlueNoiseTex[int3(samplePos, rngState.frameIndex % BlueNoiseTexDim.z)]; // 0 ~ 255

#if 1
        // (0.0001 ~ 255.0001) ~ (0.9999 ~ 255.9999)
        u = ((float)r8 + rngState.globalRandom_F) / 256.0;
#else
        // (0.5 ~ 255.5)
        u = ((float)r8 + 0.5) / 256.0;
#endif
    }

    return u;
}

// Return a pair of random float in <0; 1) range
float2 randUV(inout RNGState rngState)
{
    float2 uv = float2(0, 0);

    if (rngState.rngType == Rng_Type_XOR_Shift) {
        uv.x = uintToFloat(xorshift(rngState.seed));
        uv.y = uintToFloat(xorshift(rngState.seed));
    }
    else if (rngState.rngType == Rng_BlueNoiseTexture) {
        uint2 seedOffset_u = uint2(R2(rngState.seed++) * float2(BlueNoiseTexDim.xy));
        uint2 seedOffset_v = uint2(R2(rngState.seed++) * float2(BlueNoiseTexDim.xy));
        uint2 samplePos_u = (rngState.launchIndex + seedOffset_u) % BlueNoiseTexDim.xy;
        uint2 samplePos_v = (rngState.launchIndex + seedOffset_v) % BlueNoiseTexDim.xy;
        uint r8_u = t_BlueNoiseTex[int3(samplePos_u, rngState.frameIndex % BlueNoiseTexDim.z)]; // 0 ~ 255
        uint r8_v = t_BlueNoiseTex[int3(samplePos_v, rngState.frameIndex % BlueNoiseTexDim.z)];

#if 1
        uv = (float2(r8_u, r8_v) + rngState.globalRandom_F) / 256.0;
#else
        uv = (float2(r8_u, r8_v) + 0.5) / 256.0;
#endif
    }

    return uv;
}

