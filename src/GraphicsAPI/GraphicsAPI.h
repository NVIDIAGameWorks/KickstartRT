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

#include "GraphicsAPI/GraphicsAPI_Utils.h"

#include <memory>
#include <array>
#include <vector>

// This is to enable RT shader stage bindings of descriptor layouts in Vulkan. 
#define USE_SHADER_TABLE_RT_SHADERS 1

namespace KickstartRT_NativeLayer::GraphicsAPI {
#if defined(GRAPHICS_API_VK)
    /***************************************************************
     * Vulkan extension function pointers.
     ***************************************************************/
    namespace VK {
#if 0
        using PFN_vkCreateAccelerationStructureKHR = VkResult (VKAPI_PTR *)(VkDevice device, const VkAccelerationStructureCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkAccelerationStructureKHR* pAccelerationStructure);
        using PFN_vkDestroyAccelerationStructureKHR = void (VKAPI_PTR *)(VkDevice device, VkAccelerationStructureKHR accelerationStructure, const VkAllocationCallbacks* pAllocator);
        using PFN_vkGetAccelerationStructureBuildSizesKHR = void (VKAPI_PTR *)(VkDevice device, VkAccelerationStructureBuildTypeKHR buildType, const VkAccelerationStructureBuildGeometryInfoKHR* pBuildInfo, const uint32_t* pMaxPrimitiveCounts, VkAccelerationStructureBuildSizesInfoKHR* pSizeInfo);
        using PFN_vkCmdBuildAccelerationStructuresKHR = void (VKAPI_PTR *)(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkAccelerationStructureBuildGeometryInfoKHR* pInfos, const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos);
        using PFN_vkSetDebugUtilsObjectNameEXT = VkResult (VKAPI_PTR*)(VkDevice device, const VkDebugUtilsObjectNameInfoEXT* pNameInfo);
        using PFN_vkCmdBeginDebugUtilsLabelEXT = void (VKAPI_PTR*)(VkCommandBuffer commandBuffer, const VkDebugUtilsLabelEXT* pLabelInfo);
        using PFN_vkCmdEndDebugUtilsLabelEXT = void (VKAPI_PTR*)(VkCommandBuffer commandBuffer);
        using PFN_vkCmdCopyAccelerationStructureKHR = void (VKAPI_PTR *)(VkCommandBuffer commandBuffer, const VkCopyAccelerationStructureInfoKHR* pInfo);
        using PFN_vkCmdWriteAccelerationStructuresPropertiesKHR = void (VKAPI_PTR *)(VkCommandBuffer commandBuffer, uint32_t accelerationStructureCount, const VkAccelerationStructureKHR* pAccelerationStructures, VkQueryType queryType, VkQueryPool queryPool, uint32_t firstQuery);
#endif

        extern PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
        extern PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR;
        extern PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR;
        extern PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR;
        extern PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT;
        extern PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT;
        extern PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT;
        extern PFN_vkCmdCopyAccelerationStructureKHR vkCmdCopyAccelerationStructureKHR;
        extern PFN_vkCmdWriteAccelerationStructuresPropertiesKHR vkCmdWriteAccelerationStructuresPropertiesKHR;
        extern PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR;
        extern PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR;
        extern PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR;

        bool GetProcAddresses(VkInstance instance);
    }
#endif

    /***************************************************************
     * Prevent from copying an object since most classes that manage raw pointers.
     ***************************************************************/
    struct Noncopyable {
    protected:
        Noncopyable() noexcept {};
        ~Noncopyable() noexcept {};

    private:
        void operator =(const Noncopyable& /* src */) noexcept {};
        Noncopyable(const Noncopyable& /* src */) noexcept {};
    };

    /***************************************************************
     * Base class for objects that are allocated with associated device in D3D12 and VK
     ***************************************************************/
    struct DeviceObject : public Noncopyable
    {
        virtual ~DeviceObject() = 0;

#if defined(GRAPHICS_API_D3D12)
        void SetNameInternal(ID3D12Object* m_object, const wchar_t* const str);
#elif defined(GRAPHICS_API_VK)
        void SetNameInternal(VkDevice dev, VkObjectType type, uint64_t objHandle, const wchar_t* const str);
#endif
    };

    /***************************************************************
     * Device in D3D12
     * Device in VK
     ***************************************************************/
    struct  Device : public Noncopyable
    {
#if defined(GRAPHICS_API_D3D12)
        struct ApiData {
            ID3D12Device5*  m_device;
        };
#elif defined(GRAPHICS_API_VK)
        struct ApiData {
            VkDevice            m_device;
            VkPhysicalDevice    m_physicalDevice;
            VkInstance          m_instance;
        };
#endif

#if defined(GRAPHICS_API_VK)
        enum class VulkanDeviceMemoryType : uint32_t
        {
            Default,
            Upload,
            Readback,

            Count
        };
        std::array<uint32_t, (size_t)VulkanDeviceMemoryType::Count>     m_deviceMemoryTypeIndex;
#endif

        ApiData   m_apiData = {};

        virtual ~Device();

        bool CreateFromApiData(const ApiData &apiData);
    };

#if 0
    /***************************************************************
     * CommandAllocator in D3D12
     * CommandPool in VK
     ***************************************************************/
    struct CommandAllocator : public DeviceObject
    {
#if defined(GRAPHICS_API_D3D12)
        struct ApiData {
            ID3D12CommandAllocator* m_allocator;
            D3D12_COMMAND_LIST_TYPE m_commandListType;
        };
        struct Type {
            D3D12_COMMAND_LIST_TYPE m_commandListType;
        };
#elif defined(GRAPHICS_API_VK)
        struct ApiData {
            VkDevice            m_device = {};
            VkCommandPool       m_commandPool = {};
            uint32_t            m_queueFamilyIndex = 0xFFFF'FFFF;
        };
        struct Type {
            uint32_t                m_queueFamilyIndex;
        };
#endif

