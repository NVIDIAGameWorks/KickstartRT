# Kickstart RT Integration Guide
- [Basic Integration Overview](#basic-integration-overview)

- [Executing CPU and GPU work](#executing-cpu-and-gpu-work)

- [Debugging](#debugging)

- [Optimisations and Tuning](#optimisations-and-tuning)

This guide aims to provide insight and recommendations for integrating
the Kickstart RT SDK into a game. It is expected that the reader has
read the repo [Readme](../README.md) and the [SDK Reference Guide](SDK_Reference.md) to familiarize themselves with the
key algorithmic details of the SDK.

![Overview Image](https://user-images.githubusercontent.com/100689874/158652196-92878b8f-65e6-4df3-be67-3bc4fe2da373.PNG)

The SDK provides support for shadows, ambient occlusion (AO), global
illumination (GI) and reflections.

# Basic Integration Overview

The Kickstart RT SDK has 4 main phases of operation to generate
raytraced output for your game.

1.  **BVH Building**. Internally this updates the BLAS and TLAS for the scene.

2.  **Direct Light Injection**. The game will provide the rasterized lighting and depth buffers along with view matrices which Kickstart RT uses to populate the internal lighting cache for each mesh.

3.  **Raytraced Output**. Kickstart RT requires the gbuffer including depth, normals, roughness, specular and color buffers. These are used to generate the secondary rays that intersect with the BVHs to generate the raytraced reflections. Shadows will require lighting data.

4.  **Denoising**. Raytracing is inherently noisy for non mirror surfaces, but Kickstart RT has NVIDIAs realtime denoiser (NRD) seamlessly integrated to denoise the output images if required. Kickstart RT requires the gbuffer again, along with the velocity buffer to provide correct denoising.

![Integration Phases Image](https://user-images.githubusercontent.com/100689874/158650279-675c856f-9612-4c0e-a2b3-c0114e8e2524.PNG)

Throughout all of these phases or passes, the surface area of the
integration should be relatively small and no shaders or material
systems require any modifications. The only shaders that may be required
will be a conversion pass to decode gbuffer image formats to Kickstart
RT supported ones.

Each phase of operation will either work directly with the Kickstart RT
execution context, or with a Kickstart RT render task and task container
where the execution context maps to a DX12/Vulkan device, a task
container maps to a command list and a task is a high level rendering
operation.

![Main Components](https://user-images.githubusercontent.com/100689874/158650928-e68c8a71-c4ac-45f5-aca3-7dcff1e9f013.PNG)

The context manages the lifetime of objects (BLAS/TLAS) and the internal
state. The tasks are scheduled for processing against a task container
and the task container will then populate the provided command list when
the tasks are built. It is important to highlight that the engine
**owns** all command lists and command queues; Kickstart RT will record
the command lists to be executed by the engine.

## BVH Building

### Mesh Initialisation
Kickstart RT will automatically build and maintain the BVH's for the
scene, but the engine must provide the input vertex and index buffers
with the transform matrices for each object that is to be visible to the
raytracing engine.

Kickstart RT defines a **Geometry** to represent a mesh with an
associated BLAS and an **Instance** to represent an instance of a
Geometry with an associated lighting cache resource.

It is recommended that the engine creates a Kickstart RT geometry for
all renderable game objects that are to appear in the raytraced outputs
and the returned geometry handles should be associated with the game
object.

```
// Build the BVHs for all geometries at startup
for (auto& prim : primitiveList)
{
    KickstartRT::D3D12::BVHTask::GeometryTask task;

    // Create a geometry handle
    m_executeContext->CreateGeometryHandles(&task.handle, 1);

    // populate the index buffer and vertex buffer data
    task.input.indexBuffer.resource = prim->getIndexBuffer()->getD3DBuffer();
    task.input.indexBuffer.format = prim->getIndexBuffer()->getFormat();
    task.input.indexBuffer.offsetInBytes = prim->getStartIndex() * prim->indexSizeInBytes();
    task.input.indexBuffer.count = prim->getTriangleCount() * 3;
    task.input.vertexBuffer.resource = prim->getVertexBuffer()->getD3DBuffer();
    task.input.vertexBuffer.format = prim->getVertexBuffer()->getFormat();
    task.input.vertexBuffer.offsetInBytes = prim->getVertexBuffer()->getOffset();
    task.input.vertexBuffer.strideInBytes = prim->getVertexBuffer()->getStride();
    task.input.vertexBuffer.count = prim->getVertexCount();

    // override any defaults
    task.input.type = KickstartRT::D3D12::BVHTask::GeometryInput::Type::TrianglesIndexed;

    // schedule the task to be built against a task container.
    m_taskContainer->ScheduleBVHTask(&task);

    // store the handle for later use
    prim->m_KSGeomHandle = task.handle;
}
```

Multiple tasks can be scheduled at once for efficiency, but the above
pseudo-code shows single tasks scheduled for simplicity.

When creating a Kickstart RT geometry, various default parameters can be
over-ridden to help tune the Kickstart RT integration for your game.
These are covered in the [Optimisation and
Tuning](#optimisations-and-tuning) section. [Normal
guidance should be followed for efficient management of
BVHs](https://developer.nvidia.com/blog/best-practices-using-nvidia-rtx-ray-tracing/).

### Mesh Updates and Instance Lifetimes
Each frame, the scene graph is walked and all relevant meshes that
require a Kickstart RT instance/BLAS are processed. Kickstart RT will
generate a TLAS that contains all known instances and it is the
responsibility of the engine to determine which instances should be in
the TLAS.

A game primitive's Kickstart RT instance can be in 1 of 4 states :

-   No longer in the TLAS : destroy instance if it exists

-   Newly visible in the TLAS : create the instance

-   Has moved/changed : update the instance

-   Static with no updates : nothing

All Kickstart RT instances should be checked for inclusion each frame
and modified accordingly before the Kickstart TLAS task is scheduled and
built.

```
// Gather the required instances and build the BLAS/TLAS
for (auto& prim : primitiveList)
{
    if (prim->isVisible(extendedViewFrustum))
    {
        KickstartRT::D3D12::BVHTask::InstanceTask task;

        // If the primitive has already been registered with KS and has an instance handle
        if (prim->m_KSInstanceHandle != KickstartRT::D3D12::InstanceHandle::Null)
        {
            // If the prim hasn't moved/changed, continue
            if (!prim->isDirty())
            {
                continue;
            }

            // Update the BLAS for this primitive.
            // The default operation is KickstartRT::D3D12::BVHTask::TaskOperation::Register
            task.taskOperation = KickstartRT::D3D12::BVHTask::TaskOperation::Update;
        }
        else
        {
            // Create an instance handle to hold the BLAS and lighting cache
            m_12->m_executeContext->CreateInstanceHandles(&prim->m_KSInstanceHandle, 1);
        }

        // Associate the instance to update with the geometry handle
        task.input.geomHandle = prim->m_KSGeomHandle;
        task.handle = prim->m_KSInstanceHandle;
        
        // Copy over the local to world space matrix
        task.input.transform.f = prim->getLocalToWorldF();

        // Schedule the task
        m_taskContainer->ScheduleBVHTask(&task);
    }
    else if (prim->m_KSInstanceHandle != KickstartRT::D3D12::InstanceHandle::Null)
    {
        // Destroy the instance as it is no longer visible
        m_executeContext->DestroyInstanceHandles(&prim->m_KSInstanceHandle, 1);
        prim->m_KSInstanceHandle = KickstartRT::D3D12::InstanceHandle::Null;
    }	
}

// After generating all of the instances, build the TLAS
KickstartRT::D3D12::BVHTask::BVHBuildTask task;

// Set the number of BLASs to be built per frame to avoid large stutters
task.maxBlasBuildCount = c_MaxBlasBuildsPerFrame;

m_taskContainer->ScheduleBVHTask(&task);

```

It is important to note that the view frustum should be expanded in all
directions when visibility culling the scene graph as raytracing needs
to know about meshes behind the main camera as well as in front of it.

It is also important that the instances persist across frames (which may
be different to native RT implementations) as the lighting cache is
associated with each instance. storing the light injection values for
the mesh.

All of this work can be done asynchronously, both on the GPU and whilst
walking the scene graph on the CPU, so the costs should be hidden.

To confirm this works as intended, various debug visualization modes are
supported by the Kickstart RT render tasks so the primary ray HitT
values could be displayed, or the BVHs rendered with random colors.

```
enum class DebugOutputType : uint32_t {
	Default = 0,
	Debug_DirectLightingCache_PrimaryRays = 100,
	Debug_RandomTileColor_PrimaryRays = 101,
	Debug_RandomMeshColor_PrimaryRays = 102,
	Debug_HitT_PrimaryRays = 103,
	Debug_Barycentrics_PrimaryRays = 104,
};
```

## Direct Lighting Injection
Once the BVHs have been built, the current lighting values need to be
baked, or injected, into the lighting cache of the Kickstart RT
instances. To enable this, the engine needs to provide a depth buffer,
the current direct lighting buffer and the set of current view
transformations.

The direct lighting injection buffer is essentially the current
rasterized frame. It should include everything that is to be reflected
in the raytraced output and has an associated BVH to bake the lighting
values into. This should include the lit shaded geometry and shadows,
but not particles or transparent geometry if they do not have associated
Kickstart RT instances.

```
// Light Injection
KickstartRT::D3D12::RenderTask::DirectLightingInjectionTask renderTask;

// Provide Depth buffer
renderTask.depth.tex = initShaderResourceTex(pDepthBuffer);
renderTask.depth.type = KickstartRT::D3D12::RenderTask::DepthType::R_ClipSpace;

// Provide color buffer
renderTask.directLighting = initShaderResourceTex(pDirectLightingBuffer);

// Viewport
renderTask.viewport = viewport;

// Matrices
renderTask.clipToViewMatrix = cameraView.getClipToViewF();
renderTask.viewToWorldMatrix = cameraView.getViewToWorldF();

// Tuning parameters
renderTask.averageWindow = c_AccumulationWindow;
renderTask.useInlineRT = false;

// Schedule the task
m_taskContainer->ScheduleRenderTask(&renderTask);
```

To confirm that the injection works as intended, try to use the
`Debug_DirectLightingCache_PrimaryRays` debug visualization mode. This
will render the BVH meshes with their lit colors baked in.

## Raytracing Output

Kickstart RT can be used to generate various outputs, such as AO, GI,
reflections and shadows. Apart from shadows, these all require similar
gbuffer input in the form of depth, normal, roughness, specular buffers
and various input matrices. The direct lighting buffer from the
injection phase can also be passed back in for specular reflections
and/or diffuse reflections (GI) to provide a higher quality reflection
output.

Each type of output has its own task type, but there are a number of
common parameters that have been put into a common structure.

```
KickstartRT::D3D12::RenderTask::TraceTaskCommon rtTaskCommon;

// Provide Depth buffer
rtTaskCommon.depth.tex = initShaderResourceTex(pDepthBuffer);
rtTaskCommon.depth.type = depthType;

// Provide Normal buffer
rtTaskCommon.normal.tex = initShaderResourceTex(pNormalBuffer);
rtTaskCommon.normal.type = normalType;

// Provide Roughness buffer
rtTaskCommon.roughness.tex = initShaderResourceTex(pRoughnessBuffer);
rtTaskCommon.roughness.globalRoughness = 1.0f;
rtTaskCommon.roughness.roughnessMask = { 0.0f, 0.0f, 0.0f, 1.0f }; // A Channel

// Provide Specular buffer
rtTaskCommon.specular.tex = initShaderResourceTex(pSpecularBuffer);
rtTaskCommon.specular.globalMetalness = 1.0f;

// Provide the direct lighting buffer used in the injection phase
rtTaskCommon.directLighting = initShaderResourceTex(pDirectLightingBuffer);

// Viewport
rtTaskCommon.viewport = viewport;

// Matrices
rtTaskCommon.viewToClipMatrix = cameraView.getViewToClipF();
rtTaskCommon.clipToViewMatrix = cameraView.getClipToViewF();
rtTaskCommon.viewToWorldMatrix = cameraView.getViewToWorldF();
rtTaskCommon.worldToViewMatrix = cameraView.getWorldToViewF();

// Tuning parameters
rtTaskCommon.useInlineRT = false;
rtTaskCommon.rayOffset.type = KickstartRT::D3D12::RenderTask::RayOffset::Type::e_CamDistance;
rtTaskCommon.enableBilinearSampling = true;
rtTaskCommon.halfResolutionMode = false; // Disable checkerboard
```
To schedule a specular reflection task :

```
// Set up the specular reflection render task.
KickstartRT::D3D12::RenderTask::TraceSpecularTask renderTask;
renderTask.common = rtTaskCommon;
renderTask.out = initUnorderedAccessTex(pOutputBuffer);

// Schedule the task
m_taskContainer->ScheduleRenderTask(&renderTask);
```

Or a diffuse task for GI :

```
// Set up the GI diffuserender task.
KickstartRT::D3D12::RenderTask::TraceDiffuseTask renderTask;
renderTask.common = rtTaskCommon;
renderTask.out = initUnorderedAccessTex(pOutputBuffer);
renderTask.diffuseBRDFType = KickstartRT::D3D12::RenderTask::DiffuseBRDFType::NormalizedDisney;

// Schedule the task
m_taskContainer->ScheduleRenderTask(&renderTask);
```

Or an AO task :

```
// Set up the AO task.
KickstartRT::D3D12::RenderTask::TraceAmbientOcclusionTask renderTask;
renderTask.common = rtTaskCommon;
renderTask.out = initUnorderedAccessTex(pOutputBuffer);

// Schedule the task
m_taskContainer->ScheduleRenderTask(&renderTask);
```

Various parameters are available at this stage, such as global
metalness, various roughness modifiers, ray offset types, diffuse BRDF
types etc as well as the opportunity to have a separate demodulated
specular buffer returned that can be multiplied with the denoised
specular buffer to provide a higher quality specular reflection.

Multiple tasks can be scheduled against a container before it is built
and we would recommend a single task container to be used for the light
injection, RT output and denoising.

### Transparent Reflections

Transparent reflections can be achieved by creating another specular
reflection task that will take transparent surface properties as
parameters. Kickstart RT will trace specular rays and shade them using
the direct lighting cache. There is no support in Kickstart RT for light
transmission. The feature works the best when there is a single
transparent surface layer such as a regular clear class.

Perfectly smooth transparent reflections don't require denoising, but if
the transparent surface has some roughness then a denoising pass might
be required.

### Shadows

To render shadows the engine is required to convert its internal light
representation to the Kickstart RT defined light structure *LightInfo*.
The supported types are Directional, Point and Spot lights. Kickstart RT
will trace a single ray towards each light source for each pixel,
meaning that no stochastic selection process of the lights themselves
are taking place, only the point on the light surface to trace against
is randomized. For this reason it's advised to keep the number of active
lights as low as possible.

There are two render tasks related to shadow rendering.
*TraceShadowTask* and *TraceMultiShadowTask*. As implied the
*TraceMultiShadowTask* is meant to be used when the number of light
sources is greater than one.

The primary reason it's separated in two tasks relates to the output
visibility representation. In multi light mode an additional destination
UAV is required, and the data is not trivially discernible. In single
light source mode the output is a single fp16+ value representing the
closest hit distance, negative values indicate miss (=light is visible).
When tracing multiple lights the visibility information for all lights
is merged into a format tailored for NRD. Instead of hit distance a
weighted combination of visibility, intensity, distance and depth is
stored. The specifics of the format is not a secret, but it's purposely
opaque to stay forward compatible with future NRD releases.
DenoiseMultiShadow task understands this format and will return denoised
visibility that the engine can read directly.

Both single and multi light sources can be passed to corresponding
denoising tasks. The output is the same for shadow denoising, which is a
single visibility value per pixel.

## Denoising

Noisy output is expected for non-mirror reflections and whilst game
developers can write their own denoiser from scratch, Kickstart RT
conveniently provides support for the NRD denoiser if desired. To
simplify integration, the Kickstart RT API provides a simplified subset
of parameters and options for controlling NRD. As source code is
provided, these defaults can obviously be modified directly if needed.

NRD supports several denoisers depending on the input - Reblur, Relax
and Sigma. To use the denoisers, an NRD context needs to be created for
each denoiser when Kickstart RT is initialized. The relevant denoiser
context should then be used when executing the denoising operations.

```
// Create the denoiser
KickstartRT::D3D12::DenoisingContextInput input;
input.maxWidth = maxWidth;
input.maxHeight = maxHeight;
input.denoisingMethod = KickstartRT::D3D12::DenoisingContextInput::DenoisingMethod::NRD_Relax;
input.signalType = KickstartRT::D3D12::DenoisingContextInput::SignalType::SpecularAndDiffuse;

m_KSDenoisingContext = m_executeContext->CreateDenoisingContextHandle(&input);
```

A denoising task is generated for each buffer to denoise, with the
exception that specular reflection and diffuse GI can be denoised from a
single combined task. Similar to the RT output passes, the depth, normal
and roughness buffers need providing along with the velocity buffer to
allow NRD to temporally denoise the various inputs correctly.

```
// Denoise
KickstartRT::D3D12::RenderTask::DenoisingTaskCommon dTaskCommon;

// Provide Depth buffer
dTaskCommon.depth.tex = initShaderResourceTex(pDepthBuffer);
dTaskCommon.depth.type = KickstartRT::D3D12::RenderTask::DepthType::R_ClipSpace;

// Provide Normal buffer
dTaskCommon.normal.tex = initShaderResourceTex(pNormalBuffer);
dTaskCommon.normal.type = KickstartRT::D3D12::RenderTask::NormalType::RGB_NormalizedVector; // RGB_Vector;

// Provide Velocity buffer
dTaskCommon.motion.tex = initShaderResourceTex(pVelocityBuffer);
dTaskCommon.motion.type = KickstartRT::D3D12::RenderTask::MotionType::RG_ViewSpace; // RG_ClipSpace; // RGB_WorldSpace;

// Provide Roughness buffer.
dTaskCommon.roughness.tex = initShaderResourceTex(pRoughnessBuffer);
dTaskCommon.roughness.globalRoughness = 1.0f;
dTaskCommon.roughness.roughnessMask = { 0.0f, 0.0f, 0.0f, 1.0f }; // A Channel

// Viewport
dTaskCommon.viewport = viewport;

// Matrices
dTaskCommon.viewToClipMatrix		= cameraView.getViewToClipF();
dTaskCommon.clipToViewMatrix		= cameraView.getClipToViewF();
dTaskCommon.clipToViewMatrixPrev	= cameraView.getPreviousClipToViewF();
dTaskCommon.viewToWorldMatrix		= cameraView.getViewToWorldF();
dTaskCommon.worldToViewMatrix		= cameraView.getWorldToViewF();
dTaskCommon.worldToViewMatrixPrev	= cameraView.getPreviousWorldToViewF();

// Tuning parameters
dTaskCommon.cameraJitter = { jitterX, jitterY };
dTaskCommon.halfResolutionMode = false; // Needs to match the RT output

// Denoise Specular and Diffuse buffers together
KickstartRT::D3D12::RenderTask::DenoiseSpecularAndDiffuseTask dTask;
dTask.common = dTaskCommon;
dTask.context = m_KSDenoisingContext;

// Provide Input Specular buffer
dTask.inSpecular = initShaderResourceTex(pInSpecularBuffer);
dTask.inDiffuse = initShaderResourceTex(pInDiffuseBuffer);
dTask.inOutSpecular = initCombinedAccessTex(pOutSpecularBuffer);
dTask.inOutDiffuse = initCombinedAccessTex(pOutDiffuseBuffer);

// Schedule the task
m_taskContainer->ScheduleRenderTask(&renderTask);
```

Once the output buffers have been denoised, they can be passed back into
the render pipeline for consumption by the remaining stages.

# Executing CPU and GPU work

Preparing and scheduling tasks against a task container, as seen in the
previous section, is the first part of the execution flow for Kickstart
RT.

![Execution and Data Flow](https://user-images.githubusercontent.com/100689874/158651562-2e306e3c-554c-4d51-b6da-9d2fed381b95.PNG)

The next part is to record the command lists and then execute them. As
Kickstart RT does not own the command lists, the game engine must manage
the synchronization and dependencies as well as select which command
queue the command list executes on. It is recommended to execute the BVH
build on the asynchronous command queue early in the frame to allow it
to overlap with other work and hide its cost.

```
// Record the task
KickstartRT::D3D12::BuildGPUTaskInput input = {};
KickstartRT::D3D12::GPUTaskHandle taskHandleTicket;

input.commandList = pD3DCommandList;

// Record the scheduled work to the command list
m_executeContext->BuildGPUTask(&taskHandleTicket, m_taskContainer, &input);
```

Calling `BuildGPUTask` will process the tasks scheduled against the
specific task container and return a handle to the task, referred to as
a ticket in the above diagram. This ticket is used to track the lifetime
of the work and should be returned to Kickstart RT when the work has
been executed on the GPU so that resources can be recycled.

Kickstart RT supports a user configurable number of tickets in flight at
any one time so it is important to track the progress of tickets with
appropriate fences. A typical integration might set the number of task
containers supported to be the product of the task containers needed per
frame and the number of frames of run-ahead that the engine supports.

```
KickstartRT::D3D12::ExecuteContext_InitSettings settings = {};
settings.supportedWorkingsets = c_MaxTasksPerFrame * c_MaxFrameLookAhead; // 3 * 2
```

When a task handle ticket is no longer in use and the corresponding
command list has finished executing on the GPU, it should be returned to
the SDK.

```
// Return the taskHandle to the SDK to allow resource recycling
m_executeContext->MarkGPUTaskAsCompleted(taskHandleTicket);
```

Once the GPU task has been built, the task container will be invalid so
a new one should be created.

```
// Re-create the task container after it has been built
m_taskContainer = m_executeContext->CreateTaskContainer();
```

Kickstart RT has been designed to integrate well with a native threading
implementation and render pipeline. The execute context is threadsafe
but task containers should only have work scheduled against them 1
thread a time. As `BuildGPUTask` is a blocking operation and can have a
high cost depending on the tasks scheduled, it may be useful to execute
this on a worker thread, especially for any BVH builds.

Finally, the game engine is in full control of the execution order of
the command lists and is expected to marshall their dependencies with
fences as required. This also provides the flexibility to execute the
command lists on different queues catering for async compute work
providing fine grained control of when and where the work should be
executed.

# Debugging

Integrating a complex API always provides opportunities for bugs and
issues to occur. This section of the integration guide hopes to provide
some tips and methods for debugging the integration as well as
highlighting some of the common pitfalls.

## Debug Output Visualizations

Kickstart RT offers several built in debug features, such as the debug
output visualization modes that can be generated from a `RenderTask` with
a provided output buffer and would be a useful addition to an
integration:

```
enum class DebugOutputType : uint32_t {
	Default = 0,
	Debug_DirectLightingCache_PrimaryRays = 100,
	Debug_RandomTileColor_PrimaryRays = 101,
	Debug_RandomMeshColor_PrimaryRays = 102,
	Debug_HitT_PrimaryRays = 103,
	Debug_Barycentrics_PrimaryRays = 104,
};

// Set the debug mode
KickstartRT::D3D12::RenderTask::TraceSpecularTask renderTask;
renderTask.debugParameters.debugOutputType = KickstartRT::D3D12::RenderTask::DebugParameters::DebugOutputType::Debug_RandomTileColor_PrimaryRays;
```

## Debugging the BVHs

The meshes associated with the BVHs can be visualized by choosing
`Debug_RandomMeshColor_PrimaryRays` as the debug output mode This can be
helpful to ensure the geometry is correct and matches the location of
the geometry in the game engine. If anything is incorrect at this stage,
it is worth checking if all of the geometry transform matrices are as
expected and the vertex and index buffer strides and layouts are
correct.

## Debugging the Direct Lighting Cache

The direct lighting cache can be visualized by setting the debug output
type to be `Debug_DirectLightingCache_PrimaryRays`.

It can be helpful to initialize the direct lighting cache for each
geometry to a known fixed color, ie pink or cyan. Over a period of
frames the debug colors should be replaced with the correct lit colors
highlighting any areas that have not received any light injection.

```
// Set the initial tile cache to a known color
KickstartRT::D3D12::BVHTask::InstanceTask task;
task.input.initialTileColor[0] = 1.0f;
task.input.initialTileColor[1] = 0.0f;
task.input.initialTileColor[2] = 1.0f;
```

As a convenience, it is recommended to implement a way to clear the
direct lighting cache to check the behavior of the cache at different
times in the game level. As the cache is directly tied to the BVH of a
mesh, this requires the engine to destroy all of the Kickstart RT
geometries and instances and re-create them.

## Debugging the Raytracing

Once the BVH's are confirmed to be correct and the light injection is
working, the next step is to get the specular reflections working. To
start, the recommendation is to just use the depth and normal buffers
and not the roughness or specular buffers. The global metalness can be
set to 1.0f and the global roughness should be set to 0.0f to ensure
maximum reflections and minimum noise. This should very quickly show up
any issues in the implementation.

Any problems at this stage will be down to the depth buffer or normals,
so check that the buffer format and encoding for both is compatible with
Kickstart RT and has been correctly set.

```
enum class DepthType : uint32_t {
	RGB_WorldSpace = 0, // RGBch represents a world position
	R_ClipSpace = 1,    // Rch represents depth value in viewport transformed clip space.
};


enum class NormalType : uint32_t {
    RGB_Vector = 0,	          // RGBch represents a normal vector in XYZ.
    RGB_NormalizedVector = 1,   // RGBch represents normalized normal vector, (xyz) = (rgb) * 2.0 - 1.0.
    RG_Octahedron = 2,          // RGch represents octahedron encoded noamal vector (xyz) = ocd_decode(rg)
    BA_Octahedron = 3,          // BAch represents octahedron encoded noamal vector (xyz) = ocd_decode(ba)
    RG_NormalizedOctahedron = 4,// RGch represents normalized octahedron encoded noamal vector (xyz) = ocd_decode((rg) * 2.0 - 1.0)
    BA_NormalizedOctahedron = 5,// BAch represents normalized octahedron encoded noamal vector (xyz) = ocd_decode((ba) * 2.0 - 1.0)
};
```

A conversion pass may be required if your depth or normal formats are
not supported.

Once the basic raytracing is working with global metalness and global
roughness, the specular and roughness buffers from the gbuffer can be
applied. If used, the global metalness and roughness over-rides should
be set to 1.0f.

Another common issue is self intersection of the secondary rays at the
point of reflection and an offset is typically applied to avoid this.
Kickstart RT provides different options for the offset which should be
tried to see which is more appropriate for the game. Issues with this
can manifest as zero reflections in part of the image.

```
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
```

## Debugging the Denoiser

NRDs documentation should be referred to when trying to determine issues
and when tuning for the game, but the inputs should be tested for
correctness as the first step.Assuming the gbuffer data passed to
Kickstart RT works, some of the only differences may be with the
velocity buffers, so these should be double checked. If no velocity
buffer is provided, then NRD will try to create its own velocity vectors
which can be a useful debug aide.

The motion vector buffer can be optionally left out, this way NRD will
fall back to doing frame reprojection using the provided matrices
instead which can be a good way to verify that matrices are correctly
set up. This is not recommended for production as the reprojection will
fail for moving objects, only use this mode for debugging purposes.

If motion vectors in the velocity buffer are in the correct scale and
format, the stationary image quality should be equal when the velocity
buffer is provided and when it's not.

## Out of Resource Issues

Before any work can be done, there are some important initialisation
settings that may require modification to support a large game. A
typical setup may be :

```
Kickstart RT::D3D12::ExecuteContext_InitSettings settings = {};

settings.uploadHeapSizeForVolatileConstantBuffers = 4 * 1024 * 1024;
settings.descHeapSize = 128 * 1024;
settings.supportedWorkingsets = maxTasksPerFrame * maxFrameLookAhead; // 3 * 2 = 6
...
```

These may require tuning throughout the integration process, depending
on the scale of the game data.

A common issue is that Kickstart RT can run out of descriptors when
processing a lot of geometry. This can be modified by changing the value
of m_unboundDescTableUpperbound

in the Kickstart RT SDK source code itself.

# Optimisations and Tuning

## Surfel Type

The direct lighting cache has 2 different layouts based on regular tiles
or mesh colors. The mesh colors tend to use slightly more memory, but
allow bilinear sampling of the direct lighting cache so generates a
higher quality image and is the recommended surfel type.

The surfel type is selected when geometry is created.

```
enum class SurfelType : uint32_t {
	WarpedBarycentricStorage,
	MeshColors
};

KickstartRT::D3D12::BVHTask::GeometryTask task; 
task.input.surfelType = KickstartRT::D3D12::BVHTask::GeometryInput::SurfelType::MeshColors;
```

## Surfel Tile Size

Whether the surfel type is based on mesh colors or regular tiles, there
are some modifiers to influence the granularity of subdivision that is
used to create the direct lighting cache. A smaller surfel tile size per
primitive results in a better quality lighting cache but at the cost of
increased memory. They should be tuned to the game content.

```
KickstartRT::D3D12::BVHTask::GeometryTask task; 

task.input.tileUnitLength = c_KickStartTileUnitLength;
task.input.tileResolutionLimit = c_KickStartTileResolutionLimit;
```

These are set when geometry is created, therefore any modifications for
tuning requires the geometry to be destroyed and recreated.

## Direct Lighting Injection Parameters

```
struct DirectLightingInjectionTask : public Task {
    DirectLightingInjectionTask() : Task(Task::Type::DirectLightInjection) {};

    //< SDK accumulates direct lighting information into allocated tiles on the surfaces. 
    //< Longer average window will converge values slowly but more stable than shorter average window. 
    float    	averageWindow = 200.f;
    ...
};
```
When injecting the direct lighting into the cache, the average window
parameter can be used to set the number of frames required for the image
to converge to full brightness. A small number allows for fast reactions
to lighting updates but may produce high frequency flickering. This can
be reduced by increasing the average window to increase the temporal
stability, but the right value needs tuning for the game.

It is important to highlight that the direct lighting buffer does not
need to be native resolution. The resolution is directly linked to the
number of rays traced (1 per pixel), so a quality/performance tradeoff
is possible.

## Direct Lighting Input Reflections

The quality of specular reflections can be improved by optionally
binding the direct lighting input buffer to the common render task.

```
KickstartRT::D3D12::RenderTask::TraceTaskCommon rtTaskCommon;
rtTaskCommon.directLighting = initShaderResourceTex(pDirectLightingBuffer);
```

This could be perceived as a hybrid of raytraced reflections and screen
space reflections. When a secondary ray hits a BVH, the provided direct
lighting buffer and depth buffer are checked to see if they contain a
sample for the hit point. If they do, then the direct lighting buffer is
used directly, otherwise the value in the direct lighting cache is used.

This provides an obvious quality improvement when reflected secondary
rays hit this buffer, but the transition between the direct lighting
cache and the direct lighting input buffer can be obvious, so requires
some subjective analysis to determine its use. The recommendation is to
use the direct lighting input buffer.

## Secondary Camera Views

One of the limitations of the direct lighting cache is that it can only
cache the lighting values for surfaces that the camera has seen. Whilst
this works well in many scenarios, it is easy to imagine scenes where
this will break down, such as looking behind the player in a mirror
where the camera has not yet looked; or seeing the reflections of
ceiling lights in a reflective floor when the user has not yet looked
up.

This can be improved by implementing a secondary camera (often referred
to as a Lidar camera), at a reduced resolution, to inject supplementary
direct lighting input buffers from different views into the direct
lighting cache. Kickstart RT fully supports as many lighting input
buffers as the game wants to provide and they do not need to be at the
native resolution. We recommend 512x512 resolution as a good starting
point for performance experimentation.

![Lidar Camera](https://user-images.githubusercontent.com/100689874/158651879-8a31e5af-79dd-46e1-98fa-fd1c584c1b18.PNG)

This would be game/engine specific, but could be set up to spiral around
the player whilst rotating around the cameras X and Y axis to increase
the visible area injected into the cache. The exact flight path and
speed of this camera would need to be tailored for the environment, but
has been seen to radically improve the quality of the direct lighting
cache although there is a clear performance implication.

Combining the lidar camera with the previously discussed lighting cache
debug initialisation colors is a powerful way to optimize the lidar
camera path by highlighting unlit regions of the scene.

## Warming the Lighting Cache

Whilst loading a new scene, it may be beneficial to warm the lighting
cache using the Lidar camera mentioned above. This could be on a
predetermined path, or stochastically sampled from within the scenes
bounding volume.

## Checkerboard Support

An optimisation to reduce the cost of the RT output passes is to enable
checkerboard rendering which runs the reflection passes at a reduced
resolution. If using the NRD denoiser, this is as simple as setting a
flag on the reflection task and denoiser task so the `halfResolutionMode`
variable is enabled. This will run the reflection passes at a reduced
resolution and the NRD denoiser pass will resolve the images to the full
resolution. The engine does not need to modify any buffer sizes - it
just simply sets the flag.

Note : If the engine chooses to output demodulated specular buffers,
then these are not denoised and so will not be resolved to full
resolution automatically by NRD.

## LODs

LODs are a useful optimisation technique in raytracing, as in normal
rasterization and the LOD used in the BVH build should ideally match the
LOD used in the raster pass. This is not a hard requirement, but using
LODs will reduce the complexity of BVHs for geometry which will in turn
speed up triangle intersection testing on the GPU.

If the engine switches LODs for a specific object, then the Kickstart RT
instance for that LOD should be destroyed and a new instance created for
the new LOD. This ensures there is only 1 LOD per object in the TLAS at
a time.

The new LOD will have a cold direct lighting cache but this
will be updated to full intensity as soon as it is visible in the input
buffer.

## Asynchronous BVH Builds

A lot of CPU work typically happens when walking the scene graph each
frame looking for objects to raytrace and pass to Kickstart RT for the
BVH build and update. The CPU cost of this work can typically be hidden
by doing it in a worker thread before it is needed by the Kickstart RT
lighting pass.

There is also an associated GPU cost of building the acceleration
structures and, if supported, it is recommended to execute it early on
the GPUs asynchronous compute queue.

## Throttling BVH Builds

The `maxBlasBuildCount` parameter of the `BVHBuildTask` can be modified to
limit the number of BVH builds per frame. This is a tradeoff as a small
maximum value will result in low overhead but creates a backlog of work
which can spread over multiple frames resulting in newly built BVH
geometry slowly popping into view. A large maximum value can cause a GPU
spike when there are many BVHs to build, resulting in a frame stutter
and increased GPU memory overhead from the temporary resources used in
the BVH build process.

As this value can be updated each frame, a sensible approach would be to
find a high threshold for level load times (when lots of BVHs are built)
and a lower threshold at runtime to handle newly visible or dynamic
geometry.

A value of 100 is a good starting point for a large scene, but should be
tailored to fit the data.

```
// After generating all of the instances, build the TLAS
KickstartRT::D3D12::BVHTask::BVHBuildTask task;
task.maxBlasBuildCount = c_MaxBlasBuildsPerFrame; // 100
m_taskContainer->ScheduleBVHTask(&task);
```