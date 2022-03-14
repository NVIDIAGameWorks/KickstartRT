# KickStart RTX

1. [What's KickStartRTX](#1-whats-kickstartrtx)
2. [How it works](#2-how-it-works)
3. [Getting Started](#3-getting-started)
4. [License](#4-license)

## 1. What's KickStartRTX
This SDK aims to achieve higher quality reflection and GI rendering than screen space ones, using hardware ray-tracing functionality without having to set up shaders and shader resources for ray-tracing.  
When implementing ray-tracing into an existing game engine, one of the biggest problems is preparing the shaders for reflection and GI rays.
All of the countless shaders that exist in a game scene must be listed and configured. We also need to make sure that the various shader resources (ConstantBuffer, Texture, etc.) can be accessed correctly from those shaders for each material. This can be a very complex task.  
Instead of setting up all the shaders, this SDK takes the lighting information from a rendered G-Buffer and stores the lighting information to a cache in world space.
Therefore, the application does not need to configure shaders for ray-tracing at all.
The SDK creates reflection and GI information by sampling the lighting information using ray-tracing internally, so lighting information for objects that are off-screen will also be sampled if it is stored in the SDK, which is the biggest difference from screen space based techniques.

#### Is this a complete replacement for ray-traced reflection or GI?
No, it is not.  
First, KickStart RTX takes the lighting information from the G-Buffer and stores it in world space, but SDK doesn't take into account what components it consists of (Diffuse, Specular, Fresnel etc...). It also does not take into account the material of the surfaces.
Any surface of material that the ray hits will be evaluated as a full Lambertian, so the lighting information from the G-Buffer will be treated as radiance. This is a major compromise, as it frees the application from having to manage the materials for ray-tracing.
On the other hand, each pixel in the screen space, which is the starting point for ray-tracing, can be evaluated as a material by passing parameters such as specular and roughness as textures.
In addition, if there is a sudden change in the lighting environment or object movement in the scene, the lighting information stored in the SDK will obsolete and doesn't represent lighting well. In order to update the changed lighting, multiple G-Buffers containing the new lighting information must be continuously provided to the SDK for several frames. So there is a latency here and this is the another compromise.

## 2. How it works
For detailed information of KickStart RTX, please refer to the separate document. Here's a rough idea of how it works

1. The application passes the vertex and index buffers of the geometry in the scene to the SDK so taht it builds the BVH of the scene internally.
The SDK also receives information such as updates on geometry shapes, instance position, newly placed instances or removed, etc...  
![Slide1](https://user-images.githubusercontent.com/5753935/157593405-1a18be4e-893c-4d14-b104-773f69738ac3.png)

1. The application passes the G-buffer which contains lighting along with depth and normal to the SDK.
The SDK stores the information of the G-buffer in the lighting cache in world space. Therefore, it also receives projection matrix and view matrix information along with the G-Buffer, that are needed to reconstruct world space position.  
![Slide2](https://user-images.githubusercontent.com/5753935/157593410-c6a037ec-3d1b-4f14-a19c-9dbbf10560b4.png)

1. SDK performs raytracing internally and passes Reflection and GI results to the application.
The SDK performs raytracing based on the camera position specified by the application, renders reflection and GI using information from the lighting cache built in world space, and returns the result to the application as a texture.  
![Slide3](https://user-images.githubusercontent.com/5753935/157593412-758d200b-da90-4b69-ab25-bc75545f0dca.png)

## 3. Getting Started

Kickstart RT is designed from the ground up to provide a simple interface to common implementations of ray tracing techniques used in modern games.  It provides API support in Direct3D11/12 & Vulkan.  It can be compiled to Windows or Linux platforms including ARM instruction sets.  
Kickstart is designed to be a dynamically linked runtime library that is linked to your application at build time. We provide full source code to all parts of the library so that you may build it yourself.
In addition to browsing the sample code, we encourage you to check out the formal documentation listed here.
- [Sample code repository (KickstartRT_Demo)](https://github.com/NVIDIAGameWorks/KickstartRT_Demo)
- [SDK Reference](https://github.com/NVIDIAGameWorks/KickstartRT/docs)
- [Integration Guide](https://github.com/NVIDIAGameWorks/KickstartRT/docs)

#### Requirements
The requirements to use Kickstart RT in built from source.

##### Windows
- Cmake 3.22+  
  Tested with : 3.22.0
- Visual Studio 2019+  
  Tested with : Microsoft Visual Studio Professional 2022 (64-bit) Version 17.0.1  
- Vulkan SDK  
  Should be worked with 1.2 or higher. Tested with : 1.3.204.0 and 1.2.189.
- Windows10 SDK  
  Any recent one should be fine. Tested with : 10.0.20348.0.  
- PIX EventMarker runtime  
  If you want to enable perf merker in SDK’s render passes. Tested with : 1.0.220124001.

##### Linux
- Cmake 3.22+  
- Vulkan SDK  
  Should be worked with 1.2 or higher. Tested with 1.2.189.
- gcc/g++ 9.3 or above
- Other various libs
  - libglu1-mesa-dev
  - freeglut3-dev
  - mesa-common-dev
  - libx11-dev
  - libxrandr-dev
  - libxinerama-dev
  - libxcursor-dev
  - libxi-dev
  - libxxf86vm-dev

#### GPU Requirements
At least the SDK checks the following features that need to be supported at the initialization. It doesn’t mean that the SDK guarantees to support all GPUs that fulfills the following features.

- D3D11
  - D3D11.4  
    Need to support ID3D11Device5 to handle fence objects. 

- D3D12 and 11
  D3D11 uses D3D12 as a backend of the rendering, so the requirement is basically the same.
  - D3D12_RESOURCE_BINDING_TIER_3
  - D3D12_RAYTRACING_TIER_1_0 or greater.
    Need 1.1 for Inline Raytracing.

- Vulkan  
  Instance Extensions  
    - VK_EXT_debug_utils  

  Physical Device features  
    - VK_EXT_buffer_device_address
    - VK_KHR_acceleration_structure  
    - VK_KHR_ray_tracing_pipeline  

  Device Extensions  
    - VK_KHR_ray_tracing_pipeline  
    - VK_KHR_ray_query
      If enabling inline ray tracing.  

#### Build Steps
Kickstart is built using CMake, so the build instructions are pretty standard. 

##### Windows build instructions
1. Clone the repository
  `git clone –recursive https://github.com/NVIDIAGameWorks/KickstartRT`
2. Set up dependent libraries
  - Windows SDK
    You can download and install from [here](https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/).      
  - Vulkan SDK
    You can download and install from [here](https://www.lunarg.com/vulkan-sdk/).
    The installer will set `VULKAN_SDK` environment variable by default, and the CMake file refers it to find out the location of the SDK.
    So, please be sure to set the value properly.  
  - PIX WinPixEventRuntime
    If you want to enable PIX in the SDK's render passes, you need to install PIX runtime which can download from [here](https://www.nuget.org/packages/WinPixEventRuntime). There are two options to enable PIX in the SDK.
    - Set `PIX_EVENT_RUNTIME_SDK` environment variable with the path to the lib before configuring CMake.  
    - Set `KickstartRT_PIX_EVENT_RUNTIME_SDK_PATH` in the CMake's configuration with the path to the lib.  
3. CMake confiture and generate projects.  
  If you like, you can use CMake GUI to configure project files. You can set the destination folder anywhere but `build` directory just under the repository is preferred since it is already noted in `.gitignore`.   
4. Build  
  Open the Visual Studio solution which should be generated under `build` directory and build `ALL_BUILD` and `INSTALL`.    
  After `INSTALL`, built files will be assembled into `build/package` directory. Can then copy this package into your game engine for integration.  

##### Linux build instructions

1. Use `git clone --recursive` otherwise lots of the submodules will be missed

2. Download and install the Vulkan SDK (Might be available with apt-get). If built from sources, remember to run the `setup_env.sh` script to set up the required environment variables.

3. This has been tested with gcc 9.3 and is required for the C++17 functionality. If you have an older version of gcc installed, please update otherwise you will get compilation issues with `#include <filesystem>`
    
    `sudo apt install gcc-9 g++-9`

4. There are quite a few dependencies which are required before any samples will build/run. Here is a standard list which should provide most, depending on your own setup :
   
   `sudo apt-get install libglu1-mesa-dev freeglut3-dev mesa-common-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxxf86vm-dev`

5. Windows resource files have been emulated with some CMake magic which has restrictions when adding new shaders. The shader rc file is dynamically creating at build time, rather than build generation time, so a pre-populated resource file is included in the project which will get over-written on the first build. If a new shader is added and the project has not been built yet, then the pre-populated rc file will need updating or Kickstart RT will fail to find the new shader.

## 4. License

This project is under the MIT License, see the LICENSE.txt file for details. Also, you may need to check licenses for dependent components.