        ApiData     m_apiData = {};
        void SetName(const std::wstring& str);

        virtual ~CommandAllocator();
        bool Create(Device* dev, const Type& type);
        bool Reset(Device* dev);
    };
#endif

    /***************************************************************
     * DescriptorHeap in D3D12
     * DescriptorPool in VK
     ***************************************************************/
    struct DescriptorTableLayout;
    struct DescriptorHeap : public DeviceObject
    {
#if defined(GRAPHICS_API_D3D12)
        struct ApiData {
            struct HeapEntry {
                ID3D12DescriptorHeap* m_descHeap = nullptr;
                uint64_t    m_incrementSize = 0;
                uint32_t    m_numDescriptors = 0;
                uint32_t    m_currentOffset = 0;
            };

            std::array<HeapEntry, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> m_heaps;
        };
        struct AllocationInfo {
            uint64_t                    m_incrementSize;
            uint32_t                    m_numDescriptors;
            D3D12_CPU_DESCRIPTOR_HANDLE m_hCPU;
            D3D12_GPU_DESCRIPTOR_HANDLE m_hGPU;
        };

#elif defined(GRAPHICS_API_VK)
        struct ApiData {
            VkDescriptorPool    m_descPool = {};
            VkDevice            m_device = {};
        };
        struct AllocationInfo {
            VkDescriptorSet     m_descSet = {};
        };
#endif

        enum class Type : uint32_t
        {
            TextureSrv,
            TextureUav,
            RawBufferSrv,
            RawBufferUav,
            TypedBufferSrv,
            TypedBufferUav,
            Cbv,
            StructuredBufferUav,
            StructuredBufferSrv,
            AccelerationStructureSrv,
            Dsv,
            Rtv,
            Sampler,

            Count
        };
        static constexpr std::underlying_type<Type>::type value(const Type& type) {
            return static_cast<std::underlying_type<Type>::type>(type);
        };
#if defined(GRAPHICS_API_D3D12)
        static constexpr D3D12_DESCRIPTOR_HEAP_TYPE nativeType(const Type &t);
#elif defined(GRAPHICS_API_VK)
        static constexpr VkDescriptorType nativeType(const Type &type);
#endif

        struct Desc
        {
            uint32_t m_descCount[static_cast<uint32_t>(Type::Count)] = {};
            uint32_t m_totalDescCount = 0;

            Desc& setDescCount(Type type, uint32_t count)
            {
                m_totalDescCount -= m_descCount[value(type)];
                m_totalDescCount += count;
                m_descCount[value(type)] = count;
                return *this;
            }
        };

        ApiData    m_apiData = {};
        Desc       m_desc = {};

        virtual ~DescriptorHeap();
        void SetName(const std::wstring& str);
        bool Create(Device* dev, const Desc& desc, bool isShaderVisible);

        bool ResetAllocation();
        bool Allocate(const DescriptorTableLayout *desctable, AllocationInfo* retAllocationInfo, uint32_t unboundDescTableCount);
    };

    /***************************************************************
     * DescriptorTableLayout(D3D12_DESCRIPTOR_RANGE) in D3D12
     * DescriptorSetLayout in VK
     ***************************************************************/
    struct DescriptorTableLayout : public DeviceObject
    {
#if defined(GRAPHICS_API_D3D12)
        struct ApiData {
            std::vector<D3D12_DESCRIPTOR_RANGE>     m_ranges;
        };
#elif defined(GRAPHICS_API_VK)
        struct ApiData {
            std::vector<VkDescriptorSetLayoutBinding>   m_bindings;
            VkDevice                m_device = {};
            VkDescriptorSetLayout   m_descriptorSetLayout = {};
        };
#endif

#if defined(GRAPHICS_API_D3D12)
        static constexpr D3D12_DESCRIPTOR_RANGE_TYPE nativeType(const DescriptorHeap::Type& type);
#endif

        struct Range
        {
            DescriptorHeap::Type    m_type = DescriptorHeap::Type::Cbv;
            uint32_t                m_baseRegIndex = 0;
            uint32_t                m_descCount = 0;
            uint32_t                m_regSpace = 0;
            uint32_t                m_offsetFromTableStart = 0xFFFF'FFFF; // set by AddRange.
        };

        bool                        m_lastUnbound = false;
        ApiData                     m_apiData = {};
        std::vector<Range>          m_ranges;

        virtual ~DescriptorTableLayout();
        void SetName(const std::wstring& str);

        void AddRange(DescriptorHeap::Type type, uint32_t baseRegIndex, int32_t descriptorCount, uint32_t regSpace, uint32_t offset = 0);
        bool SetAPIData(Device *dev);
    };

    /***************************************************************
     * DescriptorTable in D3D12
     * DescriptorSet in VK
     ***************************************************************/
    struct ShaderResourceView;
    struct UnorderedAccessView;
    struct ConstantBufferView;
    struct Sampler;
    struct DescriptorTable : public DeviceObject
    {
#if defined(GRAPHICS_API_D3D12)
        struct ApiData
        {
            DescriptorHeap::AllocationInfo          m_heapAllocationInfo;
        };
#elif defined(GRAPHICS_API_VK)
        struct ApiData {
            DescriptorHeap::AllocationInfo          m_heapAllocationInfo;
        };
#endif

