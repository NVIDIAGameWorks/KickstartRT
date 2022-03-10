# KickStart RTX

## What's KickStartRTX?
This SDK aims to achieve higher quality reflection and GI rendering than screen space ones, using hardware ray-tracing functionality without having to set up shaders and shader resources for ray-tracing.  
When implementing ray-tracing into an existing game engine, one of the biggest problems is preparing the shaders for reflection and GI rays.
All of the countless shaders that exist in a game scene must be listed and configured. We also need to make sure that the various shader resources (ConstantBuffer, Texture, etc.) can be accessed correctly from those shaders for each material. This can be a very complex task.  
Instead of setting up all the shaders, this SDK takes the lighting information from a rendered G-Buffer and stores the lighting information to a cache in world space.
Therefore, the application does not need to configure shaders for ray-tracing at all.
The SDK creates reflection and GI information by sampling the lighting information using ray-tracing internally, so lighting information for objects that are off-screen will also be sampled if it is stored in the SDK, which is the biggest difference from screen space based techniques.

## Is this a complete replacement for ray-traced reflection or GI?
No, it is not.  
First, KickStart RTX takes the lighting information from the G-Buffer and stores it in world space, but SDK doesn't take into account what components it consists of (Diffuse, Specular, Fresnel etc...). It also does not take into account the material of the surfaces.
Any surface of material that the ray hits will be evaluated as a full Lambertian, so the lighting information from the G-Buffer will be treated as radiance. This is a major compromise, as it frees the application from having to manage the materials for ray-tracing.
On the other hand, each pixel in the screen space, which is the starting point for ray-tracing, can be evaluated as a material by passing parameters such as specular and roughness as textures.
In addition, if there is a sudden change in the lighting environment or object movement in the scene, the lighting information stored in the SDK will obsolete and doesn't represent lighting well. In order to update the changed lighting, multiple G-Buffers containing the new lighting information must be continuously provided to the SDK for several frames. So there is a latency here and this is the another compromise.

## How it works?
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

## Other references

- [SDK Reference](docs/SDK_Reference.md)

## Linux build instructions
Kickstart is built using CMake, so the build instructions are pretty standard. This section highlights some of the setup that is required for Linux that is different from Windows.

1. Use `git clone --recursive` otherwise lots of the submodules will be missed

2. Download and install the Vulkan SDK (Might be available with apt-get). If built from sources, remember to run the `setup_env.sh` script to set up the required environment variables.

3. This has been tested with gcc 9.3 and is required for the C++17 functionality. If you have an older version of gcc installed, please update otherwise you will get compilation issues with `#include <filesystem>`
    
    `sudo apt install gcc-9 g++-9`

4. There are quite a few dependencies which are required before any samples will build/run. Here is a standard list which should provide most, depending on your own setup :
   
   `sudo apt-get install libglu1-mesa-dev freeglut3-dev mesa-common-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxxf86vm-dev`

5. Windows resource files have been emulated with some CMake magic which has restrictions when adding new shaders. The shader rc file is dynamically creating at build time, rather than build generation time, so a pre-populated resource file is included in the project which will get over-written on the first build. If a new shader is added and the project has not been built yet, then the pre-populated rc file will need updating or Kickstart RT will fail to find the new shader.

## License

This project is under the MIT License, see the LICENSE.txt file for details. Also, you may need to check licenses for dependent components.

