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

// This include file is included without any include guard, since it will be included for D3D12, VK and D3D11 layer respectively
// with different namespaces.

#if ! defined(KickstartRT_DECLSPEC_INL)
#error "NotDefined: KickstartRT_DECLSPEC_INL"
#endif
#if ! defined(KickstartRT_DECLSPEC)
#error "NotDefined: KickstartRT_DECLSPEC"
#endif

/**
* A namespace to control output messages from the SDK. 
*/
namespace Log
{
	enum class Severity : uint32_t
	{
		Info = 0,
		Warning = 1,
		Error = 2,
		Fatal = 3,
		None = 0xFFFF'FFFF
	};

	typedef void(STDCALL* Callback_t)(Severity, const wchar_t*, size_t length);

	/**
	* Setting a minimum serverity to log messages. 
	* @param [in] serverity SDK will not output messages that have less serverity than this.
	*/
	KickstartRT_DECLSPEC_INL Status SetMinSeverity(Severity severity);

	/**
	* Setting a callbackfunction to process messages from the SDK.
	* @param [in] func A callback function to be set for processing messages.
	*/
	KickstartRT_DECLSPEC_INL Status SetCallback(Callback_t func);

	/** 
	* To enable and disable default message procedure. Enabled by default.
	* Default message procedure outputs all messages to stderr and debug message stream.
	* @param [in] Setting false to disable default message procedure. 
	*/
	KickstartRT_DECLSPEC_INL Status SetDefaultMessageProc(bool state);
};


/**
* A handle to keep and identify denoising context.
* SDK holds correponded GPU resource and some sates while the handle is valid and live. ExecuteContext creates and destroyes it.
* This handle is requred to schedule various kind of denoising render tasks.
* 
* Do not create a new DenoisingContextHandle every frame as it contains temporal history of the denoised render target.
* Only re-create the context when resolution or other denoising config changes.
* 
* Dynamic resolution. If the resolution changes frame-to-frame set the maxWidth and maxHeight of the maximum allowed render resolution
* and configure the resolution in the denoising and tracing tasks instead.
*/
enum class DenoisingContextHandle : uint64_t { Null = 0 };

/**
* A structure to give SDK about information to create denoising context.
* SDK holds correponded GPU resource and some sates while the handle is valid and live.
*/
struct DenoisingContextInput {

	/** SignalType
	 * This denotes the signal that the denoiser should be configured to denoise.
	 * Different signals may require a different number and format of internal buffers.
	 * Specular				- Denoises output from TraceSpecularTask
	 * Diffuse				- Denoises output from TraceDiffuseTask
	 * SpecularAndDiffuse	- Denoises output from TraceSpecularTask & TraceDiffuseTask
	 * DiffuseOcclusion		- Denoises output from TraceAmbientOcclusionTask
	 * Shadow				- Denoises output from TraceShadowTask.
	 * MultiShadow			- Denoises output from TraceMultiShadowTask.
	*/
	enum class SignalType : uint32_t {
		Specular = 0,
		Diffuse = 1,
		SpecularAndDiffuse = 2,
		DiffuseOcclusion = 3,
		Shadow = 4,	
		MultiShadow = 5,
	};

	/** DenoisingMethod
	 * The denoising procedure to use. The methods support different signal types and achive slightly different quality and runtime performance.
	 * Read more at https://github.com/NVIDIAGameWorks/RayTracingDenoiser
	 * NRD_Reblur   - Supported signal types: Specular, Diffuse, SpecularAndDiffuse, DiffuseOcclusion
	 * NRD_Relax    - Supported signal types: Specular, Diffuse, SpecularAndDiffuse, DiffuseOcclusion
	 * NRD_Sigma    - Supported signal types: Shadow, MultiShadow
	 */
	enum class DenoisingMethod : uint32_t {
		NRD_Reblur = 0,
		NRD_Relax = 1,
		NRD_Sigma = 2,
	};

	uint32_t			maxWidth = 0;
	uint32_t			maxHeight = 0;
	DenoisingMethod		denoisingMethod = DenoisingMethod::NRD_Relax;
	SignalType			signalType = SignalType::Specular;
};

/**
* This is a namespace for all render tasks.
* A render task describes a high-level sequence of commands scheduled on a
* provided command list. It can be thought of as a more complex form of a regular
* D3D/VK command list draw or dispatch.
*
* Multiple render tasks can be scheduled at once as a sequence, if so the SDK
* will analyze the resources and (if needed) perform the nessesary resources transitions
* when a resource is used in multiple tasks.
* The typical case is a resorce being used as output from a raytracing task, but input to denoising task.
* In this case the SDK will perform a UAV->SRV transition after the raytracing task.
*/
namespace RenderTask {
	enum class DepthType : uint32_t {
		RGB_WorldSpace = 0,	// RGBch represents a world position
		R_ClipSpace = 1,	// Rch represents depth value in viewport transformed clip space.
	};

	enum class MotionType : uint32_t {
		RGB_WorldSpace = 0,	// RGBch represents a world position motion
		RG_ViewSpace = 1,	// RGch represents screen space motion.
	};

	enum class NormalType : uint32_t {
		RGB_Vector = 0,				// RGBch represents a normal vector in XYZ.
		RGB_NormalizedVector = 1,	// RGBch represents normalized normal vector, (xyz) = (rgb) * 2.0 - 1.0.
		RG_Octahedron = 2,			// RGch represents octahedron encoded noamal vector (xyz) = ocd_decode(rg)
		BA_Octahedron = 3,			// BAch represents octahedron encoded noamal vector (xyz) = ocd_decode(ba)
		RG_NormalizedOctahedron = 4,// RGch represents normalized octahedron encoded noamal vector (xyz) = ocd_decode((rg) * 2.0 - 1.0)
		BA_NormalizedOctahedron = 5,// BAch represents normalized octahedron encoded noamal vector (xyz) = ocd_decode((ba) * 2.0 - 1.0)
	};

	enum class EnvMapType : uint32_t {
		Latitude_Longitude = 0,
	};