        ApiData     m_apiData = {};
        const DescriptorTableLayout* m_descTableLayout = {};

        virtual ~DescriptorTable();

        bool Allocate(DescriptorHeap* descHeap, const DescriptorTableLayout* descTableLayout, uint32_t unboundDescTableCount = 0);

        bool SetSrv(Device* dev, uint32_t rangeIndex, uint32_t indexInRange, const ShaderResourceView* srv);
        bool SetUav(Device* dev, uint32_t rangeIndex, uint32_t indexInRange, const UnorderedAccessView* uav);
        bool SetCbv(Device* dev, uint32_t rangeIndex, uint32_t indexInRange, const ConstantBufferView* cbv);
        bool SetSampler(Device* dev, uint32_t rangeIndex, uint32_t indexInRange, const Sampler* smp);

        bool Copy(Device* dev, uint32_t rangeIndex, uint32_t indexInRange, DescriptorTable* descTable, uint32_t explicitCopySize = 0xFFFF'FFFF);
    };

    /***************************************************************
     * RootSignature in D3D12
     * VkPipelineLayout in VK
     ***************************************************************/
    struct RootSignature : public DeviceObject
    {
#if defined(GRAPHICS_API_D3D12)
        struct ApiData {
            ID3D12RootSignature* m_rootSignature = {};
        };
#elif defined(GRAPHICS_API_VK)
        struct ApiData {
            VkDevice            m_device;
            VkPipelineLayout    m_pipelineLayout;
        };
#endif
        ApiData       m_apiData = {};

        virtual ~RootSignature();
        void SetName(const std::wstring& str);

        bool Init(Device* dev, const std::vector<DescriptorTableLayout*>& descLayout);
    };

    /***************************************************************
     * ShaderByteCode in D3D12
     * VkShaderModule in VK
     * compute only for simplicity.
     ***************************************************************/
    struct ComputeShader : public DeviceObject
    {
#if defined(GRAPHICS_API_D3D12)
        struct ApiData {
            std::vector<uint8_t>        m_shaderByteCode;
        };
#elif defined(GRAPHICS_API_VK)
        struct ApiData {
            std::vector<uint8_t>        m_shaderByteCode;
        };
#endif
        ApiData m_apiData;

        virtual ~ComputeShader();

        bool Init(const void* shaderByteCode, size_t size);
    };

    /***************************************************************
     * ID3D12PipelineState in D3D12
     * VkPipeline in VK
     * compute only for simplicity.
     ***************************************************************/
    struct ComputePipelineState : public DeviceObject
    {
#if defined(GRAPHICS_API_D3D12)
        struct ApiData {
            ID3D12PipelineState* m_pipelineState = {};
        };
#elif defined(GRAPHICS_API_VK)
        struct ApiData {
            VkPipeline      m_pipeline;
            VkShaderModule  m_module_CS;
            VkDevice        m_device;
        };
#endif
        ApiData           m_apiData = {};

        virtual ~ComputePipelineState();
        void SetName(const std::wstring& str);

        bool Init(Device* dev, RootSignature* rootSig, ComputeShader* shader);
    };

    /***************************************************************
     * ID3D12StateObject in D3D12
     ***************************************************************/
    struct RaytracingPipelineState : public DeviceObject
    {
#if defined(GRAPHICS_API_D3D12)
        struct ApiData {
            ID3D12StateObject* m_rtPSO = {};
        };
#elif defined(GRAPHICS_API_VK)
        struct ApiData {
            VkPipeline      m_pipeline;
            VkShaderModule  m_module;
            VkDevice        m_device;
        };
#endif
        ApiData           m_apiData = {};

        virtual ~RaytracingPipelineState();
        void SetName(const std::wstring& str);
    };

    /***************************************************************
     * Abstraction for samplers.
     * D3D12_SAMPLER_DESC in D3D12
     * VkSamplers in VK
     ***************************************************************/
    struct Sampler : public DeviceObject
    {
#if defined(GRAPHICS_API_D3D12)
        struct ApiData {
            D3D12_SAMPLER_DESC m_desc = {};
        };
#elif defined(GRAPHICS_API_VK)
        struct ApiData {
            VkSampler       m_sampler;
            VkDevice        m_device;
    };
#endif

        ApiData                     m_apiData = {};

        virtual ~Sampler();
        bool CreateLinearClamp(Device *dev);
    };

    struct SubresourceRange {
        uint8_t baseArrayLayer = 0;
        uint8_t arrayLayerCount = 0;
        uint8_t baseMipLevel = 0;
        uint8_t mipLevelCount = 0;

        SubresourceRange() = default;
        SubresourceRange(uint8_t _baseArrayLayer, uint8_t _arrayLayerCount, uint8_t _baseMipLevel, uint8_t _mipLevelCount)
            : baseArrayLayer(_baseArrayLayer),
            arrayLayerCount(_arrayLayerCount),
            baseMipLevel(_baseMipLevel),
            mipLevelCount(_mipLevelCount)
        {
        }

        static uint32_t CalcSubresource(uint32_t mipSlice, uint32_t arraySlice, uint32_t MipLevels);
    };

    struct ResourceState {

        using Subresource                               = uint32_t;
        static constexpr uint32_t MaxSubresourceCount   = 16;
        static constexpr Subresource SubresourceAll     = 0xFFFFFFFF;

