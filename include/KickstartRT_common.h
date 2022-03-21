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
#pragma once

#include <type_traits>
#include <stdint.h>

namespace KickstartRT {
	/** 
	* Most of the APIs in the SDK return this enum to tell if the process was successful or not.
	*/
	enum class Status : uint32_t {
		OK = 0,
		ERROR_INTERNAL,
		ERROR_INVALID_PARAM,
		ERROR_MEMORY_ALLOCATION,
		ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT,
		ERROR_FAILED_TO_INIT_TASK_WORKING_SET,
		ERROR_FAILED_TO_INIT_COMMAND_WORKING_SET,
		ERROR_FAILED_TO_INIT_RENDER_PASS,
		ERROR_FAILED_TO_INIT_FENCE,
		ERROR_FAILED_TO_WAIT_FOR_COMMAND_COMPLETION,
		ERROR_FAILED_TO_INVOKE_JOB,
		ERROR_FAILED_TO_WAIT_FOR_JOB,
		ERROR_INVALID_SIGNALING_STATE_DETECTED,
		ERROR_INVALID_PROCESSING_STAGE_TRANSITION,
		ERROR_INVALID_CALL_FOR_THE_CURRENT_PROCESSING_STAGE,
		ERROR_INVALID_GEOMETRY_HANDLE,
		ERROR_INVALID_GEOMETRY_INPUTS,
		ERROR_INVALID_INSTANCE_HANDLE,
	};


	/**
	* It contains some of Vectors and Matrices.
	* These are defined as input information to the SDK, not for the computations.
	* They have the same memory footprint as their corresponded DirectXMath classes.
	*/
	namespace Math
	{
		struct Float_2
		{
			float		f[2];
		};

		struct Float_3
		{
			float		f[3];
		};

		struct Float_4
		{
			float		f[4];
		};

		/**
		* A 3x4 row-major Matrix, but this is interpreted as transposed 4x3.
		* When it is converted from Float_4x4 matrix, it need to be transposed. (so it is effectively a column-major matrix).
		* This struct has the same structure of XMFLOAT3X4. 
		* Transforming a vector with this matrix is applied from the right side of a vector with transposed matrix :
		* (V) * (M)^T (So, it can be interpreted that the matrix is applied from the left side of a vector : (M) * (V))
		*/
		struct Float_3x4
		{
			struct M3x4
			{
				float _11, _12, _13, _14;
				float _21, _22, _23, _24;
				float _31, _32, _33, _34;
			};

			union
			{
				M3x4 m3x4;
				float m[3][4];
				float f[12];
			};

			void CopyTo(void* dst) const
			{
				*reinterpret_cast<Float_3x4*>(dst) = *this;
			}

			void CopyFrom(const void* src)
			{
				*this = *reinterpret_cast<const Float_3x4*>(src);
			}

			// Transposed.
			void CopyFrom4x4(const void* src)
			{
				const float* srcMat = reinterpret_cast<const float*>(src);

				m3x4._11 = srcMat[0]; m3x4._12 = srcMat[4]; m3x4._13 = srcMat[8]; m3x4._14 = srcMat[12];
				m3x4._21 = srcMat[1]; m3x4._22 = srcMat[5]; m3x4._23 = srcMat[9]; m3x4._24 = srcMat[13];
				m3x4._31 = srcMat[2]; m3x4._32 = srcMat[6]; m3x4._33 = srcMat[10]; m3x4._34 = srcMat[14];
			}

			static constexpr Float_3x4 Identity() {
				return {
					1.f, 0.f, 0.f, 0.f,
					0.f, 1.f, 0.f, 0.f,
					0.f, 0.f, 1.f, 0.f
				};
			};
		};

		struct Float_3x3;