	struct Viewport {
		// tex_X = (0 ~ Viewport.Width-1) + TopLeftX
		// tex_Y = (0 ~ Viewport.Height-1) + TopLeftY
		// clip_Z = (depth - MinDepth) / (MaxDepth - MinDepth)
		uint32_t	topLeftX = 0u;
		uint32_t	topLeftY = 0u;
		uint32_t	width = 0u;
		uint32_t	height = 0u;
		float		minDepth = 0.f;
		float		maxDepth = 1.f;
	};

	// This is a parameter set used to offset reflection rays when tracing rays.
	struct RayOffset {
		enum class Type {
			e_Disabled = 0,
			e_WorldPosition,	// offset applied based on the magnitude of world position value.
			e_CamDistance,		// offset applied based on the distance from the camera.
		} type = Type::e_Disabled;
		struct ParametersForWorldPosition {
			float	threshold = 1.f / 32.f; // threshold to switch offsetting algorithm. Small value uses floating point offset, large value uses integer offset.
			float	floatScale = 1.f / 65536.f; // scaling factor for normal vector before adding to position value.
			float	intScale = 256.f; // after normal vector is scaled this factor, it is converted to integer value to make an offset in mantissa of position value.
		} worldPosition;
		struct ParametersForCamDistance {
			float	constant = 0.00174f;
			float	linear = -0.0001547f;
			float	quadratic = 0.0000996f;
		} camDistance;
	};

	// Enable to render in half resolution using the checkerboard pattern. 
	// Normal and inverted setting refers to whether odd or even frame is being rendered, and alternates between white and black checkers.
	enum class HalfResolutionMode : uint32_t {
		OFF = 0,
		CHECKERBOARD = 1,
		CHECKERBOARD_INVERTED = 2
	};

	// A type of diffuse BRDF that will be used by the diffuse reflection (GI) pass.
	//  - Lambertian type is a simple Lambertian diffuse BRDF.
	//  - NormalizedDisney type is a diffuse BRDF sensitive to roughness, designed to be energy 
	//    conserving when combined with specular BRDF. I.e. after adding diffuse and specular 
	//    reflections together, this BRDF ensures result will never be more than 1
	enum class DiffuseBRDFType : uint32_t {
		Lambertian = 0,
		NormalizedDisney = 1
	};

	// defined in API dependent part.
	//struct ShaderResourceTex;
	//struct UnorderedAccessTex

	/*=========================================================================
	Structs/Params for composition
	===========================================================================*/
	struct DepthInput {
		DepthType			type = DepthType::RGB_WorldSpace;

		// Required
		ShaderResourceTex	tex;
	};

	struct NormalInput {
		NormalType			type = NormalType::RGB_Vector;

		// a rotation matrix for normal 
		Math::Float_3x3		normalToWorldMatrix = Math::Float_3x3::Identity();

		// Required
		ShaderResourceTex	tex;
	};

	struct RoughnessInput {
		// Surface roughness gobally being applied.
		float				globalRoughness = 0.3f;

		// Parameters for remapping roughness
		// Formula is: new_roughness = clamp(roughness * roughnessMultiplier + minRoughness, minRoughness, maxRoughness)
		// For smoothly limiting roughness, the recommended values are: min = 0.1, max = 0.4, multiplier = 0.5
		float				roughnessMultiplier = 1.0f;
		float				minRoughness = 0.0f;
		float				maxRoughness = 1.0f;

		// Roughness value is calculated as the inner product of this value and the RGBA texture.
		Math::Float_4		roughnessMask = { 0.f, 0.f, 0.f, 1.f };

		// Optional. Roughness texture is treated as a RGBA texture in shader code, and calculate a scalar value as the inner product with roughnessMask.
		ShaderResourceTex	tex;
	};

	struct SpecularInput {
		/**
		* Global metalness atcs as a scalar factor for the specular value, so just 1.0 is fine in most cases.
		* This should be changed only for debugging purpose when the specular texture is not set.
		*/
		float globalMetalness = 1.f;

		/**
		* Specular texture is treated as a RGB texture in the shader codes.
		* This is optional. If you don't set the texture, specular is set to RGB(1.0, 1.0, 1.0) in the shader code.
		* It must be set when enabling TraceSpecularTask::demodulateSpecular.
		*/
		ShaderResourceTex	tex;
	};

	struct InputMaskInput {
		// Input mask can be used to select which pixels on the screen will receive kickstart lighting. 
		// Use this to prevent rays from being traced on e.g. the GUI.
		ShaderResourceTex	tex;
	};

	struct MotionInput {
		MotionType			type = MotionType::RG_ViewSpace;

		Math::Float_2		scale = { 1.f, 1.f };

		ShaderResourceTex	tex;
	};

	struct EnvironmentMapInput {
		EnvMapType			type = EnvMapType::Latitude_Longitude;

		// A rotation matrix for transforming a direction from world space to environmental map local space. 
		Math::Float_3x3		worldToEnvMapMatrix = Math::Float_3x3::Identity();

		// This will be multiplied to a sampled value from the env map. 
		float				envMapIntensity = 1.0f;

		// Required.
		ShaderResourceTex	tex;
	};

	struct LightInfo {
		enum class Type {
			Directional,
			Spot,
			Point,
			Undefined,
		};

		union {
			struct {
				float 			intensity;
				float 			angularExtent;
				Math::Float_3 	dir;
			} dir;

			struct {
				float			intensity;		// Luminous intensity of the light (lm/sr) in its primary direction; multiplied by `color`.
				float			radius;			// Radius of the light sphere, in world units.
				float			range;			// Range of influence for the light. 0 means infinite range.
				float			apexAngle;		// Apex angle of the full-bright cone, in degrees; constant intensity inside the inner cone, smooth falloff between inside and outside.
				Math::Float_3 	dir;			// D
				Math::Float_3 	pos;
			} spot;

			struct {
				float			intensity;	// Luminous intensity of the light (lm/sr) in its primary direction; multiplied by `color`.
				float			radius;		// Radius of the light sphere, in world units.
				float			range;		// Range of influence for the light. 0 means infinite range.
				Math::Float_3 	pos;
			} point;
		};

		Type type = Type::Undefined;
	};