        constexpr void Validate() {
            static_assert((int)State::Undefined == 0, "m_state must be initialized to State::Undefined");
        }

        /** Resource state. Keeps track of how the resource was last used
        */
        enum class State : uint8_t
        {
            Undefined,
            PreInitialized,
            Common,
            VertexBuffer,
            ConstantBuffer,
            IndexBuffer,
            RenderTarget,
            UnorderedAccess,
            DepthStencil,
            ShaderResource,
            StreamOut,
            IndirectArg,
            CopyDest,
            CopySource,
            ResolveDest,
            ResolveSource,
            Present,
            GenericRead,
            Predication,
            PixelShader,
            NonPixelShader,
            AccelerationStructure,
        };

#if defined(GRAPHICS_API_D3D12)
        static State GetResourceState(D3D12_RESOURCE_STATES state);
        static D3D12_RESOURCE_STATES GetD3D12ResourceState(State state);

#elif defined(GRAPHICS_API_VK)

#endif

        void SetState(State state, Subresource subresource);
        State GetState(Subresource subresource);

        bool IsTrackingPerSubresource() const;

        // idx 0 = Global State if subresource states are not tracked.
        State       m_state[1 + MaxSubresourceCount] = { /*State::Undefined == 0 */ };
        bool m_isTrackingPerSubresource = false;
    };

    /***************************************************************
     * Abstraction for all resource types.
     * D3D12Resource in D3D12
     * VkResource in VK
     ***************************************************************/
    struct Resource : public DeviceObject
    {
        // ApiResourceID is a backend agnostic resource identifier. Useful for detecting resource aliasing.
        // For D3D12: reinterpret_cast<uint64_t>(ID3D12Resource*)
        // For VK:    reinterpret_cast<uint64_t>(VkImage)
        using ApiResourceID = uint64_t;

#if defined(GRAPHICS_API_D3D12)
        struct ApiData {
            ID3D12Resource* m_resource;
        };
#elif defined(GRAPHICS_API_VK)
        struct ApiData {
            VkDevice        m_device = {};
            VkBuffer        m_buffer = {};
            VkImage         m_image = {};

#if 0
            VkAccelerationStructureKHR  m_accelerationStructure = {};
#endif

            VkDeviceMemory  m_deviceMemory = {};
            VkDeviceAddress m_deviceAddress = {};
            uint64_t        m_deviceMemoryOffset = uint64_t(-1);
        };
#endif
        /** Resource types. Notice there are no array types. Array are controlled using the array size parameter on texture creation.
        */
        enum class Type : uint32_t
        {
            Buffer,                 ///< Buffer. Can be bound to all shader-stages
            Texture1D,              ///< 1D texture. Can be bound as render-target, shader-resource and UAV
            Texture2D,              ///< 2D texture. Can be bound as render-target, shader-resource and UAV
            Texture3D,              ///< 3D texture. Can be bound as render-target, shader-resource and UAV
            TextureCube,            ///< Texture-cube. Can be bound as render-target, shader-resource and UAV
            Texture2DMultisample,   ///< 2D multi-sampled texture. Can be bound as render-target, shader-resource and UAV
        };

        /** These flags are hints the driver to what pipeline stages the resource will be bound to.
        */
        enum class BindFlags : uint32_t
        {
            None = 0x0,                         ///< The resource will not be bound the pipeline. Use this to create a staging resource
            Vertex = 0x1,                       ///< The resource will be bound as a vertex-buffer
            Index = 0x2,                        ///< The resource will be bound as a index-buffer
            Constant = 0x4,                     ///< The resource will be bound as a constant-buffer
            StreamOutput = 0x8,                 ///< The resource will be bound to the stream-output stage as an output buffer
            ShaderResource = 0x10,              ///< The resource will be bound as a shader-resource
            UnorderedAccess = 0x20,             ///< The resource will be bound as an UAV
            RenderTarget = 0x40,                ///< The resource will be bound as a render-target
            DepthStencil = 0x80,                ///< The resource will be bound as a depth-stencil buffer
            IndirectArg = 0x100,                ///< The resource will be bound as an indirect argument buffer
            Shared = 0x200,                     ///< The resource will be shared with a different adapter. Mostly useful for sharing resoures with CUDA
            AllowShaderAtomics = 0x400,         ///< The resource will be bound as buffer used with shader atomics
            AccelerationStructureBuildInput = 0x800, ///< The resource will be used as an input of build acceleration structure.
            ShaderDeviceAddress = 0x40000000,   ///< The resource will be bound as a buffer with its device address (e.g. AS, scratch buffer), Vulkan need to define this when buffer creation.
            AccelerationStructure = 0x80000000, ///< The resource will be bound as an acceleration structure

            AllColorViews = ShaderResource | UnorderedAccess | RenderTarget,
            AllDepthViews = ShaderResource | DepthStencil
        };