		/**
		* A 4x4 row-major Matrix.
		* Transform Matrix is applied from the right side of a vector : (V) * (M)
		* This struct has the same structure of XMFLOAT4X4.
		*/
		struct Float_4x4
		{
			struct M4x4
			{
				float _11, _12, _13, _14;
				float _21, _22, _23, _24;
				float _31, _32, _33, _34;
				float _41, _42, _43, _44;
			};				union
			{
				M4x4 m4x4;
				float m[4][4];
				float f[16];
			};

			// Transposed.
			Float_4x4& operator=(const Float_3x4& src)
			{
				m4x4._11 = src.m3x4._11;	m4x4._12 = src.m3x4._21;	m4x4._13 = src.m3x4._31;	m4x4._14 = 0.f;
				m4x4._21 = src.m3x4._12;	m4x4._22 = src.m3x4._22;	m4x4._23 = src.m3x4._32;	m4x4._24 = 0.f;
				m4x4._31 = src.m3x4._13;	m4x4._32 = src.m3x4._23;	m4x4._33 = src.m3x4._33;	m4x4._34 = 0.f;
				m4x4._41 = src.m3x4._14;	m4x4._42 = src.m3x4._24;	m4x4._43 = src.m3x4._34;	m4x4._44 = 1.f;

				return *this;
			};

			Float_4x4 Transpose() const
			{
				Float_4x4 ret;
				ret.m4x4._11 = m4x4._11;	ret.m4x4._12 = m4x4._21;	ret.m4x4._13 = m4x4._31;	ret.m4x4._14 = m4x4._41;
				ret.m4x4._21 = m4x4._12;	ret.m4x4._22 = m4x4._22;	ret.m4x4._23 = m4x4._32;	ret.m4x4._24 = m4x4._42;
				ret.m4x4._31 = m4x4._13;	ret.m4x4._32 = m4x4._23;	ret.m4x4._33 = m4x4._33;	ret.m4x4._34 = m4x4._43;
				ret.m4x4._41 = m4x4._14;	ret.m4x4._42 = m4x4._24;	ret.m4x4._43 = m4x4._34;	ret.m4x4._44 = m4x4._44;

				return ret;
			};

			inline Float_4x4& operator=(const Float_3x3& src);

			static constexpr Float_4x4 Identity() {
				return {
					1.f, 0.f, 0.f, 0.f,
					0.f, 1.f, 0.f, 0.f,
					0.f, 0.f, 1.f, 0.f,
					0.f, 0.f, 0.f, 1.f,
				};
			};
		};

		/**
		* A 3x3 row-major Matrix.
		* Transform Matrix is applied from the right side of a vector : (V) * (M)
		* This struct has the same structure of XMFLOAT3X3
		*/
		struct Float_3x3
		{
			struct M3x3
			{
				float _11, _12, _13;
				float _21, _22, _23;
				float _31, _32, _33;
			};
			union
			{
				M3x3 m3x3;
				float m[3][3];
				float f[9];
			};

			Float_3x3& operator=(const Float_4x4& src)
			{
				m3x3._11 = src.m4x4._11;	m3x3._12 = src.m4x4._12;	m3x3._13 = src.m4x4._13;
				m3x3._21 = src.m4x4._21;	m3x3._22 = src.m4x4._22;	m3x3._23 = src.m4x4._23;
				m3x3._31 = src.m4x4._31;	m3x3._32 = src.m4x4._32;	m3x3._33 = src.m4x4._33;

				return *this;
			};

			Float_3x3 Transpose() const
			{
				Float_3x3 ret;
				ret.m3x3._11 = m3x3._11;	ret.m3x3._12 = m3x3._21;	ret.m3x3._13 = m3x3._31;
				ret.m3x3._21 = m3x3._12;	ret.m3x3._22 = m3x3._22;	ret.m3x3._23 = m3x3._32;
				ret.m3x3._31 = m3x3._13;	ret.m3x3._32 = m3x3._23;	ret.m3x3._33 = m3x3._33;

				return ret;
			};

			static constexpr Float_3x3 Identity() {
				return {
					1.f, 0.f, 0.f,
					0.f, 1.f, 0.f,
					0.f, 0.f, 1.f,
				};
			};
		};