	struct DebugParameters {
		enum class DebugOutputType : uint32_t {
			Default = 0,
			Debug_DirectLightingCache_PrimaryRays = 100,
			Debug_RandomTileColor_PrimaryRays = 101,
			Debug_RandomMeshColor_PrimaryRays = 102,
			Debug_HitT_PrimaryRays = 103,
			Debug_Barycentrics_PrimaryRays = 104,
		};
		enum class RandomNumberGenerator : uint32_t
		{
			Default = 0,
			XORShift = 100,
			BlueNoiseTexture = 101,
		};

		// use the fllowing manual frame index for debugging if this parameter is non-zero.
		uint32_t		useFrameIndex = 0;
		// frame index which will be used random seed in shader codes.
		uint32_t		frameIndex = 0;
		// Instead of rendering reflection or GI, render a debug view for checking direct lighting cache and/or BVH.
		DebugOutputType			debugOutputType = DebugOutputType::Default;
		// Non-zero means specifying random vector generator for IQ comparing.
		RandomNumberGenerator	randomNumberGenerator = RandomNumberGenerator::Default;
	};

	/**
	* This is the base class of all render tasks.
	*/
	struct Task {
		enum class Type : uint32_t {
			Unknown = 0,

			DirectLightInjection,

			TraceSpecular,
			TraceDiffuse,
			TraceAmbientOcclusion,
			TraceShadow,
			TraceMultiShadow,

			DenoiseSpecular,
			DenoiseDiffuse,
			DenoiseSpecularAndDiffuse,
			DenoiseDiffuseOcclusion,
			DenoiseShadow,
			DenoiseMultiShadow,
		};

		const Type	type;
	protected:
		Task(Type typ) : type(typ) {};
	};

	/**
	* This task is to peform the light injection in to the world space and store the light information on the surface elements, "Warped Barycentric Storage" or "Mesh Color".
	* The typical usecase is to run this before all tracing tasks to perticipate the current screen space direct lighting buffer to the world space.
	*/
	struct DirectLightingInjectionTask : public Task {
		DirectLightingInjectionTask() : Task(Task::Type::DirectLightInjection) {};

		Viewport			viewport;

		//< SDK accumulates direct lighting information into allocated tiles on the surfaces. 
		//< Longer average window will converge values slowly but more stable than shorter average window. 
		float				averageWindow = 200.f;

		Math::Float_4x4		clipToViewMatrix = Math::Float_4x4::Identity();	// (Pos_View) = (Pos_CliP) * (M)
		Math::Float_4x4		viewToWorldMatrix = Math::Float_4x4::Identity();	// (Pos_World) = (Pos_View) * (M), in other words, (Cam Pos) = (0,0,0,1) * (M)
		// Switch between DXR1.1 inline raytracing via CS, or DXR1.0 tracing from RayGen shaders.
		bool				useInlineRT = false;

		DepthInput			depth;
		ShaderResourceTex	directLighting;	// RGBch must have direct lighting result.
	};

	/*=========================================================================
	Trace Task
	===========================================================================*/

	struct TraceTaskCommon {
		DepthInput			depth;
		NormalInput			normal;
		// Optional.
		InputMaskInput		inputMask;
		RoughnessInput		roughness;
		SpecularInput		specular;
		// (Experimental, Optional)
		ShaderResourceTex	directLighting;
		EnvironmentMapInput envMap;

		// Will perform bilinear sampling if the surfel mode of the mesh supports it. MeshColors is the only mode where it's supported.
		bool				enableBilinearSampling	= true;

		Viewport			viewport;
		// Enable to render in half resolution using the checkerboard pattern. 
		// Normal and inverted setting refers to whether odd or even frame is being rendered, and alternates between white and black checkers.
		HalfResolutionMode	halfResolutionMode = HalfResolutionMode::OFF;

		RayOffset			rayOffset;

		Math::Float_4x4		viewToClipMatrix = Math::Float_4x4::Identity();		// (Pos_View)	= (Pos_CliP) * (M)
		Math::Float_4x4		clipToViewMatrix = Math::Float_4x4::Identity();		// (Pos_View)	= (Pos_CliP) * (M)
		Math::Float_4x4		viewToWorldMatrix = Math::Float_4x4::Identity();		// (Pos_World)	= (Pos_View) * (M)
		Math::Float_4x4		worldToViewMatrix = Math::Float_4x4::Identity();		// (Pos_View)	= (Pos_World) * (M)
		// Switch between DXR1.1 inline raytracing via CS, or DXR1.0 tracing from RayGen shaders.
		bool				useInlineRT = false;
	};

	/**
	* This is to sample specular reflections comming from the world space's direct lighting cache.
	* The rays cast from the G-buffer's surces along the depth and normal buffer. Ray profile is decided along the surface's roughness.
	* Rendered outputs will be noisy if the SDK traces rays for rough surfaces. Those are expected to input to the subsequent denoising task.
	*/
	struct TraceSpecularTask : public Task {
		TraceSpecularTask() : Task(Type::TraceSpecular) {};

		TraceTaskCommon		common;

		// HalfResolutionMode must be disabled when demodulateSpecular is enabled
		bool				demodulateSpecular = false;

		// Primary output buffer (Required)
		// May be used as input to denoiser pass. 
		// Reflection				- RGBA16f+, RGB-Radiance, A-HitT
		UnorderedAccessTex	out;
		// Secondary output buffer, when additional outputs are requested or required for consumption by subsequent denoiser pass
		// RGB demodulated specular	- RGB16f+, (Only used if demodulateSpecular=true)
		UnorderedAccessTex	outAux;

		// SDK users don't need to set these parameters.
		DebugParameters	debugParameters;
	};

	/**
	* This is similar to the TraceSpecularTask.
	* The only difference is that this task traces diffues reflection rays instead of specular reflections. The rays are distributed over hemisphere of the surface normals.
	* Rendered outputs will be noisy and are expected to input to the subsequent denoising task.
	*/
	struct TraceDiffuseTask : public Task {
		TraceDiffuseTask() : Task(Type::TraceDiffuse) {};

		TraceTaskCommon		common;

		// Select a diffuse BRDF type used for GI pass
		DiffuseBRDFType		diffuseBRDFType = DiffuseBRDFType::Lambertian;

		// Primary output buffer (Required)
		// May be used as input to denoiser pass. 
		// Reflection				- RGBA16f+, RGB-Radiance, A-HitT
		UnorderedAccessTex	out;