        /** Resource formats
        */
        enum class Format : uint32_t
        {
            Unknown,
            R8Unorm,
            R8Snorm,
            R16Unorm,
            R16Snorm,
            RG8Unorm,
            RG8Snorm,
            RG16Unorm,
            RG16Snorm,
            RGB16Unorm,
            RGB16Snorm,
            R24UnormX8,
            RGB5A1Unorm,
            RGBA8Unorm,
            RGBA8Snorm,
            RGB10A2Unorm,
            RGB10A2Uint,
            RGBA16Unorm,
            RGBA8UnormSrgb,
            R16Float,
            RG16Float,
            RGB16Float,
            RGBA16Float,
            R32Float,
            R32FloatX32,
            RG32Float,
            RGB32Float,
            RGBA32Float,
            R11G11B10Float,
            RGB9E5Float,
            R8Int,
            R8Uint,
            R16Int,
            R16Uint,
            R32Int,
            R32Uint,
            RG8Int,
            RG8Uint,
            RG16Int,
            RG16Uint,
            RG32Int,
            RG32Uint,
            RGB16Int,
            RGB16Uint,
            RGB32Int,
            RGB32Uint,
            RGBA8Int,
            RGBA8Uint,
            RGBA16Int,
            RGBA16Uint,
            RGBA32Int,
            RGBA32Uint,

            BGRA8Unorm,
            BGRA8UnormSrgb,

            BGRX8Unorm,
            BGRX8UnormSrgb,
            Alpha8Unorm,
            Alpha32Float,
            R5G6B5Unorm,

            // Depth-stencil
            D32Float,
            D16Unorm,
            D32FloatS8X24,
            D24UnormS8,

            // Compressed formats
            BC1Unorm,   // DXT1
            BC1UnormSrgb,
            BC2Unorm,   // DXT3
            BC2UnormSrgb,
            BC3Unorm,   // DXT5
            BC3UnormSrgb,
            BC4Unorm,   // RGTC Unsigned Red
            BC4Snorm,   // RGTC Signed Red
            BC5Unorm,   // RGTC Unsigned RG
            BC5Snorm,   // RGTC Signed RG
            BC6HS16,
            BC6HU16,
            BC7Unorm,
            BC7UnormSrgb,

            Count
        };

        enum class FormatType : uint32_t
        {
            Unknown,        ///< Unknown format Type
            Float,          ///< Floating-point formats
            Unorm,          ///< Unsigned normalized formats
            UnormSrgb,      ///< Unsigned normalized SRGB formats
            Snorm,          ///< Signed normalized formats
            Uint,           ///< Unsigned integer formats
            Sint            ///< Signed integer formats
        };

        struct FormatDesc
        {
            Format              m_format;
            const std::string   m_name;
            uint32_t            m_bytesPerBlock;
            uint32_t            m_channelCount;
            FormatType          m_type;

            bool isDepth;
            bool isStencil;
            bool isCompressed;

            struct
            {
                uint32_t width;
                uint32_t height;
            } compressionRatio;
            int numChannelBits[4];
        };
        static const std::array<FormatDesc, (uint32_t)Format::Count>     m_formatDescs;

        static inline uint32_t GetFormatBytesPerBlock(Format format)
        {
            return m_formatDescs[(uint32_t)format].m_bytesPerBlock;
        }
        static inline uint32_t GetChannelCount(Format format)
        {
            return m_formatDescs[(uint32_t)format].m_channelCount;
        }
        static inline FormatType GetFormatType(Format format)
        {
            return m_formatDescs[(uint32_t)format].m_type;
        }
        static inline bool IsDepthFormat(Format format)
        {
            return m_formatDescs[(uint32_t)format].isDepth;
        }
        static inline bool IsStencilFormat(Format format)
        {
            return m_formatDescs[(uint32_t)format].isStencil;
        }
        static inline bool IsDepthStencilFormat(Format format)
        {
            return IsDepthFormat(format) || IsStencilFormat(format);
        }

        static constexpr uint32_t CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT = 256;
        static uint32_t ConstantBufferPlacementAlignment(uint32_t sizeInBytes) { return ALIGN(CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, sizeInBytes); };
        static uint64_t ConstantBufferPlacementAlignment(uint64_t sizeInBytes) { return ALIGN((uint64_t)CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, sizeInBytes); };

        static constexpr uint64_t DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT = 65536;
        static uint64_t DefaultResourcePlacementAlignment(uint64_t sizeInBytes) { return ALIGN(DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, sizeInBytes); };

        static bool IsTexture(Type type);
        static bool IsBuffer(Type type);

#if defined(GRAPHICS_API_D3D12)
        struct DxgiFormatDesc
        {
            Format      m_falcorFormat;
            DXGI_FORMAT m_dxgiFormat;
        };
        static const std::array<DxgiFormatDesc, (uint32_t)Format::Count> m_DxgiFormatDesc;

        static inline DXGI_FORMAT GetDxgiFormat(Format format)
        {
            //assert(m_DxgiFormatDesc[(uint32_t)format].m_falcorFormat == format);
            return m_DxgiFormatDesc[(uint32_t)format].m_dxgiFormat;
        }

        static inline Format GetResourceFormat(DXGI_FORMAT format)
        {
            for (size_t i = 0; i < (size_t)Format::Count; ++i)
            {
                const auto& desc = m_DxgiFormatDesc[i];
                if (desc.m_dxgiFormat == format) return desc.m_falcorFormat;
            }

            return Format::Unknown;
        }

        static DXGI_FORMAT GetTypelessFormat(Format format);
        static D3D12_RESOURCE_FLAGS GetD3D12ResourceFlags(BindFlags flags);
        static BindFlags GetBindFlags(D3D12_RESOURCE_FLAGS flags);

        static Type GetResourceType(D3D12_RESOURCE_DIMENSION dim);
        static D3D12_RESOURCE_DIMENSION GetResourceDimension(Type type);