		inline Float_4x4& Float_4x4::operator=(const Float_3x3& src)
		{
			m4x4._11 = src.m3x3._11;	m4x4._12 = src.m3x3._12;	m4x4._13 = src.m3x3._13;	m4x4._14 = 0.f;
			m4x4._21 = src.m3x3._21;	m4x4._22 = src.m3x3._22;	m4x4._23 = src.m3x3._23;	m4x4._24 = 0.f;
			m4x4._31 = src.m3x3._31;	m4x4._32 = src.m3x3._32;	m4x4._33 = src.m3x3._33;	m4x4._34 = 0.f;
			m4x4._41 = 0.f;		m4x4._42 = 0.f;		m4x4._43 = 0.f;		m4x4._44 = 1.f;

			return *this;
		};


		/**
		* Transform a vector with the given 4x4 matrix.
		*
		* @param [in] mat a 4x4 matrix to be used for the transformation.
		* @param [in] p a input vector to be transfromed.
		* @return a transformed vector
		*/
		inline Float_4 Transform(const Float_4x4& mat, const Float_4& p)
		{
			float fX = (mat.m4x4._11 * p.f[0]) + (mat.m4x4._21 * p.f[1]) + (mat.m4x4._31 * p.f[2]) + (mat.m4x4._41 * p.f[3]);
			float fY = (mat.m4x4._12 * p.f[0]) + (mat.m4x4._22 * p.f[1]) + (mat.m4x4._32 * p.f[2]) + (mat.m4x4._42 * p.f[3]);
			float fZ = (mat.m4x4._13 * p.f[0]) + (mat.m4x4._23 * p.f[1]) + (mat.m4x4._33 * p.f[2]) + (mat.m4x4._43 * p.f[3]);
			float fW = (mat.m4x4._14 * p.f[0]) + (mat.m4x4._24 * p.f[1]) + (mat.m4x4._34 * p.f[2]) + (mat.m4x4._44 * p.f[3]);

			Float_4 res = { fX, fY, fZ, fW };
			return res;
		};
	};

	/**
	* A struct to be used to give the information about the status of SDK's resource allocations.
	*/
	struct ResourceAllocations
	{
		enum class ResourceKind {
			e_VertexTemporay_SharedBlock = 0,
			e_VertexTemporay_SharedEntry,
			e_VertexPersistent_SharedBlock,
			e_VertexPersistent_SharedEntry,
			e_DirectLightingCache_SharedBlock,
			e_DirectLightingCache_SharedEntry,
			e_DirectLightingCacheTemp_SharedBlock,
			e_DirectLightingCacheTemp_SharedEntry,
			e_TLAS,
			e_Other,
			e_Counter_SharedBlock,
			e_Counter_SharedEntry,
			e_Readback_SharedBlock,
			e_Readback_SharedEntry,
			e_BLASSTemporary_SharedBlock,
			e_BLASSTemporary_SharedEntry,
			e_BLASSPermanent_SharedBlock,
			e_BLASSPermanent_SharedEntry,
			e_BLASScratchTemp_SharedBlock,
			e_BLASScratchTemp_SharedEntry,
			e_BLASScratchPerm_SharedBlock,
			e_BLASScratchPerm_SharedEntry,
			e_DenoiserTemp_SharedEntry,
			e_DenoiserPerm_SharedEntry,
			e_Num_Kinds,
		};

		size_t m_numResources[(size_t)ResourceKind::e_Num_Kinds];
		size_t m_totalRequestedBytes[(size_t)ResourceKind::e_Num_Kinds];
	};

	/**
	* SDK's version.
	*/
	struct Version
	{
		uint32_t Major = 0u;	
		uint32_t Minor = 9u;
		uint32_t Patch = 0u;

		inline bool operator==(const Version& src) const
		{
			return
				(Major == src.Major) &&
				(Minor == src.Minor) &&
				(Patch == src.Patch);
		};
	};
};