		// SDK users don't need to set these parameters.
		DebugParameters		debugParameters;
	};

	/**
	* This is to render ray traced ambient occlusion.
	* The ray profile is same as the TraceDiffuseTask, but this doens't sample direct lighting cache. It only checks intersection against BVH.
	* So, this task can be run before any lighting tasks on G-buffer, and possible to calculate AO term before shading surfaces.
	* Rendered outputs will be noisy and are expected to input to the subsequent denoising task.
	*/
	struct TraceAmbientOcclusionTask : public Task {
		TraceAmbientOcclusionTask() : Task(Type::TraceAmbientOcclusion) {};

		TraceTaskCommon		common;

		// Radius of ray traced ambient occlusion
		float				aoRadius = 1.0f;

		// Primary output buffer (Required)
		// May be used as input to denoiser pass. 
		// - RGBA16f+, RGB-Radiance, A-HitT
		UnorderedAccessTex	out;

		// SDK users don't need to set these parameters.
		DebugParameters		debugParameters;
	};

	/**
	* This is to render ray traced shadow.
	* The ray profile will be decided along the provided LightInfo in this structure. 
	* The rays only checks intersection the the BVH and doesn't sample direct lighting cache.
	* So, like as AO task, this also possible to run before any lighting tasks on G-buffer.
	* Rendered outputs will be noisy if the light has surface area. In such case, the output of this task is expected to input to the subsequent denoising task.
	*/
	struct TraceShadowTask : public Task {
		TraceShadowTask() : Task(Type::TraceShadow) {};

		TraceTaskCommon		common;

		LightInfo			lightInfo;

		// Enabling usually results in more efficient shadow tracing. 
		// While it may resut in less accurate results for the denoiser that may require an accurate and stable hit distance
		bool				enableFirstHitAndEndSearch = true;

		// Primary output buffer (Required)
		// May be used as input to denoiser pass. 
		// - RGBA16f+, RGB-Radiance, A-HitT
		UnorderedAccessTex	out;

		// SDK users don't need to set these parameters.
		DebugParameters		debugParameters;
	};

	/**
	* This is to render ray traced shadow.
	* In this task, multiple light sources can be set in the structure and the shader iterates over all light sources and casts shadow rays to each light source.
	* So, the ray tracing costs increases along the number of light sources. 
	* Rendered outputs will be noisy if the light has surface area. In such case, the output of this task is expected to input to the subsequent denoising task.
	*/
	struct TraceMultiShadowTask : public Task {
		TraceMultiShadowTask() : Task(Type::TraceMultiShadow) {};

		TraceTaskCommon		common;

		static constexpr uint32_t kMaxLightNum = 32u;

		LightInfo			lightInfos[kMaxLightNum];
		uint32_t			numLights = 0;

		// Enabling usually results in more efficient shadow tracing. 
		// While it may resut in less accurate results for the denoiser that may require an accurate and stable hit distance
		bool				enableFirstHitAndEndSearch = true;

		// Primary output buffer (Required)
		// May be used as input to denoiser pass. 
		// - RG16f+,	Opaque NRD-specific data format
		UnorderedAccessTex	out0;
		// - RGBA8+, Opaque NRD-specific data format, required for NRD
		UnorderedAccessTex	out1;

		// SDK users don't need to set these parameters.
		DebugParameters		debugParameters;
	};

	/*=========================================================================
	Denoising Task
	===========================================================================*/

	struct DenoisingTaskCommon {

		/** enum Mode
		 * Continue (Default) - Use this mode during normal execution to keep accumulating history data.
		 * DiscardHistory - May be used during camera cuts that otherwise produce ghosting or accumulation lag in the history buffer.
		 */
		enum class Mode : uint32_t {
			Continue,
			DiscardHistory
		};

		Mode					mode = Mode::Continue;

		/** Brief halfResolutionMode
		* If halfResolutionMode is enabled the denoiser will return the signal in upscaled full resolution.
		* Expected to match the TraceTaskCommon::halfResolutionMode used to generate input data.
		* Note: Must be OFF when running NRD_Sigma
		*/
		HalfResolutionMode		halfResolutionMode = HalfResolutionMode::OFF;

		/** Brief viewport
		* The gbuffer depth, expected to match TraceTaskCommon::viewport used in the Trace task.
		* Required for all denoising methods
		*/
		Viewport				viewport;

		/** Brief depth
		* The gbuffer depth, expected to match TraceTaskCommon::depth used in the Trace task.
		* Required for all denoising methods
		*/
		DepthInput				depth;

		/** Brief normal
		* The gbuffer normal, expected to match TraceTaskCommon::normal used in the Trace task.
		* Required for all denoising methods
		*/
		NormalInput				normal;

		/** Brief roughness
		* The gbuffer roughness, expected to match TraceTaskCommon::roughness used in the Trace task.
		* Required for all denoising methods (including shadows).
		*/
		RoughnessInput			roughness;

		/** Brief roughness
		* The gbuffer motion vectors. It's expected to be in range [0,1[. use motion.scale to flip or scale the engine motion vectors. 
		* Is optional only if debugDisableMotion=true.
		* Required for all denoising methods.
		*/
		MotionInput				motion;

		/** Brief debugDisableMotion
		* This is recommended to enable only to triage errors that are suspected to be related to motion or view/world/clip matrices.
		* When debugDisableMotion=true, reprojection will be carried out using view/world matrices, instead of the motion vectors.
		* This is however not recommended in a shipping title as it will result in noticable ghosting of objects in motion.
		*/
		bool					debugDisableMotion = false;

		Math::Float_4x4			clipToViewMatrix = Math::Float_4x4::Identity();			//!< (Pos_View) = (Pos_CliP) * (M). Must be unjittered, jitter will be reconstructed internally as needed using cameraJitter
		Math::Float_4x4			viewToClipMatrix = Math::Float_4x4::Identity();			//!< (Pos_CliP) = (Pos_View) * (M). Must be unjittered, jitter will be reconstructed internally as needed using cameraJitter
		Math::Float_4x4			viewToClipMatrixPrev = Math::Float_4x4::Identity();		//!< (Pos_CliP) = (Pos_View) * (M). Sould be the viewToClipMatrix used in the previous frame.
		Math::Float_4x4			worldToViewMatrix = Math::Float_4x4::Identity();		//!< (Pos_View) = (Pos_World) * (M). Must be unjittered, jitter will be reconstructed internally as needed using cameraJitter
		Math::Float_4x4			worldToViewMatrixPrev = Math::Float_4x4::Identity();	//!< (Pos_View) = (Pos_WorldPrev) * (M). Sould be the worldToViewMatrix used in the previous frame.