        static const D3D12_HEAP_PROPERTIES m_defaultHeapProps;
        static const D3D12_HEAP_PROPERTIES m_readbackHeapProps;
        static const D3D12_HEAP_PROPERTIES m_uploadHeapProps;
#elif defined(GRAPHICS_API_VK)
        struct VkFormatDesc
        {
            Format      m_falcorFormat;
            VkFormat    m_vkFormat;
        };
        static const std::array<VkFormatDesc, (uint32_t)Format::Count> m_VkFormatDesc;

        static inline VkFormat GetVkFormat(Format format)
        {
            return m_VkFormatDesc[(uint32_t)format].m_vkFormat;
        }

        static inline Format GetResourceFormat(VkFormat format)
        {
            for (size_t i = 0; i < (size_t)Format::Count; ++i)
            {
                const auto& desc = m_VkFormatDesc[i];
                if (desc.m_vkFormat == format) return desc.m_falcorFormat;
            }

            return Format::Unknown;
        }

        static VkBufferUsageFlags GetBufferUsageFlag(Resource::BindFlags bindFlags);
        static VkImageUsageFlags GetImageUsageFlag(Resource::BindFlags bindFlags);
        static VkImageType GetVkImageType(Resource::Type type);
        static Resource::Type GetImageType(VkImageViewType type);
        static VkImageLayout GetVkImageLayout(ResourceState::State state);
        static VkAccessFlagBits GetVkAccessMask(ResourceState::State state);
        static VkImageAspectFlags GetVkImageAspectFlags(Resource::Format format, bool ignoreStencil = false);
        static VkPipelineStageFlags GetVkPipelineStageMask(ResourceState::State state, bool src);


        bool AllocateDeviceMemory(Device* dev, Device::VulkanDeviceMemoryType memType, uint32_t memoryTypeBits, bool enableDeviceAddress, size_t size, VkDeviceMemory* mem);
#endif


        bool        m_destructWithDestructor = true;
        ApiData     m_apiData = {};
        Type        m_type = Type::Buffer;
        BindFlags   m_bindFlags = BindFlags::None;
        uint32_t    m_subresourceCount = 1;
        // TODO: this should be part of the command list. Not per resources.
        ResourceState m_globalState;

        ApiResourceID GetApiResourceID() const;

        void SetGlobalState(ResourceState::State state, ResourceState::Subresource subresource = ResourceState::SubresourceAll);
        ResourceState::State GetGlobalState(ResourceState::Subresource subresource = ResourceState::SubresourceAll);

        virtual ~Resource();
        void SetName(const std::wstring& str);
    };
    enum_class_operators(Resource::BindFlags);

    /***************************************************************
     * Abstraction for texture resources.
     ***************************************************************/
    struct Texture : public Resource
    {
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        uint32_t m_depth = 0;
        uint32_t m_mipLevels = 0;
        uint32_t m_sampleCount = 0;
        uint32_t m_arraySize = 0;
        Resource::Format m_format = Resource::Format::Unknown;

        bool Create(Device *dev, Resource::Type type, Resource::Format format, Resource::BindFlags bindFlags,
            uint32_t width, uint32_t height, uint32_t depth, uint32_t arraySize, uint32_t mipLevels, uint32_t sampleCount);

#if defined(GRAPHICS_API_D3D12)
        bool InitFromApiData(ApiData apiData, ResourceState::State state);
#elif defined(GRAPHICS_API_VK)
        bool InitFromApiData(VkDevice device,
            VkImage image,
            VkImageViewType imageViewType,
            VkFormat format,
            uint32_t mipCount,
            uint32_t layerCount,
            ResourceState::State state);
#endif
        bool GetUploadBufferFootplint(Device *dev, uint32_t subresourceIndex, uint32_t* rowPitchInBytes, uint32_t* totalSizeInBytes);

        virtual ~Texture();
    };

    /***************************************************************
     * Abstraction for texture resources.
     ***************************************************************/
    struct Heap;
    struct Buffer : public Resource
    {
        enum class CpuAccess : uint32_t
        {
            None,    ///< The CPU can't access the buffer's content. The buffer can be updated using Buffer#updateData()
            Write,   ///< The buffer can be mapped for CPU writes
            Read,    ///< The buffer can be mapped for CPU reads
        };

        enum class MapType : uint32_t
        {
            Read,           ///< Map the buffer for read access.
            Write,          ///< Map the buffer for write access. Buffer had to be created with CpuAccess::Write flag.
            WriteDiscard,   ///< Map the buffer for write access, discarding the previous content of the entire buffer. Buffer had to be created with CpuAccess::Write flag.
        };

        CpuAccess           m_cpuAccess = CpuAccess::None;
        Resource::Format    m_format = Resource::Format::Unknown;
        uint32_t            m_elementCount = 0;
        uint32_t            m_structSizeInBytes = 0;
        uint64_t            m_sizeInBytes = 0;

        bool Create(Device* dev,
            uint64_t sizeInBytesOrNumberOfElements, Resource::Format format,
            Heap* heap, uint64_t heapOffsetInBytes, uint64_t heapAllocatedSizeInByte,
            Resource::BindFlags bindFlags,
            CpuAccess cpuAccess);

        bool Create(Device* dev,
            uint64_t sizeInBytesOrNumberOfElements, Resource::Format format,
            Resource::BindFlags bindFlags,
            CpuAccess cpuAccess);

#if 0
        // There is no needs for structured for now.
        bool CreateStructured(Device* dev, uint32_t structSize, uint32_t elementCount,
            Resource::BindFlags bindFlags = Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess,
            CpuAccess cpuAccess = Buffer::CpuAccess::None);
#endif

#if defined(GRAPHICS_API_D3D12)
        bool CreateInternal(Device* dev, const D3D12_HEAP_PROPERTIES& heapProps);
#endif