		Math::Float_2			cameraJitter = { 0.f, 0.f }; //!< [-0.5; 0.5] - sampleUv = pixelUv + cameraJitter
	};

	struct DenoiseSpecularTask : public Task {
		DenoiseSpecularTask() : Task(Type::DenoiseSpecular) {};

		DenoisingTaskCommon		common;

		/** Brief context
		* Required. Must be either DenoisingMethod::NRD_Reblur or DenoisingMethod::NRD_Relax. Must be SignalType::Specular
		*/
		DenoisingContextHandle	context = DenoisingContextHandle::Null;

		/** Brief inSpecular
		* Required. Recommended precision RGBA16f+. Must contains RGB-Radiance, A-HitT.
		* Expected to be the TraceSpecularTask::out resource without any intermediate conversion required.
		*/
		ShaderResourceTex		inSpecular;

		/** Brief inOutSpecular
		* Required. Recommended precision RGBA16f+. Will returned the denoised RGB-Radiance and denoised A-HitT.
		* Note: inOutSpecular should keep use the same resource between frames as the denoising result is temporally accumulated.
		*/
		CombinedAccessTex		inOutSpecular;
	};

	struct DenoiseDiffuseTask : public Task {
		DenoiseDiffuseTask() : Task(Type::DenoiseDiffuse) {};

		DenoisingTaskCommon		common;

		/** Brief context
		* Required. Must be either DenoisingMethod::NRD_Reblur or DenoisingMethod::NRD_Relax. Must be SignalType::Diffuse
		*/
		DenoisingContextHandle	context = DenoisingContextHandle::Null;

		/** Brief inDiffuse
		* Required. Recommended precision RGBA16f+. Must contains RGB-Radiance, A-HitT.
		* Expected to be the TraceDiffuseTask::out resource without any intermediate conversion required.
		*/
		ShaderResourceTex		inDiffuse;

		/** Brief inOutDiffuse
		* Required. Recommended precision RGBA16f+. Will returned the denoised RGB-Radiance and denoised A-HitT.
		* Note: inOutDiffuse should keep use the same resource between frames as the denoising result is temporally accumulated.
		*/
		CombinedAccessTex		inOutDiffuse;
	};

	/** Brief DenoiseSpecularAndDiffuseTask
	* Is used to efficently denoise a specular and a diffuse signal at the same time. 
	*/
	struct DenoiseSpecularAndDiffuseTask : public Task {
		DenoiseSpecularAndDiffuseTask() : Task(Type::DenoiseSpecularAndDiffuse) {};

		DenoisingTaskCommon		common;

		/** Brief context
		* Required. Must be either DenoisingMethod::NRD_Reblur or DenoisingMethod::NRD_Relax. Must be SignalType::SpecularAndDiffuse
		*/
		DenoisingContextHandle	context = DenoisingContextHandle::Null;

		/** Brief inSpecular
		* Required. Recommended precision RGBA16f+. Must contains RGB-Radiance, A-HitT.
		* Expected to be the TraceSpecularTask::out resource without any intermediate conversion required.
		*/
		ShaderResourceTex	inSpecular;

		/** Brief inOutSpecular
		* Required. Recommended precision RGBA16f+. Will returned the denoised RGB-Radiance and denoised A-HitT.
		* Note: inOutSpecular should keep use the same resource between frames as the denoising result is temporally accumulated.
		*/
		CombinedAccessTex	inOutSpecular;

		/** Brief inDiffuse
		* Required. Recommended precision RGBA16f+. Must contains RGB-Radiance, A-HitT.
		* Expected to be the TraceDiffuseTask::out resource without any intermediate conversion required.
		*/
		ShaderResourceTex	inDiffuse;

		/** Brief inOutDiffuse
		* Required. Recommended precision RGBA16f+. Will returned the denoised RGB-Radiance and denoised A-HitT.
		* Note: inOutDiffuse should keep use the same resource between frames as the denoising result is temporally accumulated.
		*/
		CombinedAccessTex	inOutDiffuse;
	};

	struct DenoiseDiffuseOcclusionTask : public Task {
		DenoiseDiffuseOcclusionTask() : Task(Type::DenoiseDiffuseOcclusion) {};

		DenoisingTaskCommon		common;

		/** Brief context
		* Required. Must be DenoisingMethod::NRD_Reblur and SignalType::DiffuseOcclusion
		*/
		DenoisingContextHandle	context = DenoisingContextHandle::Null;

		/** Brief hitTMask
		* HitT is calculated as the inner product of this value and the inHitT texture.
		*/
		Math::Float_4			hitTMask = { 0.f, 0.f, 0.f, 1.f };

		/** Brief inHitT
		* Required. Recommended precision R16f+. Must contain HitT. Negative values indicate ray miss.
		* Expected to be the TraceAmbientOcclusionTask::out resource without any intermediate conversion required.
		*/
		ShaderResourceTex		inHitT;

		/** Brief inOutOcclusion
		* Required. Recommended precision R8+. Will returned the denoised normalized occlusion.
		* Note: inOutOcclusion should keep use the same resource between frames as the denoising result is temporally accumulated.
		*/
		CombinedAccessTex		inOutOcclusion;
	};

	struct DenoiseShadowTask : public Task {
		DenoiseShadowTask() : Task(Type::DenoiseShadow) {};

		DenoisingTaskCommon		common;

		/** Brief context
		* Required. Must be DenoisingMethod::NRD_Sigma and SignalType::Shadow
		*/
		DenoisingContextHandle	context = DenoisingContextHandle::Null;

		/** Brief inShadow
		* Required. Recommended format R16f+. Must contain ray HitT.
		* Expected to be the TraceShadowTask::out resource without any intermediate conversion required.
		*/
		ShaderResourceTex		inShadow;

		/** Brief inOutShadow
		* Required. Recommended format R8f+. Will returne the denoised per pixel visibility [0, 1].
		* Note: inOutShadow should keep use the same resource between frames as the denoising result is temporally accumulated.
		*/
		CombinedAccessTex		inOutShadow;
	};

	struct DenoiseMultiShadowTask : public Task {
		DenoiseMultiShadowTask() : Task(Type::DenoiseMultiShadow) {};

		DenoisingTaskCommon		common;

		/** Brief context
		* Required. Must be DenoisingMethod::NRD_Sigma and SignalType::MultiShadow
		*/
		DenoisingContextHandle	context = DenoisingContextHandle::Null;

		/** Brief inShadow0
		* Required. Recommended format RG16f+. Must contain the opaque NRD-specific data format returned from TraceMultiShadowTask::out0.
		*/
		// RG16f+, - Opaque NRD denoising data.
		ShaderResourceTex		inShadow0;

		/** Brief inShadow0
		* Required. Recommended format RGBA8+. Must contain the opaque NRD-specific data format returned from TraceMultiShadowTask::out1.
		*/
		ShaderResourceTex		inShadow1;

		/** Brief inOutShadow
		* Required. Recommended format R8f+. Will returne the denoised per pixel visibility [0, 1].
		* Note: inOutShadow should keep use the same resource between frames as the denoising result is temporally accumulated.
		*/
		CombinedAccessTex		inOutShadow;
	};
};


/**
* A handle to keep and identify geometry which represents a bottom level acceleration structure (BLAS).
* SDK holds corresponded GPU resource and some sates while the handle is valid and live. ExecuteContext creates and destroys it.
* This handle is required to schedule various kind of BVH tasks defined below.
* Users need to maintain the lifetime of the handle and properly destroy it when it doen't need anymore to optimize VRAM usage.
*/
enum class GeometryHandle : uint64_t { Null = 0 };

/**
* A handle to keep and identify an instance of a geometry which represents an instance of top level acceleration structure (TLAS).
* An instance is participating in the scene while the handle is valid and live. ExecuteContext creates and destroys it.
* This handle is required to schedule various kind of BVH tasks defined below.
* Users need to maintain the lifetime of the handle and properly destroy it when it doesn't need anymore to optimize TLAS.
*/
enum class InstanceHandle : uint64_t { Null = 0 };

/**
* This is a namespace for all tasks to process geometries and instances including BVH processing.
* A list of BVH tasks are interpreted to a set of dispatches into a command list to pre-process geometries, allocate direct lighting caches, and BLAS, TLAS buildings.
*/
namespace BVHTask
{
	/**
	* Both GeometryTask and InsntaceTask have Register and Update tasks.
	* Register is used when a geometry or an instance is newly introduced into the scene.
	* Update is used when a geometry's local vertex position has been changed whith keeping its topology, or, used when an instance's world position is changed.
	*/
	enum class TaskOperation : uint32_t
	{
		Register = 0,
		Update = 1,
	};

	/**
	* This is a structure to provide information to SDK when registering a geometry.
	* Basically, it consisted input vertex and indices buffers to represent an object with polygons.
	* It also has some additional information needed when building direct lighting cache and BLAS.
	*/
	struct GeometryInput {
		enum class Type : uint32_t
		{
			TrianglesIndexed,
			Triangles
		};

		/**
		* Surfel type of direct lighting cache. While WarpedBarycentricStorage provides a simple and small memory footprint,
		* MeshColors provides an opportunity to interpolate surfel’s color over surfaces.
		* In short, you can start with default (WarpedBarycentricStorage). When when you find a corner-case of
		* reflected surface’s flickering or a blocky rendering on smooth reflections, you should consider switching to MeshColors.
		*/
		enum class SurfelType : uint32_t {
			WarpedBarycentricStorage,
			MeshColors
		};

		/**
		* This is directly interpreted to a build flag when building a BLAS.
		* General advice for these flags can be find on various articles or presentations stored on the Internet.
		*/
		enum class BuildHint : uint32_t {
			// Auto will use heuristics to select the optimal build flag hint
			Auto,	
			PreferFastTrace,
			PreferFastBuild,
			PreferNone,
		};

		const wchar_t* name = nullptr;

		/** 
		* Enabling this flag will skip surfel allocation calculation and directly map each surfel to each polygon.
		* Enabling this frag will suit for high density meshes. SDK also switches to direct mapping automatically if calculated tile number is close enough to the number of primitive.
		* The threshold value is set by directTileMappingThreshold.
		* This is only valid with SurfelType::TileCache.
		*/
		bool				forceDirectTileMapping = false;	

		/**
		* The threshold value to control direct tile mapping mode described above.
		*/
		float				directTileMappingThreshold = 0.7f; 

		/**
		* Set true when a geometry is planned to be updated. Dynamic and Static geometries are allocated in different memory pool to avoid fragmentations.
		*/
		bool					allowUpdate = false;
		bool					useTransform = false;
		Math::Float_3x4			transform = Math::Float_3x4::Identity();


		/**
		* When allocating surfels, SDK tries to allocate them along with the tile unit length.
		* If you set a smaller value larger number of surfels will be allocated on the same size of a polygon.
		* Users need to find out the best balance between quality and VRAM usage.
		*/
		float					tileUnitLength = 1.f;

		/**
		* This is a upper limit value of surfel resolution along a polygon edge. This is to avoid allocating massive amount of surfels for a huge primitive.
		* Users can set a large number with a lisk of huge VRAM allocations.
		*/
		uint32_t				tileResolutionLimit = 64u;

		Type					type = Type::TrianglesIndexed;
		SurfelType				surfelType = SurfelType::MeshColors;
		BuildHint				buildHint = BuildHint::Auto;

		VertexBufferInput	vertexBuffer;
		IndexBufferInput	indexBuffer;