        uint64_t GetGpuAddress() const;

        void* Map(Device *dev, Buffer::MapType type, uint32_t subResourceIndex, uint64_t readRangeBegin, uint64_t readRangeEnd);
        void Unmap(Device *dev, uint32_t subResourceIndex, uint64_t writeRangeBegin, uint64_t writeRangeEnd);

        virtual ~Buffer();
    };

    /***************************************************************
     * Abstraction for heaps.
     * D3D12Heap in D3D12
     * VkDeviceMemory in VK
     ***************************************************************/
    struct Heap : public DeviceObject
    {
#if defined(GRAPHICS_API_D3D12)
        struct ApiData {
            ID3D12Heap* m_heap = {};
        };
#elif defined(GRAPHICS_API_VK)
        struct ApiData {
            VkDevice        m_device = {};
            VkDeviceMemory  m_deviceMemory = {};
        };
#endif

        ApiData                     m_apiData = {};
        size_t                      m_sizeInBytes = 0;
        Buffer::CpuAccess           m_cpuAccess = Buffer::CpuAccess::None;

        bool Create(Device* dev,
            uint64_t sizeInBytes,
            Buffer::CpuAccess cpuAccess = Buffer::CpuAccess::None);

        virtual ~Heap();
    };

    struct ShaderResourceView : public DeviceObject
    {
#if defined(GRAPHICS_API_D3D12)
        struct ApiData {
            ID3D12Resource*                 m_resource;
            D3D12_SHADER_RESOURCE_VIEW_DESC m_desc;
        };
#elif defined(GRAPHICS_API_VK)
        struct ApiData {
            VkDevice            m_device = {};
            VkBuffer            m_rawBuffer = {};

            VkAccelerationStructureKHR m_accelerationStructure = {};

            bool            m_isTypedBufferView = false;
            VkBufferView    m_typedBufferView = {};
            VkImageView     m_imageView = {};
            uint64_t        m_rawOffsetInBytes;
            uint64_t        m_rawSizeInBytes;
        };
#endif
        ApiData                     m_apiData = {};
        bool                        m_isNullView = true;
        Resource::Type              m_nullViewType = Resource::Type::Buffer;
        bool                        m_nullIsArray = false;
        bool                        m_nullIsTypedBuffer = false;

        virtual ~ShaderResourceView();

#if defined(GRAPHICS_API_D3D12)
        void InitFromApiData(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc);
        bool InitNullView(Resource::Type type, bool isArray);
#elif defined(GRAPHICS_API_VK)
        void InitFromApiData(VkBuffer rawBuffer, uint64_t rawOffsetInBytes, uint64_t rawSizeInBytes);
        bool InitFromApiData(Device* dev, VkBuffer typedBuffer, VkFormat nativeFmt, uint64_t offsetInBytes, uint64_t sizeInBytes);
        bool InitFromApiData(Device* dev, VkImage image, VkImageViewType imageType, VkFormat fmt, VkImageAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t mipCount, uint32_t baseArrayLayer, uint32_t layerCount);
        bool InitNullView(Device* dev, Resource::Type type, Resource::Format fmt, bool isArray);
#endif

        bool Init(Device* dev, Texture *tex, uint32_t mostDetailedMip, uint32_t mipCount, uint32_t firstArraySlice, uint32_t arraySize);
        bool Init(Device* dev, Texture *tex);
        
        bool Init(Device *dev, Buffer *buf, uint32_t firstElement, uint32_t elementCount);
        bool Init(Device *dev, Buffer *buf);
    };

    struct UnorderedAccessView : public DeviceObject
    {
#if defined(GRAPHICS_API_D3D12)
        struct ApiData {
            ID3D12Resource*                 m_resource;
            D3D12_UNORDERED_ACCESS_VIEW_DESC m_desc;
        };
#elif defined(GRAPHICS_API_VK)
        struct ApiData {
            VkDevice            m_device = {};
            VkBuffer            m_rawBuffer = {};

            VkAccelerationStructureKHR m_accelerationStructure = {};

            bool            m_isTypedBufferView = false;
            VkBufferView    m_typedBufferView = {};
            VkImageView     m_imageView = {};
            uint64_t        m_rawOffsetInBytes;
            uint64_t        m_rawSizeInBytes;
        };
#endif
        ApiData         m_apiData = {};
        bool            m_isNullView = true;
        Resource::Type  m_nullViewType = Resource::Type::Buffer;
        bool            m_nullIsArray = false;
        bool            m_nullIsTypedBuffer = false;

        virtual ~UnorderedAccessView();

#if defined(GRAPHICS_API_D3D12)
        void InitFromApiData(ID3D12Resource *resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc);
        bool InitNullView(Resource::Type type, bool isArray);
#elif defined(GRAPHICS_API_VK)
        bool InitFromApiData(Device* dev, VkImage image, VkImageViewType imageType, VkFormat fmt, VkImageAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t baseArrayLayer, uint32_t layerCount);
        bool InitNullView(Device* dev, Resource::Type type, Resource::Format fmt, bool isArray);
#endif

        bool Init(Device* dev, Texture *tex, uint32_t mipLevel, uint32_t firstArraySlice, uint32_t arraySize);
        bool Init(Device* dev, Texture *tex);

        bool Init(Device* dev, Buffer *buf, uint32_t firstElement, uint32_t elementCount);
        bool Init(Device* dev, Buffer *buf);
    };