		/** 
		* This is an optional parameter to optimize VRAM usage for copies of vertex and index buffer in SDK.
		* SDK copies vertex and index buffer for each geometry once when building BLAS.
		* If users provides a large vertex buffer and an index buffer which refers few vertices,
		* that will result in an inefficient VRAM allocation because SDK has to copy the entire vertex buffer for few primitives.
		* This parameter is to limit the region of vertex buffer which is actually being used by this geometry, to make an efficient copy of vertex buffer.
		*/
		struct {
			bool					isEnabled = false;
			uint32_t				minIndex = 0;
			uint32_t				maxIndex = 0;
		} indexRange;
	};

	/**
	* This is a structure to provide information to the SDK when registering an instance.
	* An instance acts as an instance of a top-level acceleration structure
	* by setting a 4x3 matrix and refering a GeometryHandle which represents a BLAS.
	*/
	struct InstanceInput {
		const wchar_t* name = nullptr;
		Math::Float_3x4		transform = Math::Float_3x4::Identity();
		GeometryHandle		geomHandle = GeometryHandle::Null;

		float				initialTileColor[3] = { 0.f, 0.f, 0.f };
	};

	/**
	* The base class of various BVH tasks.
	*/
	struct Task {
		enum class Type : uint32_t {
			Unknown = 0,

			Geometry,
			Instance,
			BVHBuild,
		};

		const Type	type;
	protected:
		Task(Type typ) : type(typ) {};
	};

	/**
	* This task is used when registering and updating a geometry.
	* A geomety essentially acts as a BLAS in the SDK.
	* A GeometryHandle is needed to be created through the ExecuteContext in advance of scheduling the task.
	*/
	struct GeometryTask : public Task
	{
		GeometryTask() : Task(Type::Geometry) {};

		TaskOperation	taskOperation = TaskOperation::Register;
		GeometryHandle	handle;
		GeometryInput	input;
	};

	/**
	* This task is used when registering and updating an instance.
	* An instance acts as an instance of a top-level acceleration structure.
	* An InstanceHandle is need to be created through the ExecuteContext in advance of scheduling the task.
	*/
	struct InstanceTask : public Task
	{
		InstanceTask() : Task(Type::Instance) {};

		TaskOperation	taskOperation = TaskOperation::Register;
		InstanceHandle	handle;
		InstanceInput	input;
	};

	/**
	* This is a task to schedule BVH build process to the task container.
	* If any of a geometry or an instance has been updated by a scheduled task, or, any of them is destroyed via ExecuteContext,
	* this task must be scheduled with buildTLAS flag before doing any rendering tasks.
	* Otherwise SDK will produce an error, since any of rendering task cannot run with an obsolete TLAS.
	*/
	struct BVHBuildTask : public Task
	{
		/** 
		* The max number of BLASes to be built from the build queue.
		* When registering geometries, building BLAS processes are stacked up on a queue
		* and are processed by the number of maxBlasBuildCount to avoid sudden long processing time.
		*/
		uint32_t maxBlasBuildCount = 4u;

		/**
		* Set true to build TLAS.
		* TLAS build is automatically skipped even the flag is set to true if there isn't any geometry or instance update.
		*/
		bool	 buildTLAS = true;

		BVHBuildTask() : Task(Type::BVHBuild) {};
	};

};

/** 
 * Task container stores various RenderTask and BVHTask which will then turned into a command list via ExecuteContext.
 */
struct KickstartRT_DECLSPEC_INL TaskContainer {
protected:
	TaskContainer();
	virtual ~TaskContainer();

public:
	virtual Status ScheduleRenderTask(const RenderTask::Task* renderTask) = 0;
	virtual Status ScheduleRenderTasks(const RenderTask::Task* const * renderTaskPtrArr, uint32_t nbTasks) = 0;
	virtual Status ScheduleBVHTask(const BVHTask::Task* bvhTask) = 0;
	virtual Status ScheduleBVHTasks(const BVHTask::Task* const * bvhTaskPtrArr, uint32_t nbTasks) = 0;
};

#if !defined(KickstartRT_ExecutionContext_Interop)
/**
 * This handle is provided when the SDK builds a command list.
 * The handle represents the life-time of the command list, so if it's alive,
 * it means the command list is in-flight in the GPU.
 * Users must return the handle as soon as it can by calling MarkedGPUTaskCompleted() function with the handle,
 *  so that SDK can reuse the resources in subsequent GPU tasks.
 */
enum class GPUTaskHandle : uint64_t { Null = 0 };
#endif

/**
 * The main gate of the SDK. 
 * An instance of this struct represents the execute context of SDK.
 */
struct KickstartRT_DECLSPEC_INL ExecuteContext {
protected:
	virtual ~ExecuteContext();
	ExecuteContext();

public:
	/**
	 * Initalzie the SDK.
     * @param [in] setting A setting to be used when initializing the SDK.
	 * @param [out] exc A valid pointer should be returned when initialization is succeeded.
	 * @param [in] headerVersion It will be compared against lib's version. It must be compatible to the lib's version.
	 * @return Returns Status::OK if succeeded.
	 */
	static Status Init(const ExecuteContext_InitSettings* settings, ExecuteContext** exc, const KickstartRT::Version headerVersion = KickstartRT::Version());

	/**
	 * Destruct the SDK.
	 * @param [in] exc A pointer to an execute context.
	 * @return Returns Status::OK if succeeded.
	 */
	static Status Destruct(ExecuteContext* exc);

	/**
	 * Creates a task container. 
	 */
	virtual TaskContainer* CreateTaskContainer() = 0;

	/**
	* Creates a denoising context handle. 
	* It's expected to have a denoising context handle allocated for each effect (Reflections, GI, AO, Shadows) used,
	* to compare different denoiser methods (NRD_Relax vs NRD_Reblur) new context(s) must be created.
	* @param [in] input a denoising context handle description. 
	*/
	virtual DenoisingContextHandle CreateDenoisingContextHandle(const DenoisingContextInput* input) = 0;
	
	/**
	* Destroys a denoising context handle. 
	* It's safe to call this function before the render tasks referencing the handle have been finalized (I.e MarkGPUTaskAsCompleted is called),
	* the SDK will defer deletion of the underlying resources until the render tasks have completed.
	* @param [in] handle a denoising context handle.
	*/
	virtual Status DestroyDenoisingContextHandle(DenoisingContextHandle handle) = 0;

	/**
	* Destroys all denoising context handles.
	*/
	virtual Status DestroyAllDenoisingContextHandles() = 0;

	/**
	 * Creates a geometry handle.
	 */
	virtual GeometryHandle CreateGeometryHandle() = 0;

	/**
	 * Creates geometry handles.
	 * @param [in] handles A storage for the handles. The size must be larger than nbHandles.
	 * @param [in] nbHandles A number of handles to be created.
	 * @return Returns Status::OK if succeeded.
	 */
	virtual Status CreateGeometryHandles(GeometryHandle* handles, uint32_t nbHandles) = 0;

	/**
	 * Destroys a geometry handle.
	 * @param [in] handle A geometry handle to destroy.
	 */
	virtual Status DestroyGeometryHandle(GeometryHandle handle) = 0;

	/**
	 * Destroys geometry handles.
	 * @param [in] handles A storage for the handles. The size must be larger than nbHandles.
	 * @param [in] nbHandles A number of handles to be destroyed.
	 * @return Returns Status::OK if succeeded.
	 */
	virtual Status DestroyGeometryHandles(const GeometryHandle* handles, uint32_t nbHandles) = 0;

	/**
     * Destroys all geometry handles that are currently valid and live.
     */
	virtual Status DestroyAllGeometryHandles() = 0;

	/**
	 * Creates an instance handle.
	 */
	virtual InstanceHandle CreateInstanceHandle() = 0;

	/**
	 * Creates instance handles.
	 * @param [in] handles A storage for the handles. The size must be larger than nbHandles.
	 * @param [in] nbHandles A number of handles to be created.
	 * @return Returns Status::OK if succeeded.
	 */
	virtual Status CreateInstanceHandles(InstanceHandle* handles, uint32_t nbHandles) = 0;

	/**
	 * Destroys an instance handle.
	 * @param [in] handle An instance handle to destroy.
	 */
	virtual Status DestroyInstanceHandle(InstanceHandle handle) = 0;

	/**
	 * Destroys instance handles.
	 * @param [in] handles A storage for the handles. The size must be larger than nbHandles.
	 * @param [in] nbHandles A number of handles to be destroyed.
	 * @return Returns Status::OK if succeeded.
	 */
	virtual Status DestroyInstanceHandles(const InstanceHandle* handles, uint32_t nbHandles) = 0;

	/**
	 * Destroys all instance handles that are currently valid and live.
	 */
	virtual Status DestroyAllInstanceHandles() = 0;

#if defined(KickstartRT_ExecutionContext_Interop)
	/**
	 * This is for D3D11 interop layer only.
	 * By calling this, the SDK will create a command list on D3D12 and then execute it with D3D11 fence objects.
	 * @param [in] container the task container for a GPU task.
	 * @param [in] input An input structure which contains some parameters to build a GPU task.
	 * @return Returns Status::OK if succeeded.
	 */
	virtual Status InvokeGPUTask(TaskContainer* container, const BuildGPUTaskInput* input) = 0;
#else
	/**
	 * Builds a GPU command list with the provided TaskContainer and returns a valid GPUTaskHanlde.
	 * It is SDK users responsibility to execute the built command list on an execution queue
	 * and hands off the returned GPUTaskHandle when it's completed on the GPU.
	 * @param [out] retHandle A GPUTaskHanldle It must be hand off via MarkGPUTaskAsCompleted() when the corresponded GPU task has been completed on the GPU.
	 * @param [in] container A task container used to build a GPU command list.
	 * @param [in] input An input structure which contains some parameters to build a GPU task.
	 * @return Returns Status::OK if succeeded.
	 */
	virtual Status BuildGPUTask(GPUTaskHandle *retHandle, TaskContainer *container, const BuildGPUTaskInput* input) = 0;

	/**
	 * Hands off a GPUTaskHandle.
	 * It is SDK users responsibility to hand off the returned GPUTaskHandle when the corresponded GPU task has been completed on the GPU.
	 * @param [in] handle The GPUTaskHandle to be returned.
	 * @return Returns Status::OK if succeeded.
	 */
	virtual Status MarkGPUTaskAsCompleted(GPUTaskHandle handle) = 0;
#endif

	/**
	 * Immediately releases allocated resources and memories for destroyed geometry, instance and denoising context.
	 * To call this method, all GPUTaskHandles must be returned to the SDK, so there is no in-flight GPU task.
	 * This is to be called when the application want to change whole scene by loading a new level.
	 * @param [in] handle The GPUTaskHandle to be returned.
	 * @return Returns Status::OK if succeeded.
	 */
	virtual Status ReleaseDeviceResourcesImmediately() = 0;

	/**
	 * This returns shader IDs that currently loaded in the SDK. This list can be used when initializing the SDK next time
	 * to compile shaders in advance of use to avoid shader compile hitching. 
	 * @param [in, out] loadedListBuffer The storage to return the shader list. The size must be larger than the bufferSize.
	 * @param [in] bufferSize The size of the loadedListBuffer.
	 * @param [out] retListSize The size of the returned shader list.
	 * @return Returns Status::OK if succeeded.
	 */
	virtual Status GetLoadedShaderList(uint32_t* loadedListBuffer, size_t bufferSize, size_t* retListSize) = 0;

	/**
	 * Returns the current VRAM resource allocation by the SDK.
	 * @param [in, out] retStatus The storage to return the allocation information.
	 * @return Returns Status::OK if succeeded.
	 */
	virtual Status GetCurrentResourceAllocations(KickstartRT::ResourceAllocations* retStatus) = 0;

	/**
	 * By calling this, A CSV file will be written to the provided path with the resouce allocation information.
	 * This can be used to understand the current resource allocations.
	 * @param [in] filePath A file path to write a CSV file.
	 * @return Returns Status::OK if succeeded.
	 */
	virtual Status BeginLoggingResourceAllocations(const wchar_t* filePath) = 0;

	/**
	 * Stops logging resource allocations.
	 * @return Returns Status::OK if succeeded.
	 */
	virtual Status EndLoggingResourceAllocations() = 0;
};

/**
 * SDK Version
 */
namespace Version {
	/**
	 * Returns a Version what the library codes see at the compile time.
	 * @return The SDK version
	 */
	KickstartRT_DECLSPEC_INL KickstartRT::Version GetLibraryVersion();
};