    struct ConstantBufferView
    {
#if defined(GRAPHICS_API_D3D12)
        struct ApiData {
            ID3D12Resource*         m_resource;
            D3D12_CONSTANT_BUFFER_VIEW_DESC m_desc;
        };
#elif defined(GRAPHICS_API_VK)
        struct ApiData {
            VkBuffer            m_buffer = {};
            uint64_t            m_offsetInBytes;
            uint64_t            m_sizeInBytes;
        };
#endif
        ApiData      m_apiData = {};

        bool Init(Buffer *buf, uint64_t offsetInBytes, uint32_t sizeInBytes);
        bool Init(Buffer *buf);
    };

    /***************************************************************
     * CommandList in D3D12
     * CommandBuffer in VK
     ***************************************************************/
    struct CommandList : public DeviceObject
    {
#if defined(GRAPHICS_API_D3D12)
        struct ApiData {
            ID3D12GraphicsCommandList4* m_commandList;
            ID3D12DebugCommandList1* m_debugCommandList;
        };
#elif defined(GRAPHICS_API_VK)
        struct ApiData {
            VkDevice        m_device;
            VkCommandBuffer m_commandBuffer;
        };
#endif

        ApiData             m_apiData = {};

        virtual ~CommandList();
        void SetName(const std::wstring& str);

#if defined(GRAPHICS_API_D3D12)
        bool InitFromAPIData(ID3D12GraphicsCommandList4* cmdlist, ID3D12DebugCommandList1* dbgCmdList);
#elif defined(GRAPHICS_API_VK)
        bool InitFromAPIData(VkDevice device, VkCommandBuffer cmdBuf);
#endif
        void ClearState();

        bool SetDescriptorHeap(DescriptorHeap* heap);
        bool HasDebugCommandList() const;
        bool AssertResourceStates(Resource** resArr, SubresourceRange* subresourceArr, size_t numRes, ResourceState::State* statesToAssert);
#if defined(GRAPHICS_API_D3D12)
        bool AssertResourceStates(ID3D12Resource** resArr, size_t numRes, const D3D12_RESOURCE_STATES* statesToAssert);
#endif
        bool ResourceTransitionBarrier(Resource** resArr, size_t numRes, ResourceState::State* desiredStates);
        bool ResourceTransitionBarrier(Resource** resArr, SubresourceRange* subresourceArr, size_t numRes, ResourceState::State* desiredStates);
        bool ResourceUAVBarrier(Resource** resArr, size_t numRes);
        bool CopyTextureSingleMip(Device* dev, uint32_t mipIndex, Texture* dstTex, Buffer* srcUpBuf);
        void CopyBufferRegion(Buffer* dst, uint64_t dstOffset, Buffer* src, uint64_t srcOffset, uint64_t copySizeInBytes);
        void CopyTextureRegion(Texture* dst, Texture* src);
        void CopyResource(Texture* dst, Texture* src);

        void SetComputeRootDescriptorTable(RootSignature* rootSig, uint32_t baseSlotIndex, DescriptorTable **tables, size_t numTables);

        // VK has different binding point.
        void SetRayTracingRootDescriptorTable(RootSignature* rootSig, uint32_t baseSlotIndex, DescriptorTable** tables, size_t numTables);
        // vk need rootSig(pipelineLayout), 
        void SetComputeRootSignature(RootSignature *rootSig); // nothing to do in vk??
        void SetComputePipelineState(ComputePipelineState *pso);
        void SetRayTracingPipelineState(RaytracingPipelineState* rtPSO);

        void Dispatch(uint32_t x, uint32_t y, uint32_t z); //vkCmdDispatch
        void BeginEvent(const std::array<uint32_t, 3>& color, const std::string& str);
        void EndEvent();
    };

    namespace Utils
    {
        struct ScopedEventObject
        {
            CommandList*    m_cmdList = {};

            ScopedEventObject(CommandList* cmdList, const std::array<uint32_t, 3>& color, const std::string &str);
            ~ScopedEventObject();
        };

#if defined(GRAPHICS_API_D3D12)
        D3D12_SHADER_RESOURCE_VIEW_DESC BufferResourceViewDesc_R32F(UINT64 firstElm, UINT numElm);
        D3D12_SHADER_RESOURCE_VIEW_DESC BufferResourceViewDesc_R32U(UINT64 firstElm, UINT numElm);
        D3D12_SHADER_RESOURCE_VIEW_DESC BufferResourceViewDesc_R16U(UINT64 firstElm, UINT numElm);
        D3D12_UNORDERED_ACCESS_VIEW_DESC BufferAccessViewDesc_R32F(UINT64 firstElm, UINT numElm);
        D3D12_UNORDERED_ACCESS_VIEW_DESC BufferAccessViewDesc_R32U(UINT64 firstElm, UINT numElm);
#endif
    };


    /***************************************************************
     * QueryPool in VK 
     * Not abstracted at all. Just for resource destruction.
     ***************************************************************/
#if defined(GRAPHICS_API_VK)
    struct QueryPool_VK : public DeviceObject
    {
        struct ApiData {
            VkDevice        m_device = {};
            VkQueryPool     m_queryPool = {};
        };

        struct InitInfo {
            VkQueryPoolCreateFlags  m_createFlags;
            VkQueryType             m_queryType;
            uint32_t                m_poolSize;
        };

        ApiData     m_apiData = {};

        virtual ~QueryPool_VK();
        bool Create(Device* dev, const InitInfo& initInfo);
    };
#endif
};

