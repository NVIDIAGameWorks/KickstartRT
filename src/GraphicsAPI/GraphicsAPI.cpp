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
#include "GraphicsAPI/GraphicsAPI.h"

#include <assert.h>
#include <cstring>

namespace KickstartRT_NativeLayer::GraphicsAPI {

#if defined(GRAPHICS_API_VK)
    /***************************************************************
     * Loading vulkan extension function pointers.
     ***************************************************************/
    namespace VK {
        PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = {};
        PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = {};
        PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = {};
        PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = {};
        PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT = {};
        PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT = {};
        PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT = {};
        PFN_vkCmdCopyAccelerationStructureKHR vkCmdCopyAccelerationStructureKHR = {};
        PFN_vkCmdWriteAccelerationStructuresPropertiesKHR vkCmdWriteAccelerationStructuresPropertiesKHR = {};

        PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR = {};
        PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR = {};
        PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR = {};

        bool GetProcAddresses(VkInstance instance)
        {
#define GPA(VAR) \
            VAR = PFN_##VAR(vkGetInstanceProcAddr(instance, #VAR)); \
            if (VAR == nullptr) { \
                return false; \
            };

            GPA(vkCreateAccelerationStructureKHR);
            GPA(vkDestroyAccelerationStructureKHR);
            GPA(vkGetAccelerationStructureBuildSizesKHR);
            GPA(vkCmdBuildAccelerationStructuresKHR);
            GPA(vkSetDebugUtilsObjectNameEXT);
            GPA(vkCmdBeginDebugUtilsLabelEXT);
            GPA(vkCmdEndDebugUtilsLabelEXT);
            GPA(vkCmdCopyAccelerationStructureKHR);
            GPA(vkCmdWriteAccelerationStructuresPropertiesKHR);
            GPA(vkGetRayTracingShaderGroupHandlesKHR);
            GPA(vkCreateRayTracingPipelinesKHR);
            GPA(vkCmdTraceRaysKHR);
#undef GPA
            return true;
        };
    }
#endif

    /***************************************************************
     * Base class for objects that are allocated with associated device in D3D12 and VK
     ***************************************************************/
    DeviceObject::~DeviceObject()
    {
    };

    /***************************************************************
     * Setting debug name. VK need a object type to give a name.
     ***************************************************************/
#if defined(GRAPHICS_API_D3D12)
    void DeviceObject::SetNameInternal(ID3D12Object *obj, const wchar_t * const str)
    {
        if (str != nullptr)
            obj->SetName(str);
    }
#elif defined(GRAPHICS_API_VK)
    void DeviceObject::SetNameInternal(VkDevice dev, VkObjectType type, uint64_t objHandle, const wchar_t* const str)
    {
#if !defined(WIN32)
        std::wstring wstr = str;
        if (wstr.empty())
            return;

        std::string s = std::string(wstr.begin(), wstr.end());

        VkDebugUtilsObjectNameInfoEXT info = {};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        info.objectType = type;
        info.objectHandle = objHandle;
        info.pObjectName = s.c_str();
        VK::vkSetDebugUtilsObjectNameEXT(dev, &info);

#else
        size_t sLen = wcsnlen_s(str, 4096);
        if (sLen == 0)
            return;

        VkDebugUtilsObjectNameInfoEXT info = {};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        info.objectType = type;
        info.objectHandle = objHandle;

        std::string sBuf;
        {
		    int size_needed = WideCharToMultiByte(CP_UTF8, 0, str, (int)sLen, NULL, 0, NULL, NULL);
		    sBuf.resize(size_needed, 0);
		    WideCharToMultiByte(CP_UTF8, 0, str, (int)sLen, sBuf.data(), size_needed, NULL, NULL);
		    info.pObjectName = sBuf.data();
        }
        VK::vkSetDebugUtilsObjectNameEXT(dev, &info);
#endif
    }
#endif

    /***************************************************************
     * Device in D3D12
     * Device in VK
     ***************************************************************/
#if defined(GRAPHICS_API_D3D12)
    bool Device::CreateFromApiData(const Device::ApiData &apiData)
    {
        if (m_apiData.m_device) {
            Log::Fatal(L"Device is already in use.");
            return false;
        }
        HRESULT hr = apiData.m_device->QueryInterface(IID_PPV_ARGS(&m_apiData.m_device));
        if (FAILED(hr)) {
            Log::Fatal(L"Invalid D3D12 device detected.");
            return false;
        }

        return true;
    }

    Device::~Device()
    {
        if (m_apiData.m_device)
            m_apiData.m_device->Release();
        m_apiData = {};
    }
#elif defined(GRAPHICS_API_VK)
    bool Device::CreateFromApiData(const Device::ApiData &data)
    {
        if (m_apiData.m_device || m_apiData.m_physicalDevice || m_apiData.m_instance) {
            Log::Fatal(L"Device is already in use.");
            return false;
        }
        if ((!data.m_device) || (!data.m_physicalDevice) || (!data.m_instance)) {
            Log::Fatal(L"Provided vkInstance, vkDevice or vkPhysicalDevice was null.");
            return false;
        }
        m_apiData = data;

        if (!VK::GetProcAddresses(m_apiData.m_instance)) {
            Log::Fatal(L"Faild to load proc addresses of Vulkan extensions.");
            return false;
        }

        {
            VkPhysicalDeviceMemoryProperties memProperties;
            vkGetPhysicalDeviceMemoryProperties(m_apiData.m_physicalDevice, &memProperties);

            for (uint32_t i = 0; i < (uint32_t)VulkanDeviceMemoryType::Count; ++i) {
                VulkanDeviceMemoryType t = (VulkanDeviceMemoryType)i;

                VkMemoryPropertyFlags flagBits = {};
                switch (t) {
                case VulkanDeviceMemoryType::Default:
                    flagBits = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                    break;
                case VulkanDeviceMemoryType::Upload:
                    flagBits = VkMemoryPropertyFlagBits(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                    break;
                case VulkanDeviceMemoryType::Readback:
                    // On Quadro RTX 6000, there is not a memory type with coherent and cached. Unsure if this is right.         
                    //flagBits = VkMemoryPropertyFlagBits(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
                    flagBits = VkMemoryPropertyFlagBits(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
                    break;
                default:
                    assert(0);
                    break;
                }

#if 0
                uint32_t bits = 0;
                for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
                {
                    if ((memProperties.memoryTypes[i].propertyFlags & flagBits) == flagBits)
                    {
                        bits |= (1 << i);
                    }
                }

                m_deviceMemoryType[i] = bits;
#else
                uint32_t idx = 0xFFFF'FFFF;
                for (uint32_t j = 0; j < memProperties.memoryTypeCount; ++j)
                {
                    if (memProperties.memoryTypes[j].propertyFlags == flagBits) {
                        idx = j;
                        break;
                    }
                }
                if (idx == 0xFFFF'FFFF) {
                    // 2nd candidate is which meets the requirement but not ideal.
                    for (uint32_t j = 0; j < memProperties.memoryTypeCount; ++j)
                    {
                        if ((memProperties.memoryTypes[j].propertyFlags & flagBits) == flagBits) {
                            idx = j;
                            break;
                        }
                    }
                    if (idx == 0xFFFF'FFFF) {
                        Log::Fatal(L"Faild to find PhysicalDeviceMemoryProperty.");
                        return false;
                    }
                }
                m_deviceMemoryTypeIndex[i] = idx;
#endif
            }
        }

        return true;
    }

    Device::~Device()
    {
        // do not destruct vkDevice here since it is owned by application side.
        m_apiData = {};
    }

#endif

#if 0
    /***************************************************************
     * CommandAllocator in D3D12
     * CommandPool in VK
     ***************************************************************/
#if defined(GRAPHICS_API_D3D12)
    CommandAllocator::~CommandAllocator()
    {
        if (m_apiData.m_allocator) {
            m_apiData.m_allocator->Release();
        }
        m_apiData = {};
    }

    void CommandAllocator::SetName(const std::wstring& str)
    {
        SetNameInternal(m_apiData.m_allocator, str.c_str());
    }

    bool CommandAllocator::Create(Device* dev, const CommandAllocator::Type& type)
    {
        if (m_apiData.m_allocator) {
            Log::Fatal(L"CommandAllocator is already in use.");
            return false;
        }

        if (FAILED(dev->m_apiData.m_device->CreateCommandAllocator(type.m_commandListType, IID_PPV_ARGS(&m_apiData.m_allocator))))
        {
            Log::Fatal(L"Failed to create command allocator");
            return false;
        }
        m_apiData.m_commandListType = type.m_commandListType;

        return true;
    }

    bool CommandAllocator::Reset(Device* /*dev*/)
    {
        if (FAILED(m_apiData.m_allocator->Reset())) {
            Log::Fatal(L"Failed to reset command allocator.");
            return false;
        }
        return true;
    };

#elif defined(GRAPHICS_API_VK)
    CommandAllocator::~CommandAllocator()
    {
        if (m_apiData.m_device && m_apiData.m_commandPool)
            vkDestroyCommandPool(m_apiData.m_device, m_apiData.m_commandPool, nullptr);
        m_apiData = {};
    }

    void CommandAllocator::SetName(const std::wstring& str)
    {
        SetNameInternal(m_apiData.m_device, VkObjectType::VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)m_apiData.m_commandPool, str.c_str());
    }

    bool CommandAllocator::Create(Device* dev, const CommandAllocator::Type& type)
    {
		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(dev->m_apiData.m_physicalDevice, &queueFamilyCount, nullptr);
		//std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		//vkGetPhysicalDeviceQueueFamilyProperties(dev->m_apiData.m_physicalDevice, &queueFamilyCount, queueFamilies.data());

		if (type.m_queueFamilyIndex >= queueFamilyCount) {
			Log::Fatal(L"Invalid queue family index detected: index:%d numQFamilies:%d ", type.m_queueFamilyIndex, queueFamilyCount);
			return false;
		}

        VkCommandPoolCreateInfo commandPoolCreateInfo = {};
        commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        commandPoolCreateInfo.queueFamilyIndex = type.m_queueFamilyIndex;

        if (vkCreateCommandPool(dev->m_apiData.m_device, &commandPoolCreateInfo, nullptr, &m_apiData.m_commandPool) != VK_SUCCESS) {
            Log::Fatal(L"Faild to create a command pool");
            return false;
        }
        m_apiData.m_queueFamilyIndex = type.m_queueFamilyIndex;
        m_apiData.m_device = dev->m_apiData.m_device;

        return true;
    }

    bool CommandAllocator::Reset(Device* dev)
    {
        if (vkResetCommandPool(dev->m_apiData.m_device, m_apiData.m_commandPool, 0) != VK_SUCCESS) {
            Log::Fatal(L"Faild to reset a command pool");
            return false;
        }

        return true;
    }
#endif
#endif

    /***************************************************************
     * DescriptorHeap in D3D12
     * DescriptorPool in VK
     ***************************************************************/
#if defined(GRAPHICS_API_D3D12)
    constexpr D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeap::nativeType(const DescriptorHeap::Type &t)
    {
        switch (t)
        {
        case Type::TextureSrv:
        case Type::TextureUav:
        case Type::RawBufferSrv:
        case Type::RawBufferUav:
        case Type::TypedBufferSrv:
        case Type::TypedBufferUav:
        case Type::StructuredBufferSrv:
        case Type::StructuredBufferUav:
        case Type::AccelerationStructureSrv:
        case Type::Cbv:
            return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        case Type::Dsv:
            return D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        case Type::Rtv:
            return D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        case Type::Sampler:
            return D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        default:
            break;
        }

        Log::Fatal(L"Invalid descriptor type detected.");
        return D3D12_DESCRIPTOR_HEAP_TYPE(-1);
    }

    DescriptorHeap::~DescriptorHeap()
    {
        for (auto& h : m_apiData.m_heaps) {
            if (h.m_descHeap) {
                h.m_descHeap->Release();
            }
        }
        m_apiData = {};
    }

    void DescriptorHeap::SetName(const std::wstring& str)
    {
        for (auto& h : m_apiData.m_heaps) {
            if (h.m_descHeap != nullptr)
                SetNameInternal(h.m_descHeap, str.c_str());
        }
    }

    bool DescriptorHeap::Create(Device* dev, const DescriptorHeap::Desc& desc, bool isShaderVisible)
    {
        // Find out how many heaps we need
        static_assert(value(Type::Count) == 13, "Unexpected desc count, make sure all desc types are supported");
        for (auto& h : m_apiData.m_heaps) {
            if (h.m_descHeap) {
                Log::Fatal(L"DescriptorHeap is already in use.");
                return false;
            }
        }

        uint32_t nativeDescCount[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = { 0 };
        m_desc = desc;

        for (uint32_t i = 0; i < value(Type::Count); ++i) {
            Type t = static_cast<Type>(i);
            nativeDescCount[nativeType(t)] += m_desc.m_descCount[value(t)];
        }

        for (uint32_t i = 0; i < m_apiData.m_heaps.size(); ++i)
        {
            auto& h = m_apiData.m_heaps[i];

            if (nativeDescCount[i] > 0) {
                D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE(i);
                D3D12_DESCRIPTOR_HEAP_DESC hDesc = {};

                hDesc.Flags = isShaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                hDesc.Type = type;
                hDesc.NumDescriptors = nativeDescCount[i];
                if (FAILED(dev->m_apiData.m_device->CreateDescriptorHeap(&hDesc, IID_PPV_ARGS(&h.m_descHeap))))
                {
                    Log::Fatal(L"Failed to create descriptor heap");
                    return false;
                }
                h.m_numDescriptors = nativeDescCount[i];
                h.m_incrementSize = dev->m_apiData.m_device->GetDescriptorHandleIncrementSize(type);
            }
        }

        return true;
    }

    bool DescriptorHeap::ResetAllocation()
    {
        for (auto& h : m_apiData.m_heaps)
            h.m_currentOffset = 0;

        return true;
    }

    bool DescriptorHeap::Allocate(const DescriptorTableLayout* descTable, AllocationInfo* retAllocationInfo, uint32_t unboundDescTableCount)
    {
        *retAllocationInfo = {};

        if ((! descTable->m_lastUnbound) && unboundDescTableCount > 0) {
            Log::Fatal(L"Error: Invalid unbound descriptor table count detected.");
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_TYPE heapType = nativeType(descTable->m_ranges[0].m_type);
        uint32_t nbEntryToAllocate = 0;
        for (size_t i = 0; i < descTable->m_ranges.size(); ++i) {
            if (heapType != DescriptorHeap::nativeType(descTable->m_ranges[i].m_type)) {
                Log::Fatal(L"Different heap type entry cannot be in single descriptor table.");
                return false;
            }
            if (descTable->m_lastUnbound && i == descTable->m_ranges.size()-1) {
                // this should be only the last one. 
                nbEntryToAllocate += unboundDescTableCount;
            }
            else {
                nbEntryToAllocate += descTable->m_ranges[i].m_descCount;
            }
        }

        auto& heapEntry = m_apiData.m_heaps[(uint32_t)heapType];

        if (heapEntry.m_currentOffset + nbEntryToAllocate > heapEntry.m_numDescriptors) {
            Log::Fatal(L"Failed to allocate descriptor table entry. NumDesc:%d CurrentOffset:%d TriedToAllocate:%d", heapEntry.m_numDescriptors, heapEntry.m_currentOffset, nbEntryToAllocate);
            return false;
        }

        retAllocationInfo->m_numDescriptors = nbEntryToAllocate;
        retAllocationInfo->m_incrementSize = heapEntry.m_incrementSize;
        retAllocationInfo->m_hCPU = heapEntry.m_descHeap->GetCPUDescriptorHandleForHeapStart();
        retAllocationInfo->m_hGPU = heapEntry.m_descHeap->GetGPUDescriptorHandleForHeapStart();

        retAllocationInfo->m_hCPU.ptr += heapEntry.m_incrementSize * heapEntry.m_currentOffset;
        retAllocationInfo->m_hGPU.ptr += heapEntry.m_incrementSize * heapEntry.m_currentOffset;

        heapEntry.m_currentOffset += nbEntryToAllocate;

        return true;
    }

#elif defined(GRAPHICS_API_VK)
// non class enum warnings.
#ifdef WIN32
#pragma warning( push )
#pragma warning( disable : 26812 )
#endif
    constexpr VkDescriptorType DescriptorHeap::nativeType(const DescriptorHeap::Type &type)
    {
        switch (type)
        {
        case Type::AccelerationStructureSrv:
            return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        case Type::TextureSrv:
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case Type::TextureUav:
            return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case Type::RawBufferSrv:
        case Type::TypedBufferSrv:
            return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        case Type::RawBufferUav:
        case Type::TypedBufferUav:
            return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        case Type::Cbv:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case Type::StructuredBufferSrv:
        case Type::StructuredBufferUav:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case Type::Dsv:
        case Type::Rtv:
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case Type::Sampler:
            return VK_DESCRIPTOR_TYPE_SAMPLER;
        default:
            Log::Fatal(L"Invalid descriptor type detected.");
            return VK_DESCRIPTOR_TYPE_MAX_ENUM;
        }
    }
#ifdef WIN32
#pragma warning( pop )
#endif

    DescriptorHeap::~DescriptorHeap()
    {
        if (m_apiData.m_device && m_apiData.m_descPool) {
            vkDestroyDescriptorPool(m_apiData.m_device, m_apiData.m_descPool, nullptr);
        }
        m_apiData = {};
    }

    void DescriptorHeap::SetName(const std::wstring& str)
    {
        SetNameInternal(m_apiData.m_device, VkObjectType::VK_OBJECT_TYPE_DESCRIPTOR_POOL, (uint64_t)m_apiData.m_descPool, str.c_str());
    }

    bool DescriptorHeap::Create(Device* dev, const DescriptorHeap::Desc& desc, bool isShaderVisible)
    {
        uint32_t totalDescCount = 0;
        VkDescriptorPoolSize poolSizeForType[value(Type::Count)];

        uint32_t usedSlots = 0;
        for (uint32_t i = 0; i < value(Type::Count); ++i)
        {
            if (desc.m_descCount[i] > 0) {
                poolSizeForType[usedSlots].type = nativeType((Type)i);
                poolSizeForType[usedSlots].descriptorCount = desc.m_descCount[i];
                totalDescCount += desc.m_descCount[i];
                usedSlots++;
            }
        }

        VkDescriptorPoolCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.maxSets = totalDescCount;
        info.poolSizeCount = usedSlots;
        info.pPoolSizes = poolSizeForType;
        info.flags = 0;

        // Currently disabled since it's not widely supported.
        (void)isShaderVisible;
#if 0
        if (!isShaderVisible)
            info.flags |= VK_DESCRIPTOR_POOL_CREATE_HOST_ONLY_BIT_VALVE;
#endif

        if (vkCreateDescriptorPool(dev->m_apiData.m_device, &info, nullptr, &m_apiData.m_descPool) != VK_SUCCESS) {
            Log::Fatal(L"Error creating descriptor pool!");
            return false;
        }

        m_apiData.m_device = dev->m_apiData.m_device;
        m_desc = desc;
        return true;
    }

    bool DescriptorHeap::ResetAllocation()
    {
        if (vkResetDescriptorPool(m_apiData.m_device, m_apiData.m_descPool, 0) != VK_SUCCESS) {
            Log::Fatal(L"Error: Failed to reset descriptor pool.");
            return false;
        }

        return true;
    }

    bool DescriptorHeap::Allocate(const DescriptorTableLayout *descTable, AllocationInfo* retAllocationInfo, uint32_t unboundDescTableCount)
    {
        *retAllocationInfo = {};

        if ((! descTable->m_lastUnbound) && unboundDescTableCount > 0) {
            Log::Fatal(L"Error: Invalid unbound descriptor table count detected.");
            return false;
        }

        VkDescriptorSetAllocateInfo allocInfo = {};
        std::vector<VkDescriptorSetLayout> layouts{ descTable->m_apiData.m_descriptorSetLayout };
        VkDescriptorSetVariableDescriptorCountAllocateInfo valDescInfo= {};
        std::vector<uint32_t>                               valDescCountArr(layouts.size(), 0);

        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_apiData.m_descPool;
        allocInfo.descriptorSetCount = (uint32_t)layouts.size();
        allocInfo.pSetLayouts = layouts.data();

        if (descTable->m_lastUnbound) {
            valDescInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
            valDescCountArr.back() = unboundDescTableCount; // set unbound desc count at the last of the layout.
            valDescInfo.descriptorSetCount = (uint32_t)valDescCountArr.size();
            valDescInfo.pDescriptorCounts = valDescCountArr.data();
            allocInfo.pNext = &valDescInfo;
        }

        if (vkAllocateDescriptorSets(m_apiData.m_device, &allocInfo, &retAllocationInfo->m_descSet) != VK_SUCCESS) {
            Log::Fatal(L"Error: Failed to allocate descriptor set from heap.");
            return false;
        }

        return true;
    }
#endif

    /***************************************************************
     * DescriptorTableLayout(D3D12_DESCRIPTOR_RANGE) in D3D12
     * DescriptorSetLayout in VK
     ***************************************************************/
#if defined(GRAPHICS_API_D3D12)
    constexpr D3D12_DESCRIPTOR_RANGE_TYPE DescriptorTableLayout::nativeType(const DescriptorHeap::Type& type)
    {
        using Type = DescriptorHeap::Type;

        switch (type)
        {
        case Type::TextureSrv:
        case Type::RawBufferSrv:
        case Type::TypedBufferSrv:
        case Type::StructuredBufferSrv:
        case Type::AccelerationStructureSrv:
            return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        case Type::TextureUav:
        case Type::RawBufferUav:
        case Type::TypedBufferUav:
        case Type::StructuredBufferUav:
            return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        case Type::Cbv:
            return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        case Type::Sampler:
            return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        default:
            break;
        }

        Log::Fatal(L"Invalid descriptor range type detected");
        return D3D12_DESCRIPTOR_RANGE_TYPE(-1);
    }

    DescriptorTableLayout::~DescriptorTableLayout()
    {
        // nothing to do in D3D12
        m_apiData = {};
    }

    void DescriptorTableLayout::SetName(const std::wstring& /*str*/)
    {
        // there is no object to be named.
        return;
    }

    void DescriptorTableLayout::AddRange(DescriptorHeap::Type type, uint32_t baseRegIndex, int32_t descriptorCount, uint32_t regSpace, uint32_t )
    {
        if (m_lastUnbound) {
            Log::Fatal(L"It's impossible to add further range after unbound descriptor entry.");
            return;
        }
        if (descriptorCount < 0)
            m_lastUnbound = true;

        m_ranges.push_back({ type, baseRegIndex, (uint32_t)std::abs(descriptorCount), regSpace });

        m_ranges.back().m_offsetFromTableStart = 0;
        if (m_ranges.size() > 1) {
            m_ranges[m_ranges.size() - 1].m_offsetFromTableStart = m_ranges[m_ranges.size() - 2].m_offsetFromTableStart + m_ranges[m_ranges.size() - 2].m_descCount;
        }
    }

    bool DescriptorTableLayout::SetAPIData(Device *)
    {
        if (m_ranges.size() < 1) {
            Log::Fatal(L"Invalid descriptor table detected.");
            return false;
        }

        // check if sampler and other type of entryies are conterminate.
        {
            D3D12_DESCRIPTOR_HEAP_TYPE heapType = DescriptorHeap::nativeType(m_ranges[0].m_type);
            for (size_t i = 0; i < m_ranges.size(); ++i) {
                if (heapType != DescriptorHeap::nativeType(m_ranges[i].m_type)) {
                    Log::Fatal(L"Different heap type entry cannot be in single descriptor table.");
                    return false;
                }
            }
        }

        m_apiData.m_ranges.resize(m_ranges.size());

        uint32_t offsetFromStart = 0;
        for (size_t i = 0; i < m_ranges.size(); ++i) {
            auto& src = m_ranges[i];
            auto& dst = m_apiData.m_ranges[i];

            dst.RangeType = nativeType(src.m_type);
            if (m_lastUnbound && i == m_ranges.size() - 1)
                dst.NumDescriptors = (UINT)-1; // In D3D12 -1 means unbound desc tabele.
            else
                dst.NumDescriptors = src.m_descCount;
            dst.BaseShaderRegister = src.m_baseRegIndex;
            dst.RegisterSpace = src.m_regSpace;
            dst.OffsetInDescriptorsFromTableStart = offsetFromStart;

            offsetFromStart += src.m_descCount;
        }

        return true;
    };
#elif defined(GRAPHICS_API_VK)

    DescriptorTableLayout::~DescriptorTableLayout()
    {
        if (m_apiData.m_device && m_apiData.m_descriptorSetLayout) {
            vkDestroyDescriptorSetLayout(m_apiData.m_device, m_apiData.m_descriptorSetLayout, nullptr);
        }
        m_apiData = {};
    }

    void DescriptorTableLayout::SetName(const std::wstring& str)
    {
        SetNameInternal(m_apiData.m_device, VkObjectType::VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)m_apiData.m_descriptorSetLayout, str.c_str());
    }

    void DescriptorTableLayout::AddRange(DescriptorHeap::Type type, uint32_t baseRegIndex, int32_t descriptorCount, uint32_t regSpace, uint32_t offset)
    {
        if (m_lastUnbound) {
            Log::Fatal(L"It's impossible to add further range after unbound descriptor entry.");
            return;
        }
        if (descriptorCount < 0)
            m_lastUnbound = true;

        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = offset == 0 ? (uint32_t)m_apiData.m_bindings.size() : offset; //baseRegIndex;
        binding.descriptorCount = std::abs(descriptorCount);
#if USE_SHADER_TABLE_RT_SHADERS
        binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CALLABLE_BIT_KHR;
#else
        binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
#endif
        binding.descriptorType = DescriptorHeap::nativeType(type);

        m_apiData.m_bindings.push_back(binding);

        // API independent part.
        m_ranges.push_back({ type, baseRegIndex, (uint32_t)std::abs(descriptorCount), regSpace });
        m_ranges.back().m_offsetFromTableStart = 0;
        if (m_ranges.size() > 1) {
            m_ranges[m_ranges.size() - 1].m_offsetFromTableStart = m_ranges[m_ranges.size() - 2].m_offsetFromTableStart + m_ranges[m_ranges.size() - 2].m_descCount;
        }
    }

    bool DescriptorTableLayout::SetAPIData(Device *dev)
    {
        VkDescriptorSetLayoutCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.flags = 0;
        info.pBindings = m_apiData.m_bindings.data();
        info.bindingCount = (uint32_t)m_apiData.m_bindings.size();

        std::vector<VkDescriptorBindingFlags> bindFlags(m_apiData.m_bindings.size(), 0);
        VkDescriptorSetLayoutBindingFlagsCreateInfo bindInfo = {};

        if (m_lastUnbound) {
            // Set variable desc flag 
            bindFlags.back() = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

            bindInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            bindInfo.bindingCount = (uint32_t)bindFlags.size();
            bindInfo.pBindingFlags = bindFlags.data();

            info.pNext = &bindInfo;
        }

        if (vkCreateDescriptorSetLayout(dev->m_apiData.m_device, &info, nullptr, &m_apiData.m_descriptorSetLayout) != VK_SUCCESS) {
            Log::Fatal(L"Failed to create descriptor set layout.");
            return false;
        }
        m_apiData.m_device = dev->m_apiData.m_device;

        uint32_t offsetFromStart = 0;
        for (size_t i = 0; i < m_ranges.size(); ++i) {
            auto& r = m_ranges[i];
            r.m_offsetFromTableStart = offsetFromStart;
            offsetFromStart += r.m_descCount;
        }

        return true;
    }
#endif

     /***************************************************************
      * DescriptorTable(A portion of a desc heap) in D3D12
      * DescriptorSet in VK
      ***************************************************************/
#if defined(GRAPHICS_API_D3D12)
    DescriptorTable::~DescriptorTable()
    {
        // nothing to do.
    }

    bool DescriptorTable::Allocate(DescriptorHeap* descHeap, const DescriptorTableLayout* descTableLayout, uint32_t unboundDescTableCount)
    {
        m_descTableLayout = {};

        if (!descHeap->Allocate(descTableLayout, &m_apiData.m_heapAllocationInfo, unboundDescTableCount)) {
            Log::Fatal(L"Faild to allocate descriptor heap.");
            return false;
        }

        m_descTableLayout = descTableLayout;

        return true;
    }

    bool DescriptorTable::SetSrv(Device* dev, uint32_t rangeIndex, uint32_t indexInRange, const ShaderResourceView* srv)
    {
        if (rangeIndex >= m_descTableLayout->m_ranges.size()) {
            Log::Fatal(L"Range index is out of bounds.");
            return false;
        }
        if (indexInRange >= m_descTableLayout->m_ranges[rangeIndex].m_descCount) {
            Log::Fatal(L"Index in Range  is out of bounds.");
            return false;
        }

        uint32_t tableIndex = m_descTableLayout->m_ranges[rangeIndex].m_offsetFromTableStart + indexInRange;
        if (tableIndex >= m_apiData.m_heapAllocationInfo.m_numDescriptors) {
            Log::Fatal(L"Table index is out of bounds.");
            return false;
        }

        auto cpuH = m_apiData.m_heapAllocationInfo.m_hCPU;
        cpuH.ptr += m_apiData.m_heapAllocationInfo.m_incrementSize * tableIndex;

        auto res = srv->m_apiData.m_resource;
        if (res == nullptr) {
            // null descriptor
            dev->m_apiData.m_device->CreateShaderResourceView(nullptr, &srv->m_apiData.m_desc, cpuH);
        }
        else {
            if (srv->m_apiData.m_desc.ViewDimension == D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE)
                dev->m_apiData.m_device->CreateShaderResourceView(nullptr, &srv->m_apiData.m_desc, cpuH); // resource must be null for AS.
            else
                dev->m_apiData.m_device->CreateShaderResourceView(res, &srv->m_apiData.m_desc, cpuH);
        }

        return true;
    }

    bool DescriptorTable::SetUav(Device* dev, uint32_t rangeIndex, uint32_t indexInRange, const UnorderedAccessView* uav)
    {
        if (rangeIndex >= m_descTableLayout->m_ranges.size()) {
            Log::Fatal(L"Range index is out of bounds.");
            return false;
        }
        if (indexInRange >= m_descTableLayout->m_ranges[rangeIndex].m_descCount) {
            Log::Fatal(L"Index in Range  is out of bounds.");
            return false;
        }

        uint32_t tableIndex = m_descTableLayout->m_ranges[rangeIndex].m_offsetFromTableStart + indexInRange;
        if (tableIndex >= m_apiData.m_heapAllocationInfo.m_numDescriptors) {
            Log::Fatal(L"Table index is out of bounds.");
            return false;
        }

        auto cpuH = m_apiData.m_heapAllocationInfo.m_hCPU;
        cpuH.ptr += m_apiData.m_heapAllocationInfo.m_incrementSize * tableIndex;

        auto res = uav->m_apiData.m_resource;
        if (res == nullptr) {
            // null descriptor
            dev->m_apiData.m_device->CreateUnorderedAccessView(nullptr, nullptr, &uav->m_apiData.m_desc, cpuH);
        }
        else {
            if (uav->m_apiData.m_desc.ViewDimension == (D3D12_UAV_DIMENSION)-1) {
                Log::Fatal(L"Binding AS resource as an UAV is not supported.");
                return false;
            }
            dev->m_apiData.m_device->CreateUnorderedAccessView(res, nullptr, &uav->m_apiData.m_desc, cpuH);
        }

        return true;
    }

    bool DescriptorTable::SetCbv(Device* dev, uint32_t rangeIndex, uint32_t indexInRange, const ConstantBufferView* cbv)
    {
        if (rangeIndex >= m_descTableLayout->m_ranges.size()) {
            Log::Fatal(L"Range index is out of bounds.");
            return false;
        }
        if (indexInRange >= m_descTableLayout->m_ranges[rangeIndex].m_descCount) {
            Log::Fatal(L"Index in Range  is out of bounds.");
            return false;
        }

        uint32_t tableIndex = m_descTableLayout->m_ranges[rangeIndex].m_offsetFromTableStart + indexInRange;
        if (tableIndex >= m_apiData.m_heapAllocationInfo.m_numDescriptors) {
            Log::Fatal(L"Table index is out of bounds.");
            return false;
        }

        auto cpuH = m_apiData.m_heapAllocationInfo.m_hCPU;
        cpuH.ptr += m_apiData.m_heapAllocationInfo.m_incrementSize * tableIndex;

        dev->m_apiData.m_device->CreateConstantBufferView(&cbv->m_apiData.m_desc, cpuH);

        return true;
    }

    bool DescriptorTable::SetSampler(Device* dev, uint32_t rangeIndex, uint32_t indexInRange, const Sampler* smp)
    {
        if (rangeIndex >= m_descTableLayout->m_ranges.size()) {
            Log::Fatal(L"Range index is out of bounds.");
            return false;
        }
        if (indexInRange >= m_descTableLayout->m_ranges[rangeIndex].m_descCount) {
            Log::Fatal(L"Index in Range  is out of bounds.");
            return false;
        }

        uint32_t tableIndex = m_descTableLayout->m_ranges[rangeIndex].m_offsetFromTableStart + indexInRange;
        if (tableIndex >= m_apiData.m_heapAllocationInfo.m_numDescriptors) {
            Log::Fatal(L"Table index is out of bounds.");
            return false;
        }

        auto cpuH = m_apiData.m_heapAllocationInfo.m_hCPU;
        cpuH.ptr += m_apiData.m_heapAllocationInfo.m_incrementSize * tableIndex;

        dev->m_apiData.m_device->CreateSampler(&smp->m_apiData.m_desc, cpuH);

        return true;
    }

    bool DescriptorTable::Copy(Device* dev, uint32_t rangeIndex, uint32_t indexInRange, DescriptorTable* descTable, uint32_t explicitCopySize)
    {
        uint32_t nbEntriesToCopy = descTable->m_descTableLayout->m_ranges[0].m_descCount;
		if (explicitCopySize != 0xFFFF'FFFF) {
			// table layout was smaller than the requested copy size.
			if (nbEntriesToCopy < explicitCopySize) {
				Log::Fatal(L"Explicit copy size was larger than the desc table layout size.");
				return false;
			}
			nbEntriesToCopy = explicitCopySize;
		}

        D3D12_DESCRIPTOR_HEAP_TYPE srcHeapType = DescriptorHeap::nativeType(descTable->m_descTableLayout->m_ranges[0].m_type);
        D3D12_DESCRIPTOR_HEAP_TYPE dstHeapType = DescriptorHeap::nativeType(m_descTableLayout->m_ranges[rangeIndex].m_type);

        if (srcHeapType != dstHeapType) {
            Log::Fatal(L"Different heap type detected.");
            return false;
        }

        if (rangeIndex >= m_descTableLayout->m_ranges.size()) {
            Log::Fatal(L"Range index is out of bounds.");
            return false;
        }
        if ((indexInRange + nbEntriesToCopy) > m_descTableLayout->m_ranges[rangeIndex].m_descCount) {
            Log::Fatal(L"Index in Range  is out of bounds.");
            return false;
        }

        uint32_t tableIndex = m_descTableLayout->m_ranges[rangeIndex].m_offsetFromTableStart + indexInRange;
        if (tableIndex >= m_apiData.m_heapAllocationInfo.m_numDescriptors) {
            Log::Fatal(L"Table index is out of bounds.");
            return false;
        }

        auto cpuH = m_apiData.m_heapAllocationInfo.m_hCPU;
        cpuH.ptr += m_apiData.m_heapAllocationInfo.m_incrementSize * tableIndex;

        // source is always from beggining of the allocation.
        auto srcCpuH = descTable->m_apiData.m_heapAllocationInfo.m_hCPU;

        dev->m_apiData.m_device->CopyDescriptorsSimple(nbEntriesToCopy, cpuH, srcCpuH, srcHeapType);

        return true;
    }


#elif defined(GRAPHICS_API_VK)
    DescriptorTable::~DescriptorTable()
    {
        // nothing to do.
        // it doesn't destroy allocated VkDescriptorSet from pool, since the pool will be reset at the beggining of a frame.
    }

    bool DescriptorTable::Allocate(DescriptorHeap* descHeap, const DescriptorTableLayout* descTableLayout, uint32_t unboundDescTableCount)
    {
        if (!descHeap->Allocate(descTableLayout, &m_apiData.m_heapAllocationInfo, unboundDescTableCount)) {
            Log::Fatal(L"Faild to allocate descriptor heap.");
            return false;
        }

        m_descTableLayout = descTableLayout;

        return true;
    }

    bool DescriptorTable::SetSrv(Device* dev, uint32_t rangeIndex, uint32_t indexInRange, const ShaderResourceView* srv)
    {
        VkWriteDescriptorSet write = {};
        VkWriteDescriptorSetAccelerationStructureKHR descASInfo = {};
        VkDescriptorBufferInfo  rawBufInfo = {};
        VkDescriptorImageInfo   imageInfo = {};
        VkDescriptorType        descType = VK_DESCRIPTOR_TYPE_MAX_ENUM;

        if (srv->m_isNullView) {
            // Nulldesc extension need to be supported before using.
            if (srv->m_nullViewType == Resource::Type::Buffer) {
                bool isAS = false;

                if (isAS) {
                    descType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

                    descASInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
                    descASInfo.accelerationStructureCount = 1;
                    descASInfo.pAccelerationStructures = nullptr;
                    write.pNext = &descASInfo;
                }
                else if (! srv->m_nullIsTypedBuffer) {
                    descType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;

                    // rawBuffer
                    rawBufInfo.buffer = nullptr;
                    rawBufInfo.offset = 0;
                    rawBufInfo.range = 0;
                    write.pBufferInfo = &rawBufInfo;
                }
                else {
                    descType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;

                    // typed buffer
                    write.pTexelBufferView = nullptr;
                }
            }
            else {
                descType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;

                // texture resource
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = nullptr;
                imageInfo.sampler = nullptr;
                write.pImageInfo = &imageInfo;
            }
        }
        else {
            if (srv->m_apiData.m_accelerationStructure) {
                descType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

                descASInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
                descASInfo.accelerationStructureCount = 1;
                descASInfo.pAccelerationStructures = &srv->m_apiData.m_accelerationStructure;
                write.pNext = &descASInfo;
            }
            else if (srv->m_apiData.m_rawBuffer) {
                descType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;

                // rawBuffer
                rawBufInfo.buffer = srv->m_apiData.m_rawBuffer;
                rawBufInfo.offset = srv->m_apiData.m_rawOffsetInBytes;
                rawBufInfo.range = srv->m_apiData.m_rawSizeInBytes;
                write.pBufferInfo = &rawBufInfo;
            }
            else if (srv->m_apiData.m_isTypedBufferView && srv->m_apiData.m_typedBufferView) {
                descType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;

                // typed buffer
                write.pTexelBufferView = &srv->m_apiData.m_typedBufferView;
            }
            else
            {
                descType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;

                // texture resource
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = srv->m_apiData.m_imageView;
                imageInfo.sampler = nullptr;
                write.pImageInfo = &imageInfo;
            }
        }

        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorType = descType;
        write.dstSet = m_apiData.m_heapAllocationInfo.m_descSet;
        write.dstBinding = rangeIndex;
        write.dstArrayElement = indexInRange;
        write.descriptorCount = 1;

        vkUpdateDescriptorSets(dev->m_apiData.m_device, 1, &write, 0, nullptr);

        return true;
    }

    bool DescriptorTable::SetUav(Device* dev, uint32_t rangeIndex, uint32_t indexInRange, const UnorderedAccessView* uav)
    {
        VkWriteDescriptorSet write = {};
#if 0
        VkWriteDescriptorSetAccelerationStructureKHR descASInfo = {};
#endif
        VkDescriptorBufferInfo  rawBufInfo = {};
        VkDescriptorImageInfo   imageInfo = {};
        VkDescriptorType        descType = VK_DESCRIPTOR_TYPE_MAX_ENUM;

        if (uav->m_isNullView) {
            // Nulldesc extension need to be supported before using.

            if (uav->m_nullViewType == Resource::Type::Buffer) {
#if 0
                bool isAS = false;

                if (isAS) {
                    descType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

                    descASInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
                    descASInfo.accelerationStructureCount = 1;
                    descASInfo.pAccelerationStructures = nullptr;
                    write.pNext = &descASInfo;
                } else
#endif
                if (! uav->m_nullIsTypedBuffer) {
                    // rawBuffer
                    descType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    rawBufInfo.buffer = nullptr;
                    rawBufInfo.offset = 0;
                    rawBufInfo.range = 0;
                    write.pBufferInfo = &rawBufInfo;
                }
                else {
                    // typedBuffer
                    descType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
                    write.pTexelBufferView = nullptr;
                }
            }
            else {
				descType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				// texture resource
				imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
				imageInfo.imageView = nullptr;
				imageInfo.sampler = nullptr;
				write.pImageInfo = &imageInfo;
            }
        }
        else {
#if 0
            if (uav->m_apiData.m_accelerationStructure) {
                descType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

                descASInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
                descASInfo.accelerationStructureCount = 1;
                descASInfo.pAccelerationStructures = &uav->m_apiData.m_accelerationStructure;
                write.pNext = &descASInfo;
            }
            else
#endif
            if (uav->m_apiData.m_rawBuffer) {
                // rawBuffer
                descType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                rawBufInfo.buffer = uav->m_apiData.m_rawBuffer;
                rawBufInfo.offset = uav->m_apiData.m_rawOffsetInBytes;
                rawBufInfo.range = uav->m_apiData.m_rawSizeInBytes;
                write.pBufferInfo = &rawBufInfo;
            }
            else if (uav->m_apiData.m_isTypedBufferView && uav->m_apiData.m_typedBufferView) {
                // typedBuffer
                descType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
                write.pTexelBufferView = &uav->m_apiData.m_typedBufferView;
            }
            else
            {
                descType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

                // texture resource
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                imageInfo.imageView = uav->m_apiData.m_imageView;
                imageInfo.sampler = nullptr;
                write.pImageInfo = &imageInfo;
            }
        }

        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorType = descType;
        write.dstSet = m_apiData.m_heapAllocationInfo.m_descSet;
        write.dstBinding = rangeIndex;
        write.dstArrayElement = indexInRange;
        write.descriptorCount = 1;

        vkUpdateDescriptorSets(dev->m_apiData.m_device, 1, &write, 0, nullptr);

        return true;
    }

    bool DescriptorTable::SetCbv(Device* dev, uint32_t rangeIndex, uint32_t indexInRange, const ConstantBufferView* cbv)
    {
        VkDescriptorBufferInfo info;
        info.buffer = cbv->m_apiData.m_buffer;
        info.offset = cbv->m_apiData.m_offsetInBytes;
        info.range = cbv->m_apiData.m_sizeInBytes;

        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_apiData.m_heapAllocationInfo.m_descSet;
        write.dstBinding = rangeIndex;
        write.dstArrayElement = indexInRange;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &info;

        vkUpdateDescriptorSets(dev->m_apiData.m_device, 1, &write, 0, nullptr);

        return true;
    }

    bool DescriptorTable::SetSampler(Device* dev, uint32_t rangeIndex, uint32_t indexInRange, const Sampler* sampler)
    {
        VkDescriptorImageInfo info;
        info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        info.imageView = nullptr;
        info.sampler = sampler->m_apiData.m_sampler;

        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_apiData.m_heapAllocationInfo.m_descSet;
        write.dstBinding = rangeIndex;
        write.dstArrayElement = indexInRange;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &info;

        vkUpdateDescriptorSets(dev->m_apiData.m_device, 1, &write, 0, nullptr);

        return true;
    }

    bool DescriptorTable::Copy(Device* dev, uint32_t rangeIndex, uint32_t indexInRange, DescriptorTable* descTable, uint32_t explicitCopySize)
    {
        uint32_t nbEntriesToCopy = descTable->m_descTableLayout->m_ranges[0].m_descCount;
        if (explicitCopySize != 0xFFFF'FFFF) {
            // table layout was smaller than the requested copy size.
            if (nbEntriesToCopy < explicitCopySize) {
                Log::Fatal(L"Explicit copy size was larger than the desc table layout size.");
                return false;
            }
            nbEntriesToCopy = explicitCopySize;
        }

        VkDescriptorType srcHeapType = DescriptorHeap::nativeType(descTable->m_descTableLayout->m_ranges[0].m_type);
        VkDescriptorType dstHeapType = DescriptorHeap::nativeType(m_descTableLayout->m_ranges[rangeIndex].m_type);

        if (srcHeapType != dstHeapType) {
            Log::Fatal(L"Different heap type detected.");
            return false;
        }

        if (rangeIndex >= m_descTableLayout->m_ranges.size()) {
            Log::Fatal(L"Range index is out of bounds.");
            return false;
        }
        if ((indexInRange + nbEntriesToCopy) > m_descTableLayout->m_ranges[rangeIndex].m_descCount) {
            Log::Fatal(L"Index in Range  is out of bounds.");
            return false;
        }

        VkCopyDescriptorSet copy = {};
        copy.sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
        copy.srcSet = descTable->m_apiData.m_heapAllocationInfo.m_descSet;
        copy.srcBinding = 0;
        copy.srcArrayElement = 0;
        copy.dstSet = m_apiData.m_heapAllocationInfo.m_descSet;
        copy.dstBinding = rangeIndex;
        copy.dstArrayElement = indexInRange;
        copy.descriptorCount = nbEntriesToCopy;

        vkUpdateDescriptorSets(dev->m_apiData.m_device, 0, nullptr, 1, &copy);

        return true;
    }

#endif

      /***************************************************************
       * RootSignature in D3D12
       * VkPipelineLayout in VK
       ***************************************************************/
#if defined(GRAPHICS_API_D3D12)
    RootSignature::~RootSignature()
    {
        if (m_apiData.m_rootSignature)
            m_apiData.m_rootSignature->Release();
        m_apiData = {};
    }

    void RootSignature::SetName(const std::wstring& str)
    {
        SetNameInternal(m_apiData.m_rootSignature, str.c_str());
    }

    bool RootSignature::Init(Device* dev, const std::vector<DescriptorTableLayout*>& descLayout)
    {
        std::vector<D3D12_ROOT_PARAMETER>   params;
        params.reserve(descLayout.size());

        for (auto& tl : descLayout) {
            D3D12_ROOT_PARAMETER prm = {};
            prm.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            prm.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            prm.DescriptorTable.NumDescriptorRanges = (uint32_t)tl->m_apiData.m_ranges.size();
            prm.DescriptorTable.pDescriptorRanges = &tl->m_apiData.m_ranges[0];

            params.push_back(prm);
        }

        D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
        rootDesc.NumParameters = (uint32_t)descLayout.size();
        rootDesc.pParameters = &params[0];
        rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        {
            ID3DBlob* serializedRS = nullptr;
            ID3DBlob* error = nullptr;

            HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRS, &error);
            if (error)
            {
                const char* errorMsg = (const char*)error->GetBufferPointer();
                std::wstring wErrorMsg = Log::ToWideString(std::string(errorMsg));

                Log::Error(L"SerializeRootSignature error: %s", wErrorMsg.c_str());
            }
            if (FAILED(hr)) {
                Log::Fatal(L"Failed to serialize rootSignature");
                if (serializedRS)
                    serializedRS->Release();
                if (error)
                    error->Release();
                return false;
            }

            hr = dev->m_apiData.m_device->CreateRootSignature(0, serializedRS->GetBufferPointer(), serializedRS->GetBufferSize(), IID_PPV_ARGS(&m_apiData.m_rootSignature));
            if (serializedRS)
                serializedRS->Release();
            if (error)
                error->Release();

            if (FAILED(hr)) {
                Log::Fatal(L"Failed to create rootSignature");
                return false;
            }
        }

        return true;
    };
#elif defined(GRAPHICS_API_VK)
    RootSignature::~RootSignature()
    {
        if (m_apiData.m_device && m_apiData.m_pipelineLayout)
            vkDestroyPipelineLayout(m_apiData.m_device, m_apiData.m_pipelineLayout, nullptr);
        m_apiData = {};
    }

    void RootSignature::SetName(const std::wstring& str)
    {
        SetNameInternal(m_apiData.m_device, VkObjectType::VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)m_apiData.m_pipelineLayout, str.c_str());
    }

    bool RootSignature::Init(Device* dev, const std::vector<DescriptorTableLayout*>& descLayout)
    {
        std::vector<VkDescriptorSetLayout>  lArr;
        for (auto& l : descLayout)
            lArr.push_back(l->m_apiData.m_descriptorSetLayout);

        VkPipelineLayoutCreateInfo  info = {};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        info.setLayoutCount = (uint32_t)lArr.size();
        info.pSetLayouts = lArr.data();

        if (vkCreatePipelineLayout(dev->m_apiData.m_device, &info, nullptr, &m_apiData.m_pipelineLayout) != VK_SUCCESS) {
            Log::Fatal(L"Failed to create rootSignature (vkPipelineLayout)");
            return false;
        }

        m_apiData.m_device = dev->m_apiData.m_device;

        return true;
    }
#endif

    /***************************************************************
     * ShaderByteCode in D3D12
     * VkShaderModule in VK
     * compute only for simplicity.
     ***************************************************************/
#if defined(GRAPHICS_API_D3D12)
    ComputeShader::~ComputeShader()
    {
    }

    bool ComputeShader::Init(const void* shaderByteCode, size_t size)
    {
        m_apiData.m_shaderByteCode.resize(size);
        memcpy(m_apiData.m_shaderByteCode.data(), shaderByteCode, size);

        return true;
    }
#elif defined(GRAPHICS_API_VK)
    ComputeShader::~ComputeShader()
    {
    }

    bool ComputeShader::Init(const void* shaderByteCode, size_t size)
    {
        m_apiData.m_shaderByteCode.resize(size);
        memcpy(m_apiData.m_shaderByteCode.data(), shaderByteCode, size);

        return true;
    }

#endif

    /***************************************************************
     * ID3D12PipelineState in D3D12
     * VkPipeline in VK
     * compute only for simplicity.
     ***************************************************************/
#if defined(GRAPHICS_API_D3D12)
    ComputePipelineState::~ComputePipelineState()
    {
        if (m_apiData.m_pipelineState)
            m_apiData.m_pipelineState->Release();
        m_apiData = {};
    }

    void ComputePipelineState::SetName(const std::wstring& str)
    {
        SetNameInternal(m_apiData.m_pipelineState, str.c_str());
    }

    bool ComputePipelineState::Init(Device *dev, RootSignature* rootSig, ComputeShader* shader)
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};

        desc.pRootSignature = rootSig->m_apiData.m_rootSignature;
        desc.CS = {
            shader->m_apiData.m_shaderByteCode.data(),
            shader->m_apiData.m_shaderByteCode.size() };

        HRESULT hr = dev->m_apiData.m_device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&m_apiData.m_pipelineState));
        if (FAILED(hr)) {
            Log::Fatal(L"Failed to create PSO");
            return false;
        }

        return true;
    }
#elif defined(GRAPHICS_API_VK)
    ComputePipelineState::~ComputePipelineState()
    {
        if (m_apiData.m_device && m_apiData.m_pipeline)
            vkDestroyPipeline(m_apiData.m_device, m_apiData.m_pipeline, nullptr);
        if (m_apiData.m_device && m_apiData.m_module_CS)
            vkDestroyShaderModule(m_apiData.m_device, m_apiData.m_module_CS, nullptr);
        m_apiData = {};
    }

    void ComputePipelineState::SetName(const std::wstring& str)
    {
        SetNameInternal(m_apiData.m_device, VkObjectType::VK_OBJECT_TYPE_PIPELINE, (uint64_t)m_apiData.m_pipeline, str.c_str());
    }

    bool ComputePipelineState::Init(Device* dev, RootSignature* rootSig, ComputeShader* shader)
    {

        {
            VkShaderModuleCreateInfo    info = {};
            info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            info.codeSize = shader->m_apiData.m_shaderByteCode.size();
            info.pCode = (uint32_t *)shader->m_apiData.m_shaderByteCode.data();

            if (vkCreateShaderModule(dev->m_apiData.m_device, &info, nullptr, &m_apiData.m_module_CS) != VK_SUCCESS) {
                Log::Fatal(L"Failed to create a ShaderModule (invalid SPIRV?)");
                return false;
            }
        }

        {
            VkComputePipelineCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            info.stage.pName = "main";
            info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            info.stage.module = m_apiData.m_module_CS;
            info.layout = rootSig->m_apiData.m_pipelineLayout;

            if (vkCreateComputePipelines(dev->m_apiData.m_device, nullptr, 1, &info, nullptr, &m_apiData.m_pipeline) != VK_SUCCESS) {
                Log::Fatal(L"Failed to create PSO (vkPipeline)");
                return false;
            }
        }

        m_apiData.m_device = dev->m_apiData.m_device;
 
        return true;
    }

#endif

    /***************************************************************
     * ID3D12StateObject in D3D12
     ***************************************************************/
#if defined(GRAPHICS_API_D3D12)
    RaytracingPipelineState::~RaytracingPipelineState()
    {
        if (m_apiData.m_rtPSO)
            m_apiData.m_rtPSO->Release();
        m_apiData = {};
    }

    void RaytracingPipelineState::SetName(const std::wstring& str)
    {
        SetNameInternal(m_apiData.m_rtPSO, str.c_str());
    }
#elif defined(GRAPHICS_API_VK)
    RaytracingPipelineState::~RaytracingPipelineState()
    {
    }
    void RaytracingPipelineState::SetName(const std::wstring& str)
    {
        SetNameInternal(m_apiData.m_device, VkObjectType::VK_OBJECT_TYPE_PIPELINE, (uint64_t)m_apiData.m_pipeline, str.c_str());
    }
#endif

    /***************************************************************
     * Abstraction for samplers.
     * D3D12_SAMPLER_DESC in D3D12
     * VkSamplers in VK
     ***************************************************************/
#if defined(GRAPHICS_API_D3D12)
    Sampler::~Sampler()
    {
        // nothing to do in D3D12
    }

    bool Sampler::CreateLinearClamp(Device* /*dev*/)
    {
        m_apiData.m_desc = {};
        m_apiData.m_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        m_apiData.m_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        m_apiData.m_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        m_apiData.m_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        m_apiData.m_desc.MipLODBias = 0.f;
        m_apiData.m_desc.MaxAnisotropy = 1;
        m_apiData.m_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        m_apiData.m_desc.BorderColor[0] = 0.f;
        m_apiData.m_desc.BorderColor[1] = 0.f;
        m_apiData.m_desc.BorderColor[2] = 0.f;
        m_apiData.m_desc.BorderColor[3] = 0.f;
        m_apiData.m_desc.MinLOD = 0.f;
        m_apiData.m_desc.MaxLOD = D3D12_FLOAT32_MAX;

        return true;
    }
#elif defined(GRAPHICS_API_VK)
    Sampler::~Sampler()
    {
        if (m_apiData.m_device && m_apiData.m_sampler) {
            vkDestroySampler(m_apiData.m_device, m_apiData.m_sampler, nullptr);
        }
        m_apiData = {};
    }

    bool Sampler::CreateLinearClamp(Device* dev)
    {
        VkSamplerCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = VK_FILTER_LINEAR;
        info.minFilter = VK_FILTER_LINEAR;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.mipLodBias = 0.f;
        info.anisotropyEnable = false;
        info.maxAnisotropy = 1.f;
        info.compareEnable = false;
        info.compareOp = VK_COMPARE_OP_NEVER;
        info.minLod = 0.f;
        info.maxLod = VK_LOD_CLAMP_NONE;
        info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        info.unnormalizedCoordinates = false;

        if (vkCreateSampler(dev->m_apiData.m_device, &info, nullptr, &m_apiData.m_sampler) != VK_SUCCESS) {
            Log::Fatal(L"Faild to create a sampler");
            return false;
        }

        m_apiData.m_device = dev->m_apiData.m_device;

        return true;
    }
#endif

    /***************************************************************
     * Abstraction for all resource types.
     * D3D12Resource in D3D12
     * VkResource in VK
     ***************************************************************/
    const std::array<Resource::FormatDesc, (uint32_t)Resource::Format::Count> Resource::m_formatDescs =
    { {
            // Format                           Name,           BytesPerBlock ChannelCount  Type          {bDepth,   bStencil, bCompressed},   {CompressionRatio.Width,     CompressionRatio.Height}    {numChannelBits.x, numChannelBits.y, numChannelBits.z, numChannelBits.w}
            {Resource::Format::Unknown,            "Unknown",         0,              0,  Resource::FormatType::Unknown,    false,  false, false,        {1, 1},                                                  {0, 0, 0, 0    }},
            {Resource::Format::R8Unorm,            "R8Unorm",         1,              1,  Resource::FormatType::Unorm,      false,  false, false,        {1, 1},                                                  {8, 0, 0, 0    }},
            {Resource::Format::R8Snorm,            "R8Snorm",         1,              1,  Resource::FormatType::Snorm,      false,  false, false,        {1, 1},                                                  {8, 0, 0, 0    }},
            {Resource::Format::R16Unorm,           "R16Unorm",        2,              1,  Resource::FormatType::Unorm,      false,  false, false,        {1, 1},                                                  {16, 0, 0, 0   }},
            {Resource::Format::R16Snorm,           "R16Snorm",        2,              1,  Resource::FormatType::Snorm,      false,  false, false,        {1, 1},                                                  {16, 0, 0, 0   }},
            {Resource::Format::RG8Unorm,           "RG8Unorm",        2,              2,  Resource::FormatType::Unorm,      false,  false, false,        {1, 1},                                                  {8, 8, 0, 0    }},
            {Resource::Format::RG8Snorm,           "RG8Snorm",        2,              2,  Resource::FormatType::Snorm,      false,  false, false,        {1, 1},                                                  {8, 8, 0, 0    }},
            {Resource::Format::RG16Unorm,          "RG16Unorm",       4,              2,  Resource::FormatType::Unorm,      false,  false, false,        {1, 1},                                                  {16, 16, 0, 0  }},
            {Resource::Format::RG16Snorm,          "RG16Snorm",       4,              2,  Resource::FormatType::Snorm,      false,  false, false,        {1, 1},                                                  {16, 16, 0, 0  }},
            {Resource::Format::RGB16Unorm,         "RGB16Unorm",      6,              3,  Resource::FormatType::Unorm,      false,  false, false,        {1, 1},                                                  {16, 16, 16, 0 }},
            {Resource::Format::RGB16Snorm,         "RGB16Snorm",      6,              3,  Resource::FormatType::Snorm,      false,  false, false,        {1, 1},                                                  {16, 16, 16, 0 }},
            {Resource::Format::R24UnormX8,         "R24UnormX8",      4,              2,  Resource::FormatType::Unorm,      false,  false, false,        {1, 1},                                                  {24, 8, 0, 0   }},
            {Resource::Format::RGB5A1Unorm,        "RGB5A1Unorm",     2,              4,  Resource::FormatType::Unorm,      false,  false, false,        {1, 1},                                                  {5, 5, 5, 1    }},
            {Resource::Format::RGBA8Unorm,         "RGBA8Unorm",      4,              4,  Resource::FormatType::Unorm,      false,  false, false,        {1, 1},                                                  {8, 8, 8, 8    }},
            {Resource::Format::RGBA8Snorm,         "RGBA8Snorm",      4,              4,  Resource::FormatType::Snorm,      false,  false, false,        {1, 1},                                                  {8, 8, 8, 8    }},
            {Resource::Format::RGB10A2Unorm,       "RGB10A2Unorm",    4,              4,  Resource::FormatType::Unorm,      false,  false, false,        {1, 1},                                                  {10, 10, 10, 2 }},
            {Resource::Format::RGB10A2Uint,        "RGB10A2Uint",     4,              4,  Resource::FormatType::Uint,       false,  false, false,        {1, 1},                                                  {10, 10, 10, 2 }},
            {Resource::Format::RGBA16Unorm,        "RGBA16Unorm",     8,              4,  Resource::FormatType::Unorm,      false,  false, false,        {1, 1},                                                  {16, 16, 16, 16}},
            {Resource::Format::RGBA8UnormSrgb,     "RGBA8UnormSrgb",  4,              4,  Resource::FormatType::UnormSrgb,  false,  false, false,        {1, 1},                                                  {8, 8, 8, 8    }},
            // Format                           Name,           BytesPerBlock ChannelCount  Type          {bDepth,   bStencil, bCompressed},   {CompressionRatio.Width,     CompressionRatio.Height}
            {Resource::Format::R16Float,           "R16Float",        2,              1,  Resource::FormatType::Float,      false,  false, false,        {1, 1},                                                  {16, 0, 0, 0   }},
            {Resource::Format::RG16Float,          "RG16Float",       4,              2,  Resource::FormatType::Float,      false,  false, false,        {1, 1},                                                  {16, 16, 0, 0  }},
            {Resource::Format::RGB16Float,         "RGB16Float",      6,              3,  Resource::FormatType::Float,      false,  false, false,        {1, 1},                                                  {16, 16, 16, 0 }},
            {Resource::Format::RGBA16Float,        "RGBA16Float",     8,              4,  Resource::FormatType::Float,      false,  false, false,        {1, 1},                                                  {16, 16, 16, 16}},
            {Resource::Format::R32Float,           "R32Float",        4,              1,  Resource::FormatType::Float,      false,  false, false,        {1, 1},                                                  {32, 0, 0, 0   }},
            {Resource::Format::R32FloatX32,        "R32FloatX32",     8,              2,  Resource::FormatType::Float,      false,  false, false,        {1, 1},                                                  {32, 32, 0, 0  }},
            {Resource::Format::RG32Float,          "RG32Float",       8,              2,  Resource::FormatType::Float,      false,  false, false,        {1, 1},                                                  {32, 32, 0, 0  }},
            {Resource::Format::RGB32Float,         "RGB32Float",      12,             3,  Resource::FormatType::Float,      false,  false, false,        {1, 1},                                                  {32, 32, 32, 0 }},
            {Resource::Format::RGBA32Float,        "RGBA32Float",     16,             4,  Resource::FormatType::Float,      false,  false, false,        {1, 1},                                                  {32, 32, 32, 32}},
            {Resource::Format::R11G11B10Float,     "R11G11B10Float",  4,              3,  Resource::FormatType::Float,      false,  false, false,        {1, 1},                                                  {11, 11, 10, 0 }},
            {Resource::Format::RGB9E5Float,        "RGB9E5Float",     4,              3,  Resource::FormatType::Float,      false,  false, false,        {1, 1},                                                  {9, 9, 9, 5    }},
            {Resource::Format::R8Int,              "R8Int",           1,              1,  Resource::FormatType::Sint,       false,  false, false,        {1, 1},                                                  {8, 0, 0, 0    }},
            {Resource::Format::R8Uint,             "R8Uint",          1,              1,  Resource::FormatType::Uint,       false,  false, false,        {1, 1},                                                  {8, 0, 0, 0    }},
            {Resource::Format::R16Int,             "R16Int",          2,              1,  Resource::FormatType::Sint,       false,  false, false,        {1, 1},                                                  {16, 0, 0, 0   }},
            {Resource::Format::R16Uint,            "R16Uint",         2,              1,  Resource::FormatType::Uint,       false,  false, false,        {1, 1},                                                  {16, 0, 0, 0   }},
            {Resource::Format::R32Int,             "R32Int",          4,              1,  Resource::FormatType::Sint,       false,  false, false,        {1, 1},                                                  {32, 0, 0, 0   }},
            {Resource::Format::R32Uint,            "R32Uint",         4,              1,  Resource::FormatType::Uint,       false,  false, false,        {1, 1},                                                  {32, 0, 0, 0   }},
            {Resource::Format::RG8Int,             "RG8Int",          2,              2,  Resource::FormatType::Sint,       false,  false, false,        {1, 1},                                                  {8, 8, 0, 0    }},
            {Resource::Format::RG8Uint,            "RG8Uint",         2,              2,  Resource::FormatType::Uint,       false,  false, false,        {1, 1},                                                  {8, 8, 0, 0    }},
            {Resource::Format::RG16Int,            "RG16Int",         4,              2,  Resource::FormatType::Sint,       false,  false, false,        {1, 1},                                                  {16, 16, 0, 0  }},
            {Resource::Format::RG16Uint,           "RG16Uint",        4,              2,  Resource::FormatType::Uint,       false,  false, false,        {1, 1},                                                  {16, 16, 0, 0  }},
            {Resource::Format::RG32Int,            "RG32Int",         8,              2,  Resource::FormatType::Sint,       false,  false, false,        {1, 1},                                                  {32, 32, 0, 0  }},
            {Resource::Format::RG32Uint,           "RG32Uint",        8,              2,  Resource::FormatType::Uint,       false,  false, false,        {1, 1},                                                  {32, 32, 0, 0  }},
            // Format                           Name,           BytesPerBlock ChannelCount  Type          {bDepth,   bStencil, bCompressed},   {CompressionRatio.Width,     CompressionRatio.Height}
            {Resource::Format::RGB16Int,           "RGB16Int",        6,              3,  Resource::FormatType::Sint,       false,  false, false,        {1, 1},                                                  {16, 16, 16, 0 }},
            {Resource::Format::RGB16Uint,          "RGB16Uint",       6,              3,  Resource::FormatType::Uint,       false,  false, false,        {1, 1},                                                  {16, 16, 16, 0 }},
            {Resource::Format::RGB32Int,           "RGB32Int",       12,              3,  Resource::FormatType::Sint,       false,  false, false,        {1, 1},                                                  {32, 32, 32, 0 }},
            {Resource::Format::RGB32Uint,          "RGB32Uint",      12,              3,  Resource::FormatType::Uint,       false,  false, false,        {1, 1},                                                  {32, 32, 32, 0 }},
            {Resource::Format::RGBA8Int,           "RGBA8Int",        4,              4,  Resource::FormatType::Sint,       false,  false, false,        {1, 1},                                                  {8, 8, 8, 8    }},
            {Resource::Format::RGBA8Uint,          "RGBA8Uint",       4,              4,  Resource::FormatType::Uint,       false,  false, false,        {1, 1},                                                  {8, 8, 8, 8    }},
            {Resource::Format::RGBA16Int,          "RGBA16Int",       8,              4,  Resource::FormatType::Sint,       false,  false, false,        {1, 1},                                                  {16, 16, 16, 16}},
            {Resource::Format::RGBA16Uint,         "RGBA16Uint",      8,              4,  Resource::FormatType::Uint,       false,  false, false,        {1, 1},                                                  {16, 16, 16, 16}},
            {Resource::Format::RGBA32Int,          "RGBA32Int",      16,              4,  Resource::FormatType::Sint,       false,  false, false,        {1, 1},                                                  {32, 32, 32, 32}},
            {Resource::Format::RGBA32Uint,         "RGBA32Uint",     16,              4,  Resource::FormatType::Uint,       false,  false, false,        {1, 1},                                                  {32, 32, 32, 32}},
            {Resource::Format::BGRA8Unorm,         "BGRA8Unorm",      4,              4,  Resource::FormatType::Unorm,      false,  false, false,        {1, 1},                                                  {8, 8, 8, 8    }},
            {Resource::Format::BGRA8UnormSrgb,     "BGRA8UnormSrgb",  4,              4,  Resource::FormatType::UnormSrgb,  false,  false, false,        {1, 1},                                                  {8, 8, 8, 8    }},
            {Resource::Format::BGRX8Unorm,         "BGRX8Unorm",      4,              4,  Resource::FormatType::Unorm,      false,  false, false,        {1, 1},                                                  {8, 8, 8, 8    }},
            {Resource::Format::BGRX8UnormSrgb,     "BGRX8UnormSrgb",  4,              4,  Resource::FormatType::UnormSrgb,  false,  false, false,        {1, 1},                                                  {8, 8, 8, 8    }},
            {Resource::Format::Alpha8Unorm,        "Alpha8Unorm",     1,              1,  Resource::FormatType::Unorm,      false,  false, false,        {1, 1},                                                  {0, 0, 0, 8    }},
            {Resource::Format::Alpha32Float,       "Alpha32Float",    4,              1,  Resource::FormatType::Float,      false,  false, false,        {1, 1},                                                  {0, 0, 0, 32   }},
            // Format                           Name,           BytesPerBlock ChannelCount  Type          {bDepth,   bStencil, bCompressed},   {CompressionRatio.Width,     CompressionRatio.Height}
            {Resource::Format::R5G6B5Unorm,        "R5G6B5Unorm",     2,              3,  Resource::FormatType::Unorm,      false,  false, false,        {1, 1},                                                  {5, 6, 5, 0    }},
            {Resource::Format::D32Float,           "D32Float",        4,              1,  Resource::FormatType::Float,      true,   false, false,        {1, 1},                                                  {32, 0, 0, 0   }},
            {Resource::Format::D16Unorm,           "D16Unorm",        2,              1,  Resource::FormatType::Unorm,      true,   false, false,        {1, 1},                                                  {16, 0, 0, 0   }},
            {Resource::Format::D32FloatS8X24,      "D32FloatS8X24",   8,              2,  Resource::FormatType::Float,      true,   true,  false,        {1, 1},                                                  {32, 8, 24, 0  }},
            {Resource::Format::D24UnormS8,         "D24UnormS8",      4,              2,  Resource::FormatType::Unorm,      true,   true,  false,        {1, 1},                                                  {24, 8, 0, 0   }},
            {Resource::Format::BC1Unorm,           "BC1Unorm",        8,              3,  Resource::FormatType::Unorm,      false,  false, true,         {4, 4},                                                  {64, 0, 0, 0   }},
            {Resource::Format::BC1UnormSrgb,       "BC1UnormSrgb",    8,              3,  Resource::FormatType::UnormSrgb,  false,  false, true,         {4, 4},                                                  {64, 0, 0, 0   }},
            {Resource::Format::BC2Unorm,           "BC2Unorm",        16,             4,  Resource::FormatType::Unorm,      false,  false, true,         {4, 4},                                                  {128, 0, 0, 0  }},
            {Resource::Format::BC2UnormSrgb,       "BC2UnormSrgb",    16,             4,  Resource::FormatType::UnormSrgb,  false,  false, true,         {4, 4},                                                  {128, 0, 0, 0  }},
            {Resource::Format::BC3Unorm,           "BC3Unorm",        16,             4,  Resource::FormatType::Unorm,      false,  false, true,         {4, 4},                                                  {128, 0, 0, 0  }},
            {Resource::Format::BC3UnormSrgb,       "BC3UnormSrgb",    16,             4,  Resource::FormatType::UnormSrgb,  false,  false, true,         {4, 4},                                                  {128, 0, 0, 0  }},
            {Resource::Format::BC4Unorm,           "BC4Unorm",        8,              1,  Resource::FormatType::Unorm,      false,  false, true,         {4, 4},                                                  {64, 0, 0, 0   }},
            {Resource::Format::BC4Snorm,           "BC4Snorm",        8,              1,  Resource::FormatType::Snorm,      false,  false, true,         {4, 4},                                                  {64, 0, 0, 0   }},
            {Resource::Format::BC5Unorm,           "BC5Unorm",        16,             2,  Resource::FormatType::Unorm,      false,  false, true,         {4, 4},                                                  {128, 0, 0, 0  }},
            {Resource::Format::BC5Snorm,           "BC5Snorm",        16,             2,  Resource::FormatType::Snorm,      false,  false, true,         {4, 4},                                                  {128, 0, 0, 0  }},

            {Resource::Format::BC6HS16,            "BC6HS16",         16,             3,  Resource::FormatType::Float,      false,  false, true,         {4, 4},                                                  {128, 0, 0, 0  }},
            {Resource::Format::BC6HU16,            "BC6HU16",         16,             3,  Resource::FormatType::Float,      false,  false, true,         {4, 4},                                                  {128, 0, 0, 0  }},
            {Resource::Format::BC7Unorm,           "BC7Unorm",        16,             4,  Resource::FormatType::Unorm,      false,  false, true,         {4, 4},                                                  {128, 0, 0, 0  }},
            {Resource::Format::BC7UnormSrgb,       "BC7UnormSrgb",    16,             4,  Resource::FormatType::UnormSrgb,  false,  false, true,         {4, 4},                                                  {128, 0, 0, 0  }},
    } };

#if defined(GRAPHICS_API_D3D12)
    ResourceState::State ResourceState::GetResourceState(D3D12_RESOURCE_STATES state)
    {
        switch (+state) // to avoid warning at the case which is a composited enum values. to make an integer.
        {
        case D3D12_RESOURCE_STATE_COMMON:
            //case D3D12_RESOURCE_STATE_PRESENT:
            return ResourceState::State::Common;
            //return ResourceState::State::Present;

        case D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER:
            return ResourceState::State::ConstantBuffer;
            //return ResourceState::State::VertexBuffer;

        case D3D12_RESOURCE_STATE_COPY_DEST:
            return ResourceState::State::CopyDest;

        case D3D12_RESOURCE_STATE_COPY_SOURCE:
            return ResourceState::State::CopySource;

        case D3D12_RESOURCE_STATE_DEPTH_WRITE:
            return ResourceState::State::DepthStencil;

        case D3D12_RESOURCE_STATE_INDEX_BUFFER:
            return ResourceState::State::IndexBuffer;

        case D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT:
            //case D3D12_RESOURCE_STATE_PREDICATION:
            return ResourceState::State::IndirectArg;
            //    return ResourceState::State::Predication;

        case D3D12_RESOURCE_STATE_RENDER_TARGET:
            return ResourceState::State::RenderTarget;

        case D3D12_RESOURCE_STATE_RESOLVE_DEST:
            return ResourceState::State::ResolveDest;

        case D3D12_RESOURCE_STATE_RESOLVE_SOURCE:
            return ResourceState::State::ResolveSource;

        case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:
            return ResourceState::State::ShaderResource;

        case D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:
            return ResourceState::State::PixelShader;

        case D3D12_RESOURCE_STATE_STREAM_OUT:
            return ResourceState::State::StreamOut;

        case D3D12_RESOURCE_STATE_UNORDERED_ACCESS:
            return ResourceState::State::UnorderedAccess;

        case D3D12_RESOURCE_STATE_GENERIC_READ:
            return ResourceState::State::GenericRead;

        case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:
            return ResourceState::State::NonPixelShader;

        case D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE:
            return ResourceState::State::AccelerationStructure;
        default:
            break;
        }

        Log::Fatal(L"Invalid resource state detected.");
        return ResourceState::State(-1);
    }

    D3D12_RESOURCE_STATES ResourceState::GetD3D12ResourceState(ResourceState::State state)
    {
        switch (state)
        {
        case ResourceState::State::Undefined:
        case ResourceState::State::Common:
            return D3D12_RESOURCE_STATE_COMMON;
        case ResourceState::State::ConstantBuffer:
        case ResourceState::State::VertexBuffer:
            return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        case ResourceState::State::CopyDest:
            return D3D12_RESOURCE_STATE_COPY_DEST;
        case ResourceState::State::CopySource:
            return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case ResourceState::State::DepthStencil:
            return D3D12_RESOURCE_STATE_DEPTH_WRITE; // If depth-writes are disabled, return D3D12_RESOURCE_STATE_DEPTH_WRITE
        case ResourceState::State::IndexBuffer:
            return D3D12_RESOURCE_STATE_INDEX_BUFFER;
        case ResourceState::State::IndirectArg:
            return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        case ResourceState::State::Predication:
            return D3D12_RESOURCE_STATE_PREDICATION;
        case ResourceState::State::Present:
            return D3D12_RESOURCE_STATE_PRESENT;
        case ResourceState::State::RenderTarget:
            return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case ResourceState::State::ResolveDest:
            return D3D12_RESOURCE_STATE_RESOLVE_DEST;
        case ResourceState::State::ResolveSource:
            return D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
        case ResourceState::State::ShaderResource:
#if 0
            // this will hit error when SDK uses COMPUTE queue.
            return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE; // TODO: Need the shader usage mask to set state more optimally
#else
            return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
#endif
        case ResourceState::State::StreamOut:
            return D3D12_RESOURCE_STATE_STREAM_OUT;
        case ResourceState::State::UnorderedAccess:
            return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case ResourceState::State::GenericRead:
            return D3D12_RESOURCE_STATE_GENERIC_READ;
        case ResourceState::State::PixelShader:
            return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        case ResourceState::State::NonPixelShader:
            return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        case ResourceState::State::AccelerationStructure:
            return D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
        default:
            break;
        }

        Log::Fatal(L"Invalid resource state detected.");
        return D3D12_RESOURCE_STATES(-1);
    }
#endif

    uint32_t SubresourceRange::CalcSubresource(uint32_t mipSlice, uint32_t arraySlice, uint32_t MipLevels) {
        return mipSlice + (arraySlice * MipLevels);
    }

    void ResourceState::SetState(ResourceState::State state, Subresource subresource) {
        m_isTrackingPerSubresource |= subresource != SubresourceAll;
        if (subresource == SubresourceAll)
        {
            static_assert(sizeof(state) == 1, "Expects small state enum size for memset to work");
            memset(m_state, (int)state, sizeof(m_state));
        }
        else
            m_state[subresource] = state;
    }

    ResourceState::State ResourceState::GetState(Subresource subresource) {
        return subresource == SubresourceAll ? m_state[0] : m_state[subresource];
    }

    bool ResourceState::IsTrackingPerSubresource() const {
        return m_isTrackingPerSubresource;
    }

    void Resource::SetGlobalState(ResourceState::State state, ResourceState::Subresource subresource) {
        m_globalState.SetState(state, subresource);
    }

    ResourceState::State Resource::GetGlobalState(ResourceState::Subresource subresource) {
        return m_globalState.GetState(subresource);
    }

#if defined(GRAPHICS_API_D3D12)

    Resource::ApiResourceID Resource::GetApiResourceID() const {
        return reinterpret_cast<Resource::ApiResourceID>(m_apiData.m_resource);
    }

    const std::array<Resource::DxgiFormatDesc, uint32_t(Resource::Format::Count)> Resource::m_DxgiFormatDesc =
    { {
        {Resource::Format::Unknown,                       DXGI_FORMAT_UNKNOWN},
        {Resource::Format::R8Unorm,                       DXGI_FORMAT_R8_UNORM},
        {Resource::Format::R8Snorm,                       DXGI_FORMAT_R8_SNORM},
        {Resource::Format::R16Unorm,                      DXGI_FORMAT_R16_UNORM},
        {Resource::Format::R16Snorm,                      DXGI_FORMAT_R16_SNORM},
        {Resource::Format::RG8Unorm,                      DXGI_FORMAT_R8G8_UNORM},
        {Resource::Format::RG8Snorm,                      DXGI_FORMAT_R8G8_SNORM},
        {Resource::Format::RG16Unorm,                     DXGI_FORMAT_R16G16_UNORM},
        {Resource::Format::RG16Snorm,                     DXGI_FORMAT_R16G16_SNORM},
        {Resource::Format::RGB16Unorm,                    DXGI_FORMAT_UNKNOWN},
        {Resource::Format::RGB16Snorm,                    DXGI_FORMAT_UNKNOWN},
        {Resource::Format::R24UnormX8,                    DXGI_FORMAT_R24_UNORM_X8_TYPELESS},
        {Resource::Format::RGB5A1Unorm,                   DXGI_FORMAT_B5G5R5A1_UNORM},
        {Resource::Format::RGBA8Unorm,                    DXGI_FORMAT_R8G8B8A8_UNORM},
        {Resource::Format::RGBA8Snorm,                    DXGI_FORMAT_R8G8B8A8_SNORM},
        {Resource::Format::RGB10A2Unorm,                  DXGI_FORMAT_R10G10B10A2_UNORM},
        {Resource::Format::RGB10A2Uint,                   DXGI_FORMAT_R10G10B10A2_UINT},
        {Resource::Format::RGBA16Unorm,                   DXGI_FORMAT_R16G16B16A16_UNORM},
        {Resource::Format::RGBA8UnormSrgb,                DXGI_FORMAT_R8G8B8A8_UNORM_SRGB},
        {Resource::Format::R16Float,                      DXGI_FORMAT_R16_FLOAT},
        {Resource::Format::RG16Float,                     DXGI_FORMAT_R16G16_FLOAT},
        {Resource::Format::RGB16Float,                    DXGI_FORMAT_UNKNOWN},
        {Resource::Format::RGBA16Float,                   DXGI_FORMAT_R16G16B16A16_FLOAT},
        {Resource::Format::R32Float,                      DXGI_FORMAT_R32_FLOAT},
        {Resource::Format::R32FloatX32,                   DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS},
        {Resource::Format::RG32Float,                     DXGI_FORMAT_R32G32_FLOAT},
        {Resource::Format::RGB32Float,                    DXGI_FORMAT_R32G32B32_FLOAT},
        {Resource::Format::RGBA32Float,                   DXGI_FORMAT_R32G32B32A32_FLOAT},
        {Resource::Format::R11G11B10Float,                DXGI_FORMAT_R11G11B10_FLOAT},
        {Resource::Format::RGB9E5Float,                   DXGI_FORMAT_R9G9B9E5_SHAREDEXP},
        {Resource::Format::R8Int,                         DXGI_FORMAT_R8_SINT},
        {Resource::Format::R8Uint,                        DXGI_FORMAT_R8_UINT},
        {Resource::Format::R16Int,                        DXGI_FORMAT_R16_SINT},
        {Resource::Format::R16Uint,                       DXGI_FORMAT_R16_UINT},
        {Resource::Format::R32Int,                        DXGI_FORMAT_R32_SINT},
        {Resource::Format::R32Uint,                       DXGI_FORMAT_R32_UINT},
        {Resource::Format::RG8Int,                        DXGI_FORMAT_R8G8_SINT},
        {Resource::Format::RG8Uint,                       DXGI_FORMAT_R8G8_UINT},
        {Resource::Format::RG16Int,                       DXGI_FORMAT_R16G16_SINT},
        {Resource::Format::RG16Uint,                      DXGI_FORMAT_R16G16_UINT},
        {Resource::Format::RG32Int,                       DXGI_FORMAT_R32G32_SINT},
        {Resource::Format::RG32Uint,                      DXGI_FORMAT_R32G32_UINT},
        {Resource::Format::RGB16Int,                      DXGI_FORMAT_UNKNOWN},
        {Resource::Format::RGB16Uint,                     DXGI_FORMAT_UNKNOWN},
        {Resource::Format::RGB32Int,                      DXGI_FORMAT_R32G32B32_SINT},
        {Resource::Format::RGB32Uint,                     DXGI_FORMAT_R32G32B32_UINT},
        {Resource::Format::RGBA8Int,                      DXGI_FORMAT_R8G8B8A8_SINT},
        {Resource::Format::RGBA8Uint,                     DXGI_FORMAT_R8G8B8A8_UINT},
        {Resource::Format::RGBA16Int,                     DXGI_FORMAT_R16G16B16A16_SINT},
        {Resource::Format::RGBA16Uint,                    DXGI_FORMAT_R16G16B16A16_UINT},
        {Resource::Format::RGBA32Int,                     DXGI_FORMAT_R32G32B32A32_SINT},
        {Resource::Format::RGBA32Uint,                    DXGI_FORMAT_R32G32B32A32_UINT},
        {Resource::Format::BGRA8Unorm,                    DXGI_FORMAT_B8G8R8A8_UNORM},
        {Resource::Format::BGRA8UnormSrgb,                DXGI_FORMAT_B8G8R8A8_UNORM_SRGB},
        {Resource::Format::BGRX8Unorm,                    DXGI_FORMAT_B8G8R8X8_UNORM},
        {Resource::Format::BGRX8UnormSrgb,                DXGI_FORMAT_B8G8R8X8_UNORM_SRGB},
        {Resource::Format::Alpha8Unorm,                   DXGI_FORMAT_A8_UNORM},
        {Resource::Format::Alpha32Float,                  DXGI_FORMAT_UNKNOWN},
        {Resource::Format::R5G6B5Unorm,                   DXGI_FORMAT_B5G6R5_UNORM},
        {Resource::Format::D32Float,                      DXGI_FORMAT_D32_FLOAT},
        {Resource::Format::D16Unorm,                      DXGI_FORMAT_D16_UNORM},
        {Resource::Format::D32FloatS8X24,                 DXGI_FORMAT_D32_FLOAT_S8X24_UINT},
        {Resource::Format::D24UnormS8,                    DXGI_FORMAT_D24_UNORM_S8_UINT},
        {Resource::Format::BC1Unorm,                      DXGI_FORMAT_BC1_UNORM},
        {Resource::Format::BC1UnormSrgb,                  DXGI_FORMAT_BC1_UNORM_SRGB},
        {Resource::Format::BC2Unorm,                      DXGI_FORMAT_BC2_UNORM},
        {Resource::Format::BC2UnormSrgb,                  DXGI_FORMAT_BC2_UNORM_SRGB},
        {Resource::Format::BC3Unorm,                      DXGI_FORMAT_BC3_UNORM},
        {Resource::Format::BC3UnormSrgb,                  DXGI_FORMAT_BC3_UNORM_SRGB},
        {Resource::Format::BC4Unorm,                      DXGI_FORMAT_BC4_UNORM},
        {Resource::Format::BC4Snorm,                      DXGI_FORMAT_BC4_SNORM},
        {Resource::Format::BC5Unorm,                      DXGI_FORMAT_BC5_UNORM},
        {Resource::Format::BC5Snorm,                      DXGI_FORMAT_BC5_SNORM},
        {Resource::Format::BC6HS16,                       DXGI_FORMAT_BC6H_SF16},
        {Resource::Format::BC6HU16,                       DXGI_FORMAT_BC6H_UF16},
        {Resource::Format::BC7Unorm,                      DXGI_FORMAT_BC7_UNORM},
        {Resource::Format::BC7UnormSrgb,                  DXGI_FORMAT_BC7_UNORM_SRGB},
    } };
    

    DXGI_FORMAT Resource::GetTypelessFormat(Resource::Format format)
    {
        using Format = Resource::Format;

        switch (format)
        {
        case Format::D16Unorm:
            return DXGI_FORMAT_R16_TYPELESS;
        case Format::D32FloatS8X24:
            return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        case Format::D24UnormS8:
            return DXGI_FORMAT_R24G8_TYPELESS;
        case Format::D32Float:
            return DXGI_FORMAT_R32_TYPELESS;

        case Format::RGBA32Float:
        case Format::RGBA32Uint:
        case Format::RGBA32Int:
            return DXGI_FORMAT_R32G32B32A32_TYPELESS;

        case Format::RGB32Float:
        case Format::RGB32Uint:
        case Format::RGB32Int:
            return DXGI_FORMAT_R32G32B32_TYPELESS;

        case Format::RG32Float:
        case Format::RG32Uint:
        case Format::RG32Int:
            return DXGI_FORMAT_R32G32_TYPELESS;

        case Format::R32Float:
        case Format::R32Uint:
        case Format::R32Int:
            return DXGI_FORMAT_R32_TYPELESS;

        case Format::RGBA16Float:
        case Format::RGBA16Int:
        case Format::RGBA16Uint:
        case Format::RGBA16Unorm:
            return DXGI_FORMAT_R16G16B16A16_TYPELESS;

        case Format::RG16Float:
        case Format::RG16Int:
        case Format::RG16Uint:
        case Format::RG16Unorm:
            return DXGI_FORMAT_R16G16_TYPELESS;

        case Format::R16Float:
        case Format::R16Int:
        case Format::R16Uint:
        case Format::R16Unorm:
            return DXGI_FORMAT_R16_TYPELESS;

        case Format::RGBA8Int:
        case Format::RGBA8Snorm:
        case Format::RGBA8Uint:
        case Format::RGBA8Unorm:
        case Format::RGBA8UnormSrgb:
            return DXGI_FORMAT_R8G8B8A8_TYPELESS;

        case Format::RG8Int:
        case Format::RG8Snorm:
        case Format::RG8Uint:
        case Format::RG8Unorm:
            return DXGI_FORMAT_R8G8_TYPELESS;

        case Format::R8Int:
        case Format::R8Snorm:
        case Format::R8Uint:
        case Format::R8Unorm:
            return DXGI_FORMAT_R8_TYPELESS;

        case Format::RGB10A2Unorm:
        case Format::RGB10A2Uint:
            return DXGI_FORMAT_R10G10B10A2_TYPELESS;

        default:
            break;
        }

        Log::Fatal(L"Invalid format for typless format.");
        return DXGI_FORMAT_UNKNOWN;
    }

    D3D12_RESOURCE_FLAGS Resource::GetD3D12ResourceFlags(Resource::BindFlags flags)
    {
        using BindFlags = Resource::BindFlags;

        D3D12_RESOURCE_FLAGS d3d = D3D12_RESOURCE_FLAG_NONE;

        bool uavRequired = is_set(flags, BindFlags::UnorderedAccess) || is_set(flags, BindFlags::AccelerationStructure);

        if (uavRequired)
        {
            d3d |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }

        if (is_set(flags, BindFlags::DepthStencil))
        {
            if (is_set(flags, BindFlags::ShaderResource) == false)
            {
                d3d |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
            }
            d3d |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        }

        if (is_set(flags, BindFlags::RenderTarget))
        {
            d3d |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        }

        return d3d;
    }

    Resource::BindFlags Resource::GetBindFlags(D3D12_RESOURCE_FLAGS resourceFlags) {
        Resource::BindFlags bindFlags = Resource::BindFlags::None;

        if (resourceFlags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) {
            bindFlags |= BindFlags::RenderTarget;
            resourceFlags &= ~D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        }
        if (resourceFlags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) {
            bindFlags |= BindFlags::DepthStencil;
            resourceFlags &= ~D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        }
        if (resourceFlags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) {
            bindFlags |= BindFlags::UnorderedAccess;
            resourceFlags &= ~D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }
        if (resourceFlags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) {
            bindFlags |= BindFlags::ShaderResource;
            resourceFlags &= ~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
        }

        constexpr D3D12_RESOURCE_FLAGS NOP = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS | D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY;
        if (resourceFlags & NOP) {
            resourceFlags &= ~NOP;
        }

        assert(resourceFlags == 0 && "Not all shader flags accounted for.");

        return bindFlags;
    }

    D3D12_RESOURCE_DIMENSION Resource::GetResourceDimension(Resource::Type type)
    {
        using Type = Resource::Type;

        switch (type)
        {
        case Type::Buffer:
            return D3D12_RESOURCE_DIMENSION_BUFFER;

        case Type::Texture1D:
            return D3D12_RESOURCE_DIMENSION_TEXTURE1D;

        case Type::Texture2D:
        case Type::Texture2DMultisample:
        case Type::TextureCube:
            return D3D12_RESOURCE_DIMENSION_TEXTURE2D;

        case Type::Texture3D:
            return D3D12_RESOURCE_DIMENSION_TEXTURE3D;

        default:
            break;
        }

        Log::Fatal(L"Invalid resouce dimension detected.");
        return D3D12_RESOURCE_DIMENSION(-1);
    }

    Resource::Type Resource::GetResourceType(D3D12_RESOURCE_DIMENSION dimension)
    {
        switch (dimension)
        {
        case D3D12_RESOURCE_DIMENSION_BUFFER:
            return Resource::Type::Buffer;

        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            return Resource::Type::Texture1D;

        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            return Resource::Type::Texture2D;

        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            return Resource::Type::Texture3D;
        default:
            break;
        }

        Log::Fatal(L"Invalid resouce dimension detected.");
        return Resource::Type(-1);
    }

    const D3D12_HEAP_PROPERTIES Resource::m_defaultHeapProps =
    {
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        D3D12_MEMORY_POOL_UNKNOWN,
        0,
        0
    };

    const D3D12_HEAP_PROPERTIES Resource::m_uploadHeapProps =
    {
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        D3D12_MEMORY_POOL_UNKNOWN,
        0,
        0,
    };

    const D3D12_HEAP_PROPERTIES Resource::m_readbackHeapProps =
    {
        D3D12_HEAP_TYPE_READBACK,
        D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        D3D12_MEMORY_POOL_UNKNOWN,
        0,
        0
    };


    bool Resource::IsTexture(Resource::Type type) {
        return type == Type::Texture1D
            || type == Type::Texture2D
            || type == Type::Texture3D
            || type == Type::TextureCube
            || type == Type::Texture2DMultisample;
    }

    bool Resource::IsBuffer(Resource::Type type){
        return type == Type::Buffer;
    }

    Resource::~Resource()
    {
        if (m_apiData.m_resource) {
            Log::Fatal(L"ID3D12Resource was not released properly.");
        }
    }

    void Resource::SetName(const std::wstring& str)
    {
        SetNameInternal(m_apiData.m_resource, str.c_str());
    }

#else
    Resource::ApiResourceID Resource::GetApiResourceID() const {
        return reinterpret_cast<Resource::ApiResourceID>(m_apiData.m_image);
    }

    const std::array<Resource::VkFormatDesc, uint32_t(Resource::Format::Count)> Resource::m_VkFormatDesc =
    { {
        { Resource::Format::Unknown,                       VK_FORMAT_UNDEFINED },
        { Resource::Format::R8Unorm,                       VK_FORMAT_R8_UNORM },
        { Resource::Format::R8Snorm,                       VK_FORMAT_R8_SNORM },
        { Resource::Format::R16Unorm,                      VK_FORMAT_R16_UNORM },
        { Resource::Format::R16Snorm,                      VK_FORMAT_R16_SNORM },
        { Resource::Format::RG8Unorm,                      VK_FORMAT_R8G8_UNORM },
        { Resource::Format::RG8Snorm,                      VK_FORMAT_R8G8_SNORM },
        { Resource::Format::RG16Unorm,                     VK_FORMAT_R16G16_UNORM },
        { Resource::Format::RG16Snorm,                     VK_FORMAT_R16G16_SNORM },
        { Resource::Format::RGB16Unorm,                    VK_FORMAT_R16G16B16_UNORM },
        { Resource::Format::RGB16Snorm,                    VK_FORMAT_R16G16B16_SNORM },
        { Resource::Format::R24UnormX8,                    VK_FORMAT_UNDEFINED },
        { Resource::Format::RGB5A1Unorm,                   VK_FORMAT_B5G5R5A1_UNORM_PACK16 }, // VK different component order?
        { Resource::Format::RGBA8Unorm,                    VK_FORMAT_R8G8B8A8_UNORM },
        { Resource::Format::RGBA8Snorm,                    VK_FORMAT_R8G8B8A8_SNORM },
        { Resource::Format::RGB10A2Unorm,                  VK_FORMAT_A2R10G10B10_UNORM_PACK32 }, // VK different component order?
        { Resource::Format::RGB10A2Uint,                   VK_FORMAT_A2R10G10B10_UINT_PACK32 }, // VK different component order?
        { Resource::Format::RGBA16Unorm,                   VK_FORMAT_R16G16B16A16_UNORM },
        { Resource::Format::RGBA8UnormSrgb,                VK_FORMAT_R8G8B8A8_SRGB },
        { Resource::Format::R16Float,                      VK_FORMAT_R16_SFLOAT },
        { Resource::Format::RG16Float,                     VK_FORMAT_R16G16_SFLOAT },
        { Resource::Format::RGB16Float,                    VK_FORMAT_R16G16B16_SFLOAT },
        { Resource::Format::RGBA16Float,                   VK_FORMAT_R16G16B16A16_SFLOAT },
        { Resource::Format::R32Float,                      VK_FORMAT_R32_SFLOAT },
        { Resource::Format::R32FloatX32,                   VK_FORMAT_UNDEFINED },
        { Resource::Format::RG32Float,                     VK_FORMAT_R32G32_SFLOAT },
        { Resource::Format::RGB32Float,                    VK_FORMAT_R32G32B32_SFLOAT },
        { Resource::Format::RGBA32Float,                   VK_FORMAT_R32G32B32A32_SFLOAT },
        { Resource::Format::R11G11B10Float,                VK_FORMAT_B10G11R11_UFLOAT_PACK32 }, // Unsigned in VK
        { Resource::Format::RGB9E5Float,                   VK_FORMAT_E5B9G9R9_UFLOAT_PACK32 }, // Unsigned in VK
        { Resource::Format::R8Int,                         VK_FORMAT_R8_SINT },
        { Resource::Format::R8Uint,                        VK_FORMAT_R8_UINT },
        { Resource::Format::R16Int,                        VK_FORMAT_R16_SINT },
        { Resource::Format::R16Uint,                       VK_FORMAT_R16_UINT },
        { Resource::Format::R32Int,                        VK_FORMAT_R32_SINT },
        { Resource::Format::R32Uint,                       VK_FORMAT_R32_UINT },
        { Resource::Format::RG8Int,                        VK_FORMAT_R8G8_SINT },
        { Resource::Format::RG8Uint,                       VK_FORMAT_R8G8_UINT },
        { Resource::Format::RG16Int,                       VK_FORMAT_R16G16_SINT },
        { Resource::Format::RG16Uint,                      VK_FORMAT_R16G16_UINT },
        { Resource::Format::RG32Int,                       VK_FORMAT_R32G32_SINT },
        { Resource::Format::RG32Uint,                      VK_FORMAT_R32G32_UINT },
        { Resource::Format::RGB16Int,                      VK_FORMAT_R16G16B16_SINT },
        { Resource::Format::RGB16Uint,                     VK_FORMAT_R16G16B16_UINT },
        { Resource::Format::RGB32Int,                      VK_FORMAT_R32G32B32_SINT },
        { Resource::Format::RGB32Uint,                     VK_FORMAT_R32G32B32_UINT },
        { Resource::Format::RGBA8Int,                      VK_FORMAT_R8G8B8A8_SINT },
        { Resource::Format::RGBA8Uint,                     VK_FORMAT_R8G8B8A8_UINT },
        { Resource::Format::RGBA16Int,                     VK_FORMAT_R16G16B16A16_SINT },
        { Resource::Format::RGBA16Uint,                    VK_FORMAT_R16G16B16A16_UINT },
        { Resource::Format::RGBA32Int,                     VK_FORMAT_R32G32B32A32_SINT },
        { Resource::Format::RGBA32Uint,                    VK_FORMAT_R32G32B32A32_UINT },
        { Resource::Format::BGRA8Unorm,                    VK_FORMAT_B8G8R8A8_UNORM },
        { Resource::Format::BGRA8UnormSrgb,                VK_FORMAT_B8G8R8A8_SRGB },
        { Resource::Format::BGRX8Unorm,                    VK_FORMAT_B8G8R8A8_UNORM },
        { Resource::Format::BGRX8UnormSrgb,                VK_FORMAT_B8G8R8A8_SRGB },
        { Resource::Format::Alpha8Unorm,                   VK_FORMAT_UNDEFINED },
        { Resource::Format::Alpha32Float,                  VK_FORMAT_UNDEFINED },
        { Resource::Format::R5G6B5Unorm,                   VK_FORMAT_R5G6B5_UNORM_PACK16 },
        { Resource::Format::D32Float,                      VK_FORMAT_D32_SFLOAT },
        { Resource::Format::D16Unorm,                      VK_FORMAT_D16_UNORM },
        { Resource::Format::D32FloatS8X24,                 VK_FORMAT_D32_SFLOAT_S8_UINT },
        { Resource::Format::D24UnormS8,                    VK_FORMAT_D24_UNORM_S8_UINT },
        { Resource::Format::BC1Unorm,                      VK_FORMAT_BC1_RGB_UNORM_BLOCK },
        { Resource::Format::BC1UnormSrgb,                  VK_FORMAT_BC1_RGB_SRGB_BLOCK },
        { Resource::Format::BC2Unorm,                      VK_FORMAT_BC2_UNORM_BLOCK },
        { Resource::Format::BC2UnormSrgb,                  VK_FORMAT_BC2_SRGB_BLOCK },
        { Resource::Format::BC3Unorm,                      VK_FORMAT_BC3_UNORM_BLOCK },
        { Resource::Format::BC3UnormSrgb,                  VK_FORMAT_BC3_SRGB_BLOCK },
        { Resource::Format::BC4Unorm,                      VK_FORMAT_BC4_UNORM_BLOCK },
        { Resource::Format::BC4Snorm,                      VK_FORMAT_BC4_SNORM_BLOCK },
        { Resource::Format::BC5Unorm,                      VK_FORMAT_BC5_UNORM_BLOCK },
        { Resource::Format::BC5Snorm,                      VK_FORMAT_BC5_SNORM_BLOCK },
        { Resource::Format::BC6HS16,                       VK_FORMAT_BC6H_SFLOAT_BLOCK },
        { Resource::Format::BC6HU16,                       VK_FORMAT_BC6H_UFLOAT_BLOCK },
        { Resource::Format::BC7Unorm,                      VK_FORMAT_BC7_UNORM_BLOCK },
        { Resource::Format::BC7UnormSrgb,                  VK_FORMAT_BC7_SRGB_BLOCK },
    } };

    VkBufferUsageFlags Resource::GetBufferUsageFlag(Resource::BindFlags bindFlags)
    {
        // Assume every buffer can be read from and written into
        VkBufferUsageFlags flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        auto setBit = [&flags, &bindFlags](BindFlags f, VkBufferUsageFlags vkBit) {if (is_set(bindFlags, f)) flags |= vkBit; };

        setBit(BindFlags::Vertex, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        setBit(BindFlags::Index, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        setBit(BindFlags::UnorderedAccess, VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        setBit(BindFlags::ShaderResource, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT);
        setBit(BindFlags::IndirectArg, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        setBit(BindFlags::Constant, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

        setBit(BindFlags::AccelerationStructureBuildInput, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);

        setBit(BindFlags::AccelerationStructure, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR);

        setBit(BindFlags::ShaderDeviceAddress, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

        return flags;
    }

    VkImageUsageFlags Resource::GetImageUsageFlag(Resource::BindFlags bindFlags)
    {
        // Assume that every image can be updated/cleared, read from, and sampled
        VkImageUsageFlags flags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        auto setBit = [&flags, &bindFlags](BindFlags f, VkImageUsageFlags vkBit) {if (is_set(bindFlags, f)) flags |= vkBit; };

        setBit(BindFlags::UnorderedAccess, VK_IMAGE_USAGE_STORAGE_BIT);
        setBit(BindFlags::ShaderResource, VK_IMAGE_USAGE_SAMPLED_BIT);
        setBit(BindFlags::RenderTarget, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        setBit(BindFlags::DepthStencil, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

        return flags;
    }

    VkImageType Resource::GetVkImageType(Resource::Type type)
    {
        switch (type)
        {
        case Texture::Type::Texture1D:
            return VK_IMAGE_TYPE_1D;

        case Texture::Type::Texture2D:
        case Texture::Type::Texture2DMultisample:
        case Texture::Type::TextureCube:
            return VK_IMAGE_TYPE_2D;

        case Texture::Type::Texture3D:
            return VK_IMAGE_TYPE_3D;
        default:
            break;
        }

        Log::Fatal(L"Invalid image type detected.");
        return VkImageType(-1);
    }

    Texture::Type Resource::GetImageType(VkImageViewType type)
    {
        switch (type)
        {
        case VK_IMAGE_VIEW_TYPE_1D:
            return Texture::Type::Texture1D;
        case VK_IMAGE_VIEW_TYPE_2D:
            return Texture::Type::Texture2D;
        case VK_IMAGE_VIEW_TYPE_3D:
            return Texture::Type::Texture3D;
        case VK_IMAGE_VIEW_TYPE_CUBE:
            return Texture::Type::TextureCube;
        case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
        case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
        case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
        default:
            break;
        }

        Log::Fatal(L"Invalid image type detected.");
        return Texture::Type(-1);
    }

    VkImageLayout Resource::GetVkImageLayout(ResourceState::State state)
    {
        switch (state)
        {
        case ResourceState::State::Undefined:
            return VK_IMAGE_LAYOUT_UNDEFINED;
        case ResourceState::State::PreInitialized:
            return VK_IMAGE_LAYOUT_PREINITIALIZED;
        case ResourceState::State::Common:
        case ResourceState::State::UnorderedAccess:
            return VK_IMAGE_LAYOUT_GENERAL;
        case ResourceState::State::RenderTarget:
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case ResourceState::State::DepthStencil:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case ResourceState::State::ShaderResource:
        case ResourceState::State::NonPixelShader:
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case ResourceState::State::ResolveDest:
        case ResourceState::State::CopyDest:
            return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case ResourceState::State::ResolveSource:
        case ResourceState::State::CopySource:
            return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            break;
        case ResourceState::State::Present:
            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        default:
            break;
        }

        Log::Fatal(L"Invalid resource state detected.");
        return VkImageLayout(-1);
    }

    VkAccessFlagBits Resource::GetVkAccessMask(ResourceState::State state)
    {
        switch (state)
        {
        case ResourceState::State::Undefined:
        case ResourceState::State::Present:
        case ResourceState::State::Common:
        case ResourceState::State::PreInitialized:
        case ResourceState::State::GenericRead:
            return VkAccessFlagBits(0);
        case ResourceState::State::VertexBuffer:
            return VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        case ResourceState::State::ConstantBuffer:
            return VK_ACCESS_UNIFORM_READ_BIT;
        case ResourceState::State::IndexBuffer:
            return VK_ACCESS_INDEX_READ_BIT;
        case ResourceState::State::RenderTarget:
            return VkAccessFlagBits(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
        case ResourceState::State::UnorderedAccess:
            return VK_ACCESS_SHADER_WRITE_BIT;
        case ResourceState::State::DepthStencil:
            return VkAccessFlagBits(VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        case ResourceState::State::ShaderResource:
        case ResourceState::State::NonPixelShader:
            return VK_ACCESS_SHADER_READ_BIT; // modified here.
        case ResourceState::State::IndirectArg:
            return VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        case ResourceState::State::ResolveDest:
        case ResourceState::State::CopyDest:
            return VK_ACCESS_TRANSFER_WRITE_BIT;
        case ResourceState::State::ResolveSource:
        case ResourceState::State::CopySource:
            return VK_ACCESS_TRANSFER_READ_BIT;
        default:
            break;
        }

        Log::Fatal(L"Invalid resource state detected.");
        return VkAccessFlagBits(-1);
    }

    VkPipelineStageFlags Resource::GetVkPipelineStageMask(ResourceState::State state, bool src)
    {
        switch (state)
        {
        case ResourceState::State::Undefined:
        case ResourceState::State::PreInitialized:
        case ResourceState::State::Common:
        case ResourceState::State::VertexBuffer:
        case ResourceState::State::IndexBuffer:
        case ResourceState::State::UnorderedAccess:
        case ResourceState::State::ConstantBuffer:
        case ResourceState::State::ShaderResource:
        case ResourceState::State::RenderTarget:
        case ResourceState::State::DepthStencil:
        case ResourceState::State::IndirectArg:
        case ResourceState::State::CopyDest:
        case ResourceState::State::CopySource:
        case ResourceState::State::ResolveDest:
        case ResourceState::State::ResolveSource:
        case ResourceState::State::Present:
            // SDK only uses compute so there is no strong reason to specify fine grained pipeline stage.
            return src ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        default:
            break;
        }

        Log::Fatal(L"Invalid resource state detected.");
        return VkPipelineStageFlags(-1);
    }

    VkImageAspectFlags Resource::GetVkImageAspectFlags(Resource::Format format, bool ignoreStencil)
    {
		VkImageAspectFlags flags = 0;
		if (Resource::IsDepthFormat(format))      flags |= VK_IMAGE_ASPECT_DEPTH_BIT;
		if (ignoreStencil == false)
		{
			if (Resource::IsStencilFormat(format))    flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		if (Resource::IsDepthStencilFormat(format) == false) flags |= VK_IMAGE_ASPECT_COLOR_BIT;

		return flags;
    }

    bool Resource::AllocateDeviceMemory(Device *dev, Device::VulkanDeviceMemoryType memType, uint32_t /*memoryTypeBits*/, bool enableDeviceAddress, size_t size, VkDeviceMemory *mem)
    {
        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = size;
        allocInfo.memoryTypeIndex = dev->m_deviceMemoryTypeIndex[(uint32_t)memType];

        VkMemoryAllocateFlagsInfo flagInfo = {};
        flagInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        flagInfo.flags = 0;

        if (enableDeviceAddress)
            flagInfo.flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        allocInfo.pNext = &flagInfo;

        if (vkAllocateMemory(dev->m_apiData.m_device, &allocInfo, nullptr, mem) != VK_SUCCESS) {
            Log::Fatal(L"Failed to allocate vk memory.");
            return false;
        }

        return true;
    }

    Resource::~Resource()
    {
#if 0
        if (m_apiData.m_device || m_apiData.m_buffer || m_apiData.m_image || m_apiData.m_accelerationStructure || m_apiData.m_deviceMemory || m_apiData.m_deviceAddress) {
#else
        if (m_apiData.m_device || m_apiData.m_buffer || m_apiData.m_image || m_apiData.m_deviceMemory || m_apiData.m_deviceAddress) {
#endif
            Log::Fatal(L"Vk resource was not destroyed properly.");
        }
    }

    void Resource::SetName(const std::wstring& str)
    {
        if (m_apiData.m_buffer != 0)
            SetNameInternal(m_apiData.m_device, VkObjectType::VK_OBJECT_TYPE_BUFFER, (uint64_t)m_apiData.m_buffer, str.c_str());
        if (m_apiData.m_image != 0)
            SetNameInternal(m_apiData.m_device, VkObjectType::VK_OBJECT_TYPE_IMAGE, (uint64_t)m_apiData.m_image, str.c_str());
#if 0
        if (m_apiData.m_accelerationStructure != 0)
            SetNameInternal(m_apiData.m_device, VkObjectType::VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, (uint64_t)m_apiData.m_accelerationStructure, str.c_str());
#endif
        if (m_apiData.m_deviceMemory != 0)
            SetNameInternal(m_apiData.m_device, VkObjectType::VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)m_apiData.m_deviceMemory, str.c_str());
    }

#endif

    /***************************************************************
     * Abstraction for heaps.
     * D3D12Heap in D3D12
     * VkDeviceMemory in VK
     ***************************************************************/
#if defined(GRAPHICS_API_D3D12)
    Heap::~Heap()
    {
        if (m_apiData.m_heap)
            m_apiData.m_heap->Release();
    }

    bool Heap::Create(Device *dev, uint64_t sizeInBytes, Buffer::CpuAccess cpuAccess)
    {
        D3D12_HEAP_DESC desc = {};

        desc.SizeInBytes = sizeInBytes;

        if (cpuAccess == Buffer::CpuAccess::Write)
        {
            desc.Properties = Resource::m_uploadHeapProps;
        }
        else if (cpuAccess == Buffer::CpuAccess::Read) 
        {
            desc.Properties = Resource::m_readbackHeapProps;
        }
        else {
            desc.Properties = Resource::m_defaultHeapProps;
        }
        m_cpuAccess = cpuAccess;

        desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        desc.Flags = D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;

        HRESULT hr = dev->m_apiData.m_device->CreateHeap(&desc, IID_PPV_ARGS(&m_apiData.m_heap));
        if (FAILED(hr)) {
            Log::Fatal(L"Failed to create heap.");
            return false;
        }

        m_sizeInBytes = sizeInBytes;

        return true;
    }
#endif

#if defined(GRAPHICS_API_VK)
    Heap::~Heap()
    {
        if (m_apiData.m_deviceMemory && m_apiData.m_device)
            vkFreeMemory(m_apiData.m_device, m_apiData.m_deviceMemory, nullptr);

        m_apiData.m_deviceMemory = {};
        m_apiData.m_device = {};
    }

    bool Heap::Create(Device* dev, uint64_t sizeInBytes, Buffer::CpuAccess cpuAccess)
    {
        Device::VulkanDeviceMemoryType memType;
        if (cpuAccess == Buffer::CpuAccess::Write)
        {
            memType = Device::VulkanDeviceMemoryType::Upload;
        }
        else if (cpuAccess == Buffer::CpuAccess::Read)
        {
            memType = Device::VulkanDeviceMemoryType::Readback;
        }
        else
        {
            memType = Device::VulkanDeviceMemoryType::Default;
        }
        m_cpuAccess = cpuAccess;

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = sizeInBytes;
        allocInfo.memoryTypeIndex = dev->m_deviceMemoryTypeIndex[(uint32_t)memType];

        VkMemoryAllocateFlagsInfo flagInfo = {};
        if (cpuAccess == Buffer::CpuAccess::None) {
            // always enable address bit for default heaps.
            flagInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
            flagInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

            allocInfo.pNext = &flagInfo;
        }

        if (vkAllocateMemory(dev->m_apiData.m_device, &allocInfo, nullptr, &m_apiData.m_deviceMemory) != VK_SUCCESS) {
            Log::Fatal(L"Failed to allocate vk memory.");
            return false;
        }
        m_apiData.m_device = dev->m_apiData.m_device;

        m_sizeInBytes = sizeInBytes;

        return true;
    }
#endif

    /***************************************************************
     * Abstraction for texture resources.
     ***************************************************************/
#if defined(GRAPHICS_API_D3D12)
    Texture::~Texture()
    {
		if (m_apiData.m_resource && m_destructWithDestructor)
			m_apiData.m_resource->Release();
        m_apiData.m_resource = nullptr;
    }

    bool Texture::Create(Device* dev, Resource::Type type, Resource::Format format, Resource::BindFlags bindFlags,
        uint32_t width, uint32_t height, uint32_t depth, uint32_t arraySize, uint32_t mipLevels, uint32_t sampleCount)
    {
        m_type = type;
        m_format = format;
        m_bindFlags = bindFlags;
        SetGlobalState(ResourceState::State::Common);
        m_width = width;
        m_height = height;
        m_depth = depth;
        m_arraySize = arraySize;
        m_mipLevels = mipLevels;
        m_sampleCount = sampleCount;
        m_subresourceCount = SubresourceRange::CalcSubresource(mipLevels - 1, arraySize - 1, m_mipLevels) + 1;

        D3D12_RESOURCE_DESC desc = {};

        desc.MipLevels = (UINT16)m_mipLevels;
        desc.Format = Resource::GetDxgiFormat(m_format);
        desc.Width = m_width;
        desc.Height = m_height;
        desc.Flags = Resource::GetD3D12ResourceFlags(m_bindFlags);
        desc.SampleDesc.Count = m_sampleCount;
        desc.SampleDesc.Quality = 0;
        desc.Dimension = Resource::GetResourceDimension(m_type);
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Alignment = 0;

        if (m_type == Resource::Type::TextureCube)
        {
            desc.DepthOrArraySize = (UINT16)(m_arraySize * 6);
        }
        else if (m_type == Resource::Type::Texture3D)
        {
            desc.DepthOrArraySize = (UINT16)m_depth;
        }
        else
        {
            desc.DepthOrArraySize = (UINT16)m_arraySize;
        }
        assert(desc.Width > 0 && desc.Height > 0);
        assert(desc.MipLevels > 0 && desc.DepthOrArraySize > 0 && desc.SampleDesc.Count > 0);

        D3D12_CLEAR_VALUE clearValue = {};
        D3D12_CLEAR_VALUE* pClearVal = nullptr;
        if ((m_bindFlags & (Resource::BindFlags::RenderTarget | Resource::BindFlags::DepthStencil)) != Resource::BindFlags::None)
        {
            clearValue.Format = desc.Format;
            if ((m_bindFlags & Texture::BindFlags::DepthStencil) != Texture::BindFlags::None)
            {
                clearValue.DepthStencil.Depth = 1.0f;
            }
            pClearVal = &clearValue;
        }

        //If depth and either ua or sr, set to typeless
        if (Resource::IsDepthFormat(m_format) && is_set(m_bindFlags, Texture::BindFlags::ShaderResource | Texture::BindFlags::UnorderedAccess))
        {
            desc.Format = Resource::GetTypelessFormat(m_format);
            pClearVal = nullptr;
        }

        D3D12_HEAP_FLAGS heapFlags = is_set(m_bindFlags, Resource::BindFlags::Shared) ? D3D12_HEAP_FLAG_SHARED : D3D12_HEAP_FLAG_NONE;

        HRESULT hr = dev->m_apiData.m_device->CreateCommittedResource(&Resource::m_defaultHeapProps, heapFlags, &desc, D3D12_RESOURCE_STATE_COMMON, pClearVal, IID_PPV_ARGS(&m_apiData.m_resource));
        if (FAILED(hr)) {
            Log::Fatal(L"Failed to create a comitted resource");
            return false;
        }

        return true;
    };

    bool Texture::InitFromApiData(ApiData apiData, ResourceState::State state) {
        m_destructWithDestructor = false;
        m_apiData = apiData;
        D3D12_RESOURCE_DESC desc = m_apiData.m_resource->GetDesc();

        m_type = GetResourceType(desc.Dimension);
        m_bindFlags = GetBindFlags(desc.Flags);

        m_width = (uint32_t)desc.Width;
        m_height = (uint32_t)desc.Height;
        m_mipLevels = desc.MipLevels;
        m_sampleCount = desc.SampleDesc.Count;
        m_format = Resource::GetResourceFormat(desc.Format);
       // assert(m_format != Format::Unknown && "Unknown format");

        assert(desc.DepthOrArraySize == 1 && "We can distinquish between depth and array slices here...");
        m_depth = 1;
        m_arraySize = 1;

        m_subresourceCount = SubresourceRange::CalcSubresource(m_mipLevels - 1, m_arraySize - 1, m_mipLevels) + 1;

        SetGlobalState(state);

        return true;
    }

    bool Texture::GetUploadBufferFootplint(Device *dev, uint32_t /*subresourceIndex*/, uint32_t* rowPitchInBytes, uint32_t* totalSizeInBytes)
    {
        D3D12_RESOURCE_DESC desc = {};

        desc.MipLevels = (UINT16)m_mipLevels;
        desc.Format = Resource::GetDxgiFormat(m_format);
        desc.Width = m_width;
        desc.Height = m_height;
        desc.Flags = Resource::GetD3D12ResourceFlags(m_bindFlags);
        desc.SampleDesc.Count = m_sampleCount;
        desc.SampleDesc.Quality = 0;
        desc.Dimension = Resource::GetResourceDimension(m_type);
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Alignment = 0;

        if (m_type == Resource::Type::TextureCube)
        {
            desc.DepthOrArraySize = (UINT16)(m_arraySize * 6);
        }
        else if (m_type == Resource::Type::Texture3D)
        {
            desc.DepthOrArraySize = (UINT16)m_depth;
        }
        else
        {
            desc.DepthOrArraySize = (UINT16)m_arraySize;
        }
        assert(desc.Width > 0 && desc.Height > 0);
        assert(desc.MipLevels > 0 && desc.DepthOrArraySize > 0 && desc.SampleDesc.Count > 0);

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT uploadBufferFootprint;
        UINT numRows;
        UINT64 rowSizeInBytes, totalBytes;
        dev->m_apiData.m_device->GetCopyableFootprints(&desc, 0, 1, 0, &uploadBufferFootprint, &numRows, &rowSizeInBytes, &totalBytes);

        *rowPitchInBytes = uploadBufferFootprint.Footprint.RowPitch;
        *totalSizeInBytes = (uint32_t)totalBytes;

        return true;
    }

#elif defined(GRAPHICS_API_VK)
    Texture::~Texture()
    {
        if (m_destructWithDestructor) {
            if (m_apiData.m_device && m_apiData.m_image)
                vkDestroyImage(m_apiData.m_device, m_apiData.m_image, nullptr);
            if (m_apiData.m_deviceMemory && m_apiData.m_device)
                vkFreeMemory(m_apiData.m_device, m_apiData.m_deviceMemory, nullptr);
            m_apiData.m_image = {};
            m_apiData.m_deviceMemory = {};
            m_apiData.m_device = {};
        }
    }

    static VkFormatFeatureFlags getFormatFeatureBitsFromUsage(VkImageUsageFlags usage)
    {
        VkFormatFeatureFlags bits = 0;
        if (usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) bits |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
        if (usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) bits |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) bits |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if (usage & VK_IMAGE_USAGE_STORAGE_BIT) bits |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
        if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) bits |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
        if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) bits |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        assert((usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) == 0);
        assert((usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) == 0);
        return bits;
    }

    static VkImageTiling getFormatImageTiling(VkPhysicalDevice phDev, VkFormat format, VkImageUsageFlags usage)
    {
        VkFormatProperties p;
        vkGetPhysicalDeviceFormatProperties(phDev, format, &p);
        auto featureBits = getFormatFeatureBitsFromUsage(usage);
        if ((p.optimalTilingFeatures & featureBits) == featureBits) return VK_IMAGE_TILING_OPTIMAL;
        if ((p.linearTilingFeatures & featureBits) == featureBits) return VK_IMAGE_TILING_LINEAR;

        Log::Fatal(L"Invalid tiling feature detected.");
        return VkImageTiling(-1);
    }

    bool Texture::Create(Device* dev, Resource::Type type, Resource::Format format, Resource::BindFlags bindFlags,
                        uint32_t width, uint32_t height, uint32_t depth, uint32_t arraySize, uint32_t mipLevels, uint32_t sampleCount)
    {
        VkImageCreateInfo imageInfo = {};

        imageInfo.arrayLayers = arraySize;
        imageInfo.extent.depth = depth;
        imageInfo.extent.height = height;
        imageInfo.extent.width = width;
        imageInfo.format = Resource::GetVkFormat(format);
        imageInfo.imageType = Resource::GetVkImageType(type);
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;  //pData ? VK_IMAGE_LAYOUT_PREINITIALIZED : VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.mipLevels = mipLevels;
        imageInfo.pQueueFamilyIndices = nullptr;
        imageInfo.queueFamilyIndexCount = 0;
        imageInfo.samples = (VkSampleCountFlagBits)sampleCount;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.usage = Resource::GetImageUsageFlag(bindFlags);
        imageInfo.tiling = getFormatImageTiling(dev->m_apiData.m_physicalDevice, imageInfo.format, imageInfo.usage);

        if (type == Resource::Type::TextureCube)
        {
            imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            imageInfo.arrayLayers *= 6;
        }

        if (vkCreateImage(dev->m_apiData.m_device, &imageInfo, nullptr, &m_apiData.m_image) != VK_SUCCESS) {
            Log::Fatal(L"Failed to create a vkImage");
            return false;
        }

        // Allocate the GPU memory
        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(dev->m_apiData.m_device, m_apiData.m_image, &memRequirements);

        if (! Resource::AllocateDeviceMemory(dev, Device::VulkanDeviceMemoryType::Default, memRequirements.memoryTypeBits, false, memRequirements.size, &m_apiData.m_deviceMemory)) {
            Log::Fatal(L"Failed to allocate vk device memory");
            return false;
        }
        if (vkBindImageMemory(dev->m_apiData.m_device, m_apiData.m_image, m_apiData.m_deviceMemory, 0) != VK_SUCCESS) {
            Log::Fatal(L"Failed to bind vk device memory to an image");
            return false;
        }

        m_apiData.m_device = dev->m_apiData.m_device;
        m_type = type;
        m_bindFlags = bindFlags;
        SetGlobalState(ResourceState::State::Undefined); // pData ? ResourceState::State::PreInitialized : ResourceState::State::Undefined;

        m_width = width;
        m_height = height;
        m_depth = depth;
        m_mipLevels = mipLevels;
        m_sampleCount = sampleCount;
        m_arraySize = arraySize;
        m_format = format;

        return true;
    }

    bool Texture::InitFromApiData(
        VkDevice device,
        VkImage image,
        VkImageViewType imageViewType,
        VkFormat format,
        uint32_t mipCount,
        uint32_t layerCount,
        ResourceState::State state) {

        m_destructWithDestructor = false;
        m_apiData.m_device = device;
        m_apiData.m_image = image;
        m_type = Resource::GetImageType(imageViewType);
        m_bindFlags = (BindFlags)0;// Resource::GetImageUsageFlag(bindFlags);;
        SetGlobalState(state);

        m_width = 0xffffffff;
        m_height = 0xffffffff;
        m_depth = 0xffffffff;
        m_mipLevels = mipCount;
        m_sampleCount = 1;
        m_arraySize = layerCount;
        m_format = GetResourceFormat(format);

        return true;
    }

    bool Texture::GetUploadBufferFootplint(Device* /*dev*/, uint32_t subresourceIndex, uint32_t* rowPitchInBytes, uint32_t* totalSizeInBytes)
    {
        if (subresourceIndex != 0) {
            Log::Fatal(L"subresourceIndex != 0 is unsupported.");
            return false;
        }
        switch (m_type) {
        case Type::Texture1D:
        case Type::Texture2D:
        case Type::Texture3D:
            break;
        default:
            Log::Fatal(L"Unsupported dimension (type) detected.");
            return false;
        }

        uint32_t  pixelInBytes = GetFormatBytesPerBlock(m_format);
        if (pixelInBytes <= 0) {
            Log::Fatal(L"Invalid format detected.");
            return false;
        }

        *rowPitchInBytes = m_width * pixelInBytes;
        *totalSizeInBytes = *rowPitchInBytes * m_height * m_depth * m_arraySize;

        return true;
    }

#endif

    /***************************************************************
     * Abstraction for buffer resources.
    ***************************************************************/
#if defined(GRAPHICS_API_D3D12)
    Buffer::~Buffer()
    {
        if (m_apiData.m_resource && m_destructWithDestructor)
            m_apiData.m_resource->Release();

        m_apiData.m_resource = nullptr;
    }

    bool Buffer::Create(Device* dev,
        uint64_t sizeInBytesOrNumberOfElements, Resource::Format format,
        Heap* heap, uint64_t heapOffsetInBytes, uint64_t heapAllocatedSizeInByte,
        Resource::BindFlags bindFlags,
        Buffer::CpuAccess cpuAccess)
    {
        if (cpuAccess != CpuAccess::None && is_set(bindFlags, BindFlags::Shared))
        {
            Log::Fatal(L"Can't create shared resource with CPU access other than 'None'.");
            return false;
        }

        size_t size = sizeInBytesOrNumberOfElements;
        if (format != Resource::Format::Unknown) {
            size *= Resource::GetFormatBytesPerBlock(format);
        }

        if (heap != nullptr) {
            // restrictions for placed buffer.
            if (heap->m_cpuAccess != cpuAccess) {
                Log::Fatal(L"Cpu access flag was inconsistent.");
                return false;
            }
            if (is_set(bindFlags, BindFlags::Constant)) {
                Log::Fatal(L"Constant buffer isn't supported by placed resource.");
                return false;
            }
            if (is_set(m_bindFlags, Resource::BindFlags::Shared)) {
                Log::Fatal(L"Shared resource buffer isn't supported by placed resource.");
                return false;
            }
            if (heapAllocatedSizeInByte < size) {
                Log::Fatal(L"Heap allocation was insufficient.");
                return false;
            }
        }

        if (bindFlags == BindFlags::Constant)
        {
            m_sizeInBytes = ALIGN((uint64_t)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, size);
        }
        else {
            m_sizeInBytes = size;
        }

        m_bindFlags = bindFlags;
        m_cpuAccess = cpuAccess;
        m_format = Format::Unknown;

        const D3D12_HEAP_PROPERTIES *hp = nullptr;

        if (cpuAccess == CpuAccess::Write)
        {
            SetGlobalState(ResourceState::State::GenericRead);
            hp = &Resource::m_uploadHeapProps;
        }
        else if (cpuAccess == CpuAccess::Read && bindFlags == BindFlags::None)
        {
            SetGlobalState(ResourceState::State::CopyDest);
            hp = &Resource::m_readbackHeapProps;
        }
        else
        {
            SetGlobalState(ResourceState::State::Common);
            if (is_set(bindFlags, BindFlags::AccelerationStructure))
                SetGlobalState(ResourceState::State::AccelerationStructure);
            else if (is_set(bindFlags, BindFlags::UnorderedAccess))
                SetGlobalState(ResourceState::State::UnorderedAccess);
            hp = &Resource::m_defaultHeapProps;
        }

        {
            // Create the buffer
            D3D12_RESOURCE_DESC bufDesc = {};
            bufDesc.Alignment = 0;
            bufDesc.DepthOrArraySize = 1;
            bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bufDesc.Flags = Resource::GetD3D12ResourceFlags(m_bindFlags);
            bufDesc.Format = DXGI_FORMAT_UNKNOWN;
            bufDesc.Height = 1;
            bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            bufDesc.MipLevels = 1;
            bufDesc.SampleDesc.Count = 1;
            bufDesc.SampleDesc.Quality = 0;
            bufDesc.Width = m_sizeInBytes;
            assert(bufDesc.Width > 0);

            D3D12_RESOURCE_STATES d3dState = ResourceState::GetD3D12ResourceState(GetGlobalState());
            D3D12_HEAP_FLAGS heapFlags = is_set(m_bindFlags, Resource::BindFlags::Shared) ? D3D12_HEAP_FLAG_SHARED : D3D12_HEAP_FLAG_NONE;
            if (is_set(m_bindFlags, Resource::BindFlags::AllowShaderAtomics))
                heapFlags |= D3D12_HEAP_FLAG_ALLOW_SHADER_ATOMICS;

            HRESULT hr;
            if (heap == nullptr) {
                hr = dev->m_apiData.m_device->CreateCommittedResource(hp, heapFlags, &bufDesc, d3dState, nullptr, IID_PPV_ARGS(&m_apiData.m_resource));
            }
            else {
                hr = dev->m_apiData.m_device->CreatePlacedResource(heap->m_apiData.m_heap, heapOffsetInBytes, &bufDesc, d3dState, nullptr, IID_PPV_ARGS(&m_apiData.m_resource));
            }
            if (FAILED(hr)) {
                Log::Fatal(L"Faild to allocate a buffer");
                return false;
            }
        }

        if (format != Resource::Format::Unknown) {
            // set format and element count here for SRV/UAV.
            m_format = format;
            m_elementCount = (uint32_t)sizeInBytesOrNumberOfElements;
        }
        else {
            m_format = Resource::Format::Unknown;
            m_elementCount = 0;
        }

        return true;
    }

    bool Buffer::Create(Device* dev,
        uint64_t sizeInBytesOrNumberOfElements, Resource::Format format,
        Resource::BindFlags bindFlags,
        Buffer::CpuAccess cpuAccess)
    {
        return Create(dev,
            sizeInBytesOrNumberOfElements, format,
            nullptr, 0, 0,
            bindFlags,
            cpuAccess);
    }

#if 0
    bool Buffer::CreateStructured(Device* dev, uint32_t structSize, uint32_t elementCount,
        Resource::BindFlags bindFlags, CpuAccess cpuAccess)
    {
        uint32_t size = structSize * elementCount;

        if (!Create(dev, size, bindFlags, cpuAccess)) {
            Log::Fatal(L"Faild to create structured buffer.");
            return false;
        }

        m_elementCount = elementCount;
        m_structSizeInBytes = structSize;

        return true;
    }
#endif

    uint64_t Buffer::GetGpuAddress() const
    {
        D3D12_GPU_VIRTUAL_ADDRESS adr = m_apiData.m_resource->GetGPUVirtualAddress();
        return adr;
    }

    void* Buffer::Map(Device* /*dev*/, Buffer::MapType type, uint32_t subResourceIndex, uint64_t readRangeBegin, uint64_t readRangeEnd)
    {
        void* mappedPtr = nullptr;
        D3D12_RANGE readRange = { 0, 0 };

        switch (type) {
        case MapType::Read:
        case MapType::Write:
            readRange = { readRangeBegin, readRangeEnd };
            break;
        case MapType::WriteDiscard:
        default:
            break;
        }
        if (FAILED(m_apiData.m_resource->Map(subResourceIndex, &readRange, &mappedPtr))) {
            Log::Fatal(L"Faild to map buffer, probably device has been removed for some reason.");
            return nullptr;
        }

#if 0
        return mappedPtr;
#else
        // D3D12 doesn't make offset for readrange, on the other hand, VK does.
        intptr_t offsetPtr = reinterpret_cast<intptr_t>(mappedPtr) + readRangeBegin;
        return reinterpret_cast<void *>(offsetPtr);
#endif
    }

    /** Unmap the buffer
    */
    void Buffer::Unmap(Device* /*dev*/, uint32_t subResourceIndex, uint64_t writeRangeBegin, uint64_t writeRangeEnd)
    {
        D3D12_RANGE wroteRange = { writeRangeBegin, writeRangeEnd };
        m_apiData.m_resource->Unmap(subResourceIndex, &wroteRange);
    }

#elif defined(GRAPHICS_API_VK)
    Buffer::~Buffer()
    {
        if (m_destructWithDestructor) {
#if 0
            if (m_apiData.m_accelerationStructure && m_apiData.m_device)
                VK::vkDestroyAccelerationStructureKHR(m_apiData.m_device, m_apiData.m_accelerationStructure, nullptr);
#endif
            if (m_apiData.m_buffer && m_apiData.m_device)
                vkDestroyBuffer(m_apiData.m_device, m_apiData.m_buffer, nullptr);
            if (m_apiData.m_deviceMemory && m_apiData.m_device && m_apiData.m_deviceMemoryOffset == uint64_t(-1))
                vkFreeMemory(m_apiData.m_device, m_apiData.m_deviceMemory, nullptr);

#if 0
            m_apiData.m_accelerationStructure = {};
#endif
            m_apiData.m_buffer = {};
            m_apiData.m_deviceMemory = {};
            m_apiData.m_deviceAddress = {};
            m_apiData.m_device = {};
        }
    }

    bool Buffer::Create(Device* dev,
        uint64_t sizeInBytesOrNumberOfElements, Resource::Format format,
        Heap* heap, uint64_t heapOffsetInBytes, uint64_t heapAllocatedSizeInByte,
        Resource::BindFlags bindFlags,
        CpuAccess cpuAccess)
    {
        uint64_t sizeInBytes = sizeInBytesOrNumberOfElements;
        if (format != Resource::Format::Unknown) {
            sizeInBytes *= Resource::GetFormatBytesPerBlock(format);
        }

        if (heap != nullptr) {
            if (heap->m_cpuAccess != cpuAccess) {
                Log::Fatal(L"Cpu access flag was inconsistent.");
                return false;
            }
            if (is_set(bindFlags, BindFlags::Constant)) {
                Log::Fatal(L"Constant buffer isn't supported by placed resource.");
                return false;
            }
            if (is_set(m_bindFlags, Resource::BindFlags::Shared)) {
                Log::Fatal(L"Shared resource buffer isn't supported by placed resource.");
                return false;
            }
        }

		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.flags = 0;
		bufferInfo.size = sizeInBytes;
		bufferInfo.usage = Resource::GetBufferUsageFlag(bindFlags);
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		bufferInfo.queueFamilyIndexCount = 0;
		bufferInfo.pQueueFamilyIndices = nullptr;

		if (vkCreateBuffer(dev->m_apiData.m_device, &bufferInfo, nullptr, &m_apiData.m_buffer) != VK_SUCCESS) {
			Log::Fatal(L"Faild to create vkBuffer.");
			return false;
		}

		// Get the required buffer size
		VkMemoryRequirements reqs;
		vkGetBufferMemoryRequirements(dev->m_apiData.m_device, m_apiData.m_buffer, &reqs);

        Device::VulkanDeviceMemoryType memType;
        if (cpuAccess == CpuAccess::Write)
        {
            SetGlobalState(ResourceState::State::GenericRead);
            memType = Device::VulkanDeviceMemoryType::Upload;
        }
        else if (cpuAccess == CpuAccess::Read && bindFlags == BindFlags::None)
        {
            SetGlobalState(ResourceState::State::CopyDest);
            memType = Device::VulkanDeviceMemoryType::Readback;
        }
        else
        {
            SetGlobalState(ResourceState::State::Common);
            if (is_set(bindFlags, BindFlags::AccelerationStructure))
                SetGlobalState(ResourceState::State::AccelerationStructure);
            else if (is_set(bindFlags, BindFlags::UnorderedAccess))
                SetGlobalState(ResourceState::State::UnorderedAccess);

            memType = Device::VulkanDeviceMemoryType::Default;
        }

        bool enableDeviceAddress = is_set(bindFlags, Resource::BindFlags::ShaderDeviceAddress);

        if (heap != nullptr) {
            if (reqs.size > heapAllocatedSizeInByte) {
                Log::Fatal(L"Heap allocation was insufficient.");
                return false;
            }
            if (heapAllocatedSizeInByte % reqs.alignment > 0 ||
                heapOffsetInBytes % reqs.alignment > 0) {
                Log::Fatal(L"Heap allocation alignment was not meet the request.");
                return false;
            }

            if (vkBindBufferMemory(dev->m_apiData.m_device, m_apiData.m_buffer, heap->m_apiData.m_deviceMemory, heapOffsetInBytes) != VK_SUCCESS) {
                Log::Fatal(L"Faild to bind buffer memory.");
                return false;
            }

            m_apiData.m_deviceMemory = heap->m_apiData.m_deviceMemory;
            m_apiData.m_deviceMemoryOffset = heapOffsetInBytes;
        }
        else {
            if (!Resource::AllocateDeviceMemory(dev, memType, reqs.memoryTypeBits, enableDeviceAddress, reqs.size, &m_apiData.m_deviceMemory)) {
                Log::Fatal(L"Faild to allocate device memory.");
                return false;
            }
            if (vkBindBufferMemory(dev->m_apiData.m_device, m_apiData.m_buffer, m_apiData.m_deviceMemory, 0) != VK_SUCCESS) {
                Log::Fatal(L"Faild to bind buffer memory.");
                return false;
            }
            m_apiData.m_deviceMemoryOffset = uint64_t(-1);
        }

        m_apiData.m_device = dev->m_apiData.m_device;
        m_sizeInBytes = sizeInBytes;
        m_bindFlags = bindFlags;
        m_cpuAccess = cpuAccess;
        m_format = format;
        m_type = Type::Buffer;

        // set format and element count here for SRV/UAV.
        if (format != Resource::Format::Unknown) {
            m_elementCount = (uint32_t)sizeInBytesOrNumberOfElements;
        }
        else {
            m_elementCount = 0;
        }

        if (enableDeviceAddress) {
            VkBufferDeviceAddressInfo    info = {};
            info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            info.buffer = m_apiData.m_buffer;
            m_apiData.m_deviceAddress = vkGetBufferDeviceAddress(dev->m_apiData.m_device, &info);
        }
        else {
            m_apiData.m_deviceAddress = 0xFFFF'FFFF'FFFF'FFFFull;
        }

#if 0
        if (is_set(bindFlags, Resource::BindFlags::AccelerationStructure)) {
            VkAccelerationStructureCreateInfoKHR acInfo = {};
            acInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
            acInfo.createFlags = VK_ACCELERATION_STRUCTURE_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT_KHR;
            acInfo.buffer = m_apiData.m_buffer;
            acInfo.offset = 0;
            acInfo.size = m_sizeInBytes;
            acInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR;
            acInfo.deviceAddress = m_apiData.m_deviceAddress;

            if (VK::vkCreateAccelerationStructureKHR(dev->m_apiData.m_device, &acInfo, nullptr, &m_apiData.m_accelerationStructure) != VK_SUCCESS) {
                Log::Fatal(L"Faild to create a acceleration structure.");
                return false;
            }
        }
#endif

        return true;
    }

    bool Buffer::Create(Device* dev,
        uint64_t sizeInBytesOrNumberOfElements, Resource::Format format,
        Resource::BindFlags bindFlags,
        Buffer::CpuAccess cpuAccess)
    {
        return Create(dev,
            sizeInBytesOrNumberOfElements, format,
            nullptr, 0, 0,
            bindFlags,
            cpuAccess);
    }

    uint64_t Buffer::GetGpuAddress() const
    {
        return m_apiData.m_deviceAddress;
    }

    void* Buffer::Map(Device *dev, Buffer::MapType type, uint32_t subResourceIndex, uint64_t readRangeBegin, uint64_t readRangeEnd)
    {
        if (subResourceIndex > 0) {
            Log::Fatal(L"Mapping subresourceIndex != 0 isn't supported.");
            return nullptr;
        }
        void* mappedPtr = nullptr;

        VkDeviceSize offset = 0;
        VkDeviceSize size = VK_WHOLE_SIZE;

        switch (type) {
        case MapType::Read:
        case MapType::Write:
            offset = readRangeBegin;
            size = readRangeEnd - readRangeBegin;
            break;
        case MapType::WriteDiscard:
        default:
            break;
        };

        if (m_apiData.m_deviceMemoryOffset != uint64_t(-1)) {
            offset += m_apiData.m_deviceMemoryOffset;
            if (type == MapType::WriteDiscard) {
                Log::Fatal(L"Placed resource doesn't support wirte discard map().");
                return nullptr;
            }
        }

        if (vkMapMemory(dev->m_apiData.m_device, m_apiData.m_deviceMemory, offset, size, 0, &mappedPtr) != VK_SUCCESS) {
            Log::Fatal(L"Faild to map buffer.");
            return nullptr;
        }

        return mappedPtr;
    }

    void Buffer::Unmap(Device* dev, uint32_t subResourceIndex, uint64_t /*writeRangeBegin*/, uint64_t /*writeRangeEnd*/)
    {
        if (subResourceIndex > 0) {
            Log::Fatal(L"Mapping subresourceIndex != 0 isn't supported.");
        }

        vkUnmapMemory(dev->m_apiData.m_device, m_apiData.m_deviceMemory);
    }


#endif

    /***************************************************************
     * Abstraction for shader resource view
     ***************************************************************/
#if defined(GRAPHICS_API_D3D12)
    ShaderResourceView::~ShaderResourceView()
    {
    };

    bool ShaderResourceView::InitNullView(Resource::Type type, bool isArray)
    {
        m_apiData = {};
        m_isNullView = true;
        m_nullViewType = type;
        m_nullIsArray = isArray;

        auto GetSRVResourceDimension = [&]() -> D3D12_SRV_DIMENSION
        {
            using Type = Resource::Type;

            switch (type)
            {
            case Type::Buffer:
                return D3D12_SRV_DIMENSION_BUFFER;
                break;

            case Type::Texture1D:
                if (!isArray)
                    return D3D12_SRV_DIMENSION_TEXTURE1D;
                else
                    return D3D12_SRV_DIMENSION_TEXTURE1DARRAY;

            case Type::Texture2D:
                if (!isArray)
                    return D3D12_SRV_DIMENSION_TEXTURE2D;
                else
                    return D3D12_SRV_DIMENSION_TEXTURE2DARRAY;

            case Type::TextureCube:
                if (!isArray)
                    return D3D12_SRV_DIMENSION_TEXTURECUBE;
                else
                    return D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;

            case Type::Texture3D:
                return D3D12_SRV_DIMENSION_TEXTURE3D;

            default:
                break;
            }

            Log::Fatal(L"Invalid UAV dimension detected.");
            return D3D12_SRV_DIMENSION(-1);
        };

        m_apiData.m_desc = { DXGI_FORMAT_R8G8B8A8_UNORM, GetSRVResourceDimension(), D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING };

        return true;
    }

    void ShaderResourceView::InitFromApiData(ID3D12Resource *resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc)
    {
        m_apiData.m_resource = resource;
        m_apiData.m_desc = *desc;

        m_isNullView = false;
    }

    bool ShaderResourceView::Init(Device* /*dev*/, Texture *tex, uint32_t mostDetailedMip, uint32_t mipCount, uint32_t firstArraySlice, uint32_t arraySize)
    {
        auto GetSRVResourceDimension = [&]() -> D3D12_SRV_DIMENSION
        {
            using Type = Resource::Type;

            switch (tex->m_type)
            {
            case Type::Buffer:
                //return D3D12_SRV_DIMENSION_BUFFER;
                break;

            case Type::Texture1D:
                if (tex->m_arraySize == 1)
                    return D3D12_SRV_DIMENSION_TEXTURE1D;
                else
                    return D3D12_SRV_DIMENSION_TEXTURE1DARRAY;

            case Type::Texture2D:
                if (tex->m_arraySize == 1)
                    return D3D12_SRV_DIMENSION_TEXTURE2D;
                else
                    return D3D12_SRV_DIMENSION_TEXTURE2DARRAY;

            case Type::Texture2DMultisample:
                if (tex->m_arraySize == 1)
                    return D3D12_SRV_DIMENSION_TEXTURE2DMS;
                else
                    return D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;

            case Type::TextureCube:
                if (tex->m_arraySize == 1)
                    return D3D12_SRV_DIMENSION_TEXTURECUBE;
                else
                    return D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;

            case Type::Texture3D:
                return D3D12_SRV_DIMENSION_TEXTURE3D;

            default:
                break;
            }

            Log::Fatal(L"Invalid SRV dimension detected.");
            return D3D12_SRV_DIMENSION(-1);
        };

        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {Resource::GetDxgiFormat(tex->m_format), GetSRVResourceDimension(),  D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING };
        
        switch (desc.ViewDimension) {
        case D3D12_SRV_DIMENSION_TEXTURE1D:
            desc.Texture1D.MostDetailedMip = mostDetailedMip;
            desc.Texture1D.MipLevels = mipCount;
            desc.Texture1D.ResourceMinLODClamp = 0.f;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
            desc.Texture1DArray.MostDetailedMip = mostDetailedMip;
            desc.Texture1DArray.MipLevels = mipCount;
            desc.Texture1DArray.ResourceMinLODClamp = 0.f;
            desc.Texture1DArray.FirstArraySlice = firstArraySlice;
            desc.Texture1DArray.ArraySize = arraySize;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE2D:
            desc.Texture2D.MostDetailedMip = mostDetailedMip;
            desc.Texture2D.MipLevels = mipCount;
            desc.Texture2D.ResourceMinLODClamp = 0.f;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
            desc.Texture2DArray.MostDetailedMip = mostDetailedMip;
            desc.Texture2DArray.MipLevels = mipCount;
            desc.Texture2DArray.ResourceMinLODClamp = 0.f;
            desc.Texture2DArray.FirstArraySlice = firstArraySlice;
            desc.Texture2DArray.ArraySize = arraySize;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE2DMS:
            break;
        case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY:
            desc.Texture2DMSArray.ArraySize = arraySize;
            desc.Texture2DMSArray.FirstArraySlice = firstArraySlice;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE3D:
            desc.Texture3D.MostDetailedMip = mostDetailedMip;
            desc.Texture3D.MipLevels = mipCount;
            desc.Texture3D.ResourceMinLODClamp = 0.f;
            break;
        case D3D12_SRV_DIMENSION_TEXTURECUBE:
            desc.TextureCube.MipLevels = mipCount;
            desc.TextureCube.MostDetailedMip = mostDetailedMip;
            break;
        case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
            desc.TextureCubeArray.First2DArrayFace = 0;
            desc.TextureCubeArray.NumCubes = arraySize;
            desc.TextureCubeArray.MipLevels = mipCount;
            desc.TextureCubeArray.MostDetailedMip = mostDetailedMip;
            break;
        default:
            Log::Fatal(L"Invalid SRV dimension detected.");
            return false;
        };

        m_apiData.m_desc = desc;
        m_apiData.m_resource = tex->m_apiData.m_resource;

        m_isNullView = false;

        return true;
    }

    bool ShaderResourceView::Init(Device *dev, Texture *tex)
    {
        // SRV for entire resource.
        return Init(dev, tex, 0, tex->m_mipLevels, 0, tex->m_arraySize);
    }

    bool ShaderResourceView::Init(Device* /*dev*/, Buffer *buf, uint32_t firstElement, uint32_t elementCount)
    {
		D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};

		uint32_t bufferElementSize = 0;
		uint32_t bufferElementCount = 0;

		if (is_set(buf->m_bindFlags, Resource::BindFlags::AccelerationStructure)) {
			bufferElementSize = buf->m_format == Resource::Format::Unknown ? 1 : Resource::GetFormatBytesPerBlock(buf->m_format);
			bufferElementCount = buf->m_elementCount;

			desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
			desc.Format = DXGI_FORMAT_UNKNOWN;
			desc.RaytracingAccelerationStructure.Location = buf->GetGpuAddress() + (uint64_t)bufferElementSize * (uint64_t)firstElement;
		}
        else if (buf->m_format != Resource::Format::Unknown) {
			// typed
			bufferElementSize = Resource::GetFormatBytesPerBlock(buf->m_format);
			bufferElementCount = buf->m_elementCount;

			desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			desc.Format = Resource::GetDxgiFormat(buf->m_format);
		}
		else if (buf->m_structSizeInBytes > 0) {
			// structured
			bufferElementSize = buf->m_structSizeInBytes;
			bufferElementCount = buf->m_elementCount;

			desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			desc.Format = DXGI_FORMAT_UNKNOWN;
			desc.Buffer.StructureByteStride = buf->m_structSizeInBytes;
		}
		else {
			// raw
			bufferElementSize = sizeof(uint32_t);
			bufferElementCount = (uint32_t)(buf->m_sizeInBytes / sizeof(uint32_t));

			desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			desc.Format = DXGI_FORMAT_R32_TYPELESS;
			desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
		}

		if (elementCount == 0xFFFFFFFF) {
			// to the last element.
			elementCount = bufferElementCount - firstElement;
		}

		// check range.
		assert((firstElement + elementCount) <= bufferElementCount);
		assert(bufferElementSize > 0);

        // set element range for buffer view.
        if (desc.ViewDimension == D3D12_SRV_DIMENSION_BUFFER) {
            desc.Buffer.FirstElement = firstElement;
            desc.Buffer.NumElements = elementCount;
        }
		desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        m_apiData.m_desc = desc;
        m_apiData.m_resource = buf->m_apiData.m_resource;

        m_isNullView = false;

        return true;
    }

    bool ShaderResourceView::Init(Device *dev, Buffer *buf)
    {
        return Init(dev, buf, 0, 0xFFFFFFFF);
    }
#elif defined(GRAPHICS_API_VK)

    ShaderResourceView::~ShaderResourceView()
    {
        if (m_apiData.m_device && m_apiData.m_typedBufferView) {
            vkDestroyBufferView(m_apiData.m_device, m_apiData.m_typedBufferView, nullptr);
        }
        if (m_apiData.m_device && m_apiData.m_imageView) {
            vkDestroyImageView(m_apiData.m_device, m_apiData.m_imageView, nullptr);
        }
        if (m_apiData.m_device && m_apiData.m_accelerationStructure) {
            VK::vkDestroyAccelerationStructureKHR(m_apiData.m_device, m_apiData.m_accelerationStructure, nullptr);
        }
        m_apiData = {};
    }

    void ShaderResourceView::InitFromApiData(VkBuffer rawBuffer, uint64_t rawOffsetInBytes, uint64_t rawSizeInBytes)
    {
        m_apiData.m_device = {};
        m_apiData.m_rawBuffer = rawBuffer;
        m_apiData.m_isTypedBufferView = false;
        m_apiData.m_typedBufferView = {};
        m_apiData.m_imageView = {};
        m_apiData.m_rawOffsetInBytes = rawOffsetInBytes;
        m_apiData.m_rawSizeInBytes = rawSizeInBytes;
 
        m_isNullView = false;
    }

    bool ShaderResourceView::InitFromApiData(Device *dev, VkBuffer typedBuffer, VkFormat nativeFmt, uint64_t offsetInBytes, uint64_t sizeInBytes)
    {
		// Create views for TypedBuffers
        Resource::Format fmt = Resource::GetResourceFormat(nativeFmt);
		uint32_t bufferElementSize = Resource::GetFormatBytesPerBlock(fmt);
		m_apiData.m_rawOffsetInBytes = offsetInBytes;
        m_apiData.m_rawSizeInBytes = sizeInBytes;

        if (sizeInBytes % bufferElementSize != 0) {
            Log::Fatal(L"Faild to init SRV. Buffer size was not a multiple of element size. ElmSize:%d BufSize:%d", bufferElementSize, sizeInBytes);
            return false;
        }

		VkBufferViewCreateInfo cInfo = {};
		cInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
		cInfo.buffer = typedBuffer;
		cInfo.offset = m_apiData.m_rawOffsetInBytes;
		cInfo.range = m_apiData.m_rawSizeInBytes;
		cInfo.format = nativeFmt;

        //assert(m_apiData.m_rawOffsetInBytes % 16 == 0);

		if (vkCreateBufferView(dev->m_apiData.m_device, &cInfo, nullptr, &m_apiData.m_typedBufferView) != VK_SUCCESS) {
			Log::Fatal(L"Failed to create a typed buffer view");
			return false;
		}
		m_apiData.m_isTypedBufferView = true;
		m_apiData.m_device = dev->m_apiData.m_device;

        m_isNullView = false;

        return true;
    };

    bool ShaderResourceView::InitFromApiData(Device* dev, VkImage image, VkImageViewType imageType, VkFormat fmt, VkImageAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t mipCount, uint32_t baseArrayLayer, uint32_t layerCount)
    {
        VkImageViewCreateInfo info = {};

        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = image;
        info.viewType = imageType;
        info.format = fmt;
        info.subresourceRange.aspectMask = aspectMask;
        info.subresourceRange.baseMipLevel = baseMipLevel;
        info.subresourceRange.levelCount = mipCount;
        info.subresourceRange.baseArrayLayer = baseArrayLayer;
        info.subresourceRange.layerCount = layerCount;

        if (vkCreateImageView(dev->m_apiData.m_device, &info, nullptr, &m_apiData.m_imageView) != VK_SUCCESS) {
            Log::Fatal(L"Failed to create a image view (SRV)");
            return false;
        }
        m_apiData.m_device = dev->m_apiData.m_device;

        m_isNullView = false;

        return true;
    }

    bool ShaderResourceView::InitNullView(Device* /*dev*/, Resource::Type type, Resource::Format fmt, bool isArray)
    {
        m_isNullView = true;
        m_nullViewType = type;
        m_nullIsArray = isArray;
        m_nullIsTypedBuffer = (type == Resource::Type::Buffer && fmt == Resource::Format::Unknown);

        return true;
    };

    bool ShaderResourceView::Init(Device* dev, Texture* tex, uint32_t mostDetailedMip, uint32_t mipCount, uint32_t firstArraySlice, uint32_t arraySize)
    {
        VkImageViewCreateInfo info = {};

        auto getViewType = [](Resource::Type type, bool isArray) -> VkImageViewType {
            switch (type)
            {
            case Resource::Type::Texture1D:
                return isArray ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
            case Resource::Type::Texture2D:
            case Resource::Type::Texture2DMultisample:
                return isArray ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
            case Resource::Type::Texture3D:
                if (isArray)
                    break;
                return VK_IMAGE_VIEW_TYPE_3D;
            case Resource::Type::TextureCube:
                return isArray ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
            default:
                break;
            }

            Log::Fatal(L"Unsupported resource type for a shader resource view.");
            return VkImageViewType(-1);
        };

        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = tex->m_apiData.m_image;
        info.viewType = getViewType(tex->m_type, tex->m_arraySize > 1);
        info.format = Resource::GetVkFormat(tex->m_format);
        info.subresourceRange.aspectMask = Resource::GetVkImageAspectFlags(tex->m_format, true);
        info.subresourceRange.baseMipLevel = mostDetailedMip;
        info.subresourceRange.levelCount = mipCount;
        if (tex->m_type == Resource::Type::TextureCube) {
            firstArraySlice *= 6;
            arraySize *= 6;
        }
        info.subresourceRange.baseArrayLayer = firstArraySlice;
        info.subresourceRange.layerCount = arraySize;

        if (vkCreateImageView(dev->m_apiData.m_device, &info, nullptr, &m_apiData.m_imageView) != VK_SUCCESS) {
            Log::Fatal(L"Failed to create a image view (SRV)");
            return false;
        }
        m_apiData.m_device = dev->m_apiData.m_device;

        m_isNullView = false;

        return true;
    }

    bool ShaderResourceView::Init(Device* dev, Texture* tex)
    {
        return Init(dev, tex, 0, tex->m_mipLevels, 0, tex->m_arraySize);
    }

    bool ShaderResourceView::Init(Device* dev, Buffer* buf, uint32_t firstElement, uint32_t elementCount)
    {
        uint32_t bufferElementSize = buf->m_format == Resource::Format::Unknown ? 1 : Resource::GetFormatBytesPerBlock(buf->m_format);
        m_apiData.m_rawOffsetInBytes = (uint64_t)firstElement * bufferElementSize;
        m_apiData.m_rawSizeInBytes = elementCount == 0xFFFFFFFF ? buf->m_sizeInBytes : (uint64_t)elementCount * bufferElementSize;

        //assert(m_apiData.m_rawOffsetInBytes % 16 == 0);

        if (is_set(buf->m_bindFlags, Resource::BindFlags::AccelerationStructure)) {
			// create an AS for the region.
			VkAccelerationStructureCreateInfoKHR acInfo = {};
			acInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
#if 0
			acInfo.createFlags = VK_ACCELERATION_STRUCTURE_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT_KHR;
#endif
			acInfo.buffer = buf->m_apiData.m_buffer;
			acInfo.offset = m_apiData.m_rawOffsetInBytes;
			acInfo.size = m_apiData.m_rawSizeInBytes;
			acInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR;
#if 0
			acInfo.deviceAddress = buf->m_apiData.m_deviceAddress;
#endif

			if (VK::vkCreateAccelerationStructureKHR(dev->m_apiData.m_device, &acInfo, nullptr, &m_apiData.m_accelerationStructure) != VK_SUCCESS) {
				Log::Fatal(L"Faild to create a acceleration structure.");
				return false;
			}
			m_apiData.m_isTypedBufferView = false;
			m_apiData.m_device = dev->m_apiData.m_device;
        }
        else {
            if (buf->m_format == Resource::Format::Unknown) {
                // Raw buffer don't need a view.
                m_apiData.m_isTypedBufferView = false;
                m_apiData.m_device = {};
                m_apiData.m_rawBuffer = buf->m_apiData.m_buffer;
            }
            else {
                // Create a view for TypedBuffers
                VkBufferViewCreateInfo cInfo = {};
                cInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
                cInfo.buffer = buf->m_apiData.m_buffer;
                cInfo.offset = m_apiData.m_rawOffsetInBytes;
                cInfo.range = m_apiData.m_rawSizeInBytes;
                cInfo.format = Resource::GetVkFormat(buf->m_format);

                if (vkCreateBufferView(dev->m_apiData.m_device, &cInfo, nullptr, &m_apiData.m_typedBufferView) != VK_SUCCESS) {
                    Log::Fatal(L"Failed to create a typed buffer view");
                    return false;
                }
                m_apiData.m_isTypedBufferView = true;
                m_apiData.m_device = dev->m_apiData.m_device;
            }
        }

        m_isNullView = false;

        return true;
    }

    bool ShaderResourceView::Init(Device *dev, Buffer* buf)
    {
        return Init(dev, buf, 0, 0xFFFFFFFF);
    }
#endif

    /***************************************************************
     * Abstraction for unordered access view
     ***************************************************************/
#if defined(GRAPHICS_API_D3D12)
    UnorderedAccessView::~UnorderedAccessView()
    {
    };

    void UnorderedAccessView::InitFromApiData(ID3D12Resource *resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc)
    {
        m_apiData.m_resource = resource;
        m_apiData.m_desc = *desc;

        m_isNullView = false;
    }

    bool UnorderedAccessView::InitNullView(Resource::Type type, bool isArray)
    {
        m_apiData = {};
        m_isNullView = true;
        m_nullViewType = type;
        m_nullIsArray = isArray;

        auto GetUAVResourceDimension = [&]() -> D3D12_UAV_DIMENSION
        {
            using Type = Resource::Type;

            switch (type)
            {
            case Type::Buffer:
                return D3D12_UAV_DIMENSION_BUFFER;
                break;

            case Type::Texture1D:
                if (! isArray)
                    return D3D12_UAV_DIMENSION_TEXTURE1D;
                else
                    return D3D12_UAV_DIMENSION_TEXTURE1DARRAY;

            case Type::Texture2D:
                if (! isArray)
                    return D3D12_UAV_DIMENSION_TEXTURE2D;
                else
                    return D3D12_UAV_DIMENSION_TEXTURE2DARRAY;

            case Type::TextureCube:
                return D3D12_UAV_DIMENSION_TEXTURE2DARRAY;

            case Type::Texture3D:
                return D3D12_UAV_DIMENSION_TEXTURE3D;

            default:
                break;
            }

            Log::Fatal(L"Invalid UAV dimension detected.");
            return D3D12_UAV_DIMENSION(-1);
        };

        m_apiData.m_desc = { DXGI_FORMAT_R8G8B8A8_UNORM, GetUAVResourceDimension() };

        return true;
    }

    bool UnorderedAccessView::Init(Device* /*dev*/, Texture *tex, uint32_t mipLevel, uint32_t firstArraySlice, uint32_t arraySize)
    {
        auto GetUAVResourceDimension = [&]() -> D3D12_UAV_DIMENSION
        {
            using Type = Resource::Type;

            switch (tex->m_type)
            {
            case Type::Buffer:
                return D3D12_UAV_DIMENSION_BUFFER;
                break;

            case Type::Texture1D:
                if (tex->m_arraySize == 1)
                    return D3D12_UAV_DIMENSION_TEXTURE1D;
                else
                    return D3D12_UAV_DIMENSION_TEXTURE1DARRAY;

            case Type::Texture2D:
                if (tex->m_arraySize == 1)
                    return D3D12_UAV_DIMENSION_TEXTURE2D;
                else
                    return D3D12_UAV_DIMENSION_TEXTURE2DARRAY;

            case Type::TextureCube:
                return D3D12_UAV_DIMENSION_TEXTURE2DARRAY;

            case Type::Texture3D:
                return D3D12_UAV_DIMENSION_TEXTURE3D;

            default:
                break;
            }

            Log::Fatal(L"Invalid UAV dimension detected.");
            return D3D12_UAV_DIMENSION(-1);
        };

        uint32_t arrayMultiplier = (tex->m_type == Resource::Type::TextureCube) ? 6 : 1;

        D3D12_UNORDERED_ACCESS_VIEW_DESC desc = { Resource::GetDxgiFormat(tex->m_format), GetUAVResourceDimension()   };

        switch (desc.ViewDimension) {
        case D3D12_UAV_DIMENSION_TEXTURE1D:
            desc.Texture1D.MipSlice = mipLevel;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:
            desc.Texture1DArray.MipSlice = mipLevel;
            desc.Texture1DArray.FirstArraySlice = firstArraySlice;
            desc.Texture1DArray.ArraySize = arraySize;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE2D:
            desc.Texture2D.MipSlice = mipLevel;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
            desc.Texture2DArray.MipSlice = mipLevel;
            desc.Texture2DArray.FirstArraySlice = firstArraySlice * arrayMultiplier;
            desc.Texture2DArray.ArraySize = arraySize * arrayMultiplier;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE3D:
            desc.Texture3D.MipSlice = mipLevel;
            desc.Texture3D.FirstWSlice = 0;
            desc.Texture3D.WSize = std::max(1U, tex->m_depth >> mipLevel);
            break;
        default:
            Log::Fatal(L"Invalid UAV dimension detected.");
            return false;
        }

        m_apiData.m_desc = desc;
        m_apiData.m_resource = tex->m_apiData.m_resource;

        m_isNullView = false;

        return true;
    }

    bool UnorderedAccessView::Init(Device *dev, Texture *tex)
    {
        return Init(dev, tex, 0, 0, tex->m_arraySize);
    }

    bool UnorderedAccessView::Init(Device* /*dev*/, Buffer *buf, uint32_t firstElement, uint32_t elementCount)
    {
		D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};

		uint32_t bufferElementSize = 0;
		uint32_t bufferElementCount = 0;

        if (is_set(buf->m_bindFlags, Resource::BindFlags::AccelerationStructure)) {
            // In D3D12, there is no UAV for AS.
            bufferElementSize = buf->m_format == Resource::Format::Unknown ? 1 : Resource::GetFormatBytesPerBlock(buf->m_format);
            bufferElementCount = buf->m_elementCount;

            desc.ViewDimension = (D3D12_UAV_DIMENSION)-1;
            desc.Format = DXGI_FORMAT_UNKNOWN;

        } else 	if (buf->m_format != Resource::Format::Unknown)	{
            // typed
            bufferElementSize = Resource::GetFormatBytesPerBlock(buf->m_format);
			bufferElementCount = buf->m_elementCount;

            desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            desc.Format = Resource::GetDxgiFormat(buf->m_format);
		}
		else if (buf->m_structSizeInBytes > 0)
		{
			bufferElementSize = buf->m_structSizeInBytes;
			bufferElementCount = buf->m_elementCount;

            desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.Buffer.StructureByteStride = buf->m_structSizeInBytes;
		}
		else
		{
			bufferElementSize = sizeof(uint32_t);
			bufferElementCount = (uint32_t)(buf->m_sizeInBytes / sizeof(uint32_t));
			
            desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            desc.Format = DXGI_FORMAT_R32_TYPELESS;
			desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
		}

        if (elementCount == 0xFFFFFFFF) {
            // to the last element.
            elementCount = bufferElementCount - firstElement;
        }

        // check range.
        assert((firstElement + elementCount) <= bufferElementCount);
        assert(bufferElementSize > 0);

        if (desc.ViewDimension == D3D12_UAV_DIMENSION_BUFFER) {
            desc.Buffer.FirstElement = firstElement;
            desc.Buffer.NumElements = elementCount;
        }

        m_apiData.m_desc = desc;
        m_apiData.m_resource = buf->m_apiData.m_resource;

        m_isNullView = false;

        return true;
    }

    bool UnorderedAccessView::Init(Device *dev, Buffer *buf)
    {
        return Init(dev, buf, 0, 0xFFFFFFFF);
    }
#elif defined(GRAPHICS_API_VK)
    UnorderedAccessView::~UnorderedAccessView()
    {
        if (m_apiData.m_device && m_apiData.m_typedBufferView) {
            vkDestroyBufferView(m_apiData.m_device, m_apiData.m_typedBufferView, nullptr);
        }
        if (m_apiData.m_device && m_apiData.m_imageView) {
            vkDestroyImageView(m_apiData.m_device, m_apiData.m_imageView, nullptr);
        }
        if (m_apiData.m_device && m_apiData.m_accelerationStructure) {
            VK::vkDestroyAccelerationStructureKHR(m_apiData.m_device, m_apiData.m_accelerationStructure, nullptr);
        }
        m_apiData = {};
    }

    bool UnorderedAccessView::InitFromApiData(Device* dev, VkImage image, VkImageViewType imageType, VkFormat fmt, VkImageAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t baseArrayLayer, uint32_t layerCount)
    {
        VkImageViewCreateInfo info = {};

        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = image;
        info.viewType = imageType;
        info.format = fmt;
        info.subresourceRange.aspectMask = aspectMask;
        info.subresourceRange.baseMipLevel = baseMipLevel;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.baseArrayLayer = baseArrayLayer;
        info.subresourceRange.layerCount = layerCount;

        if (vkCreateImageView(dev->m_apiData.m_device, &info, nullptr, &m_apiData.m_imageView) != VK_SUCCESS) {
            Log::Fatal(L"Failed to create a image view (UAV)");
            return false;
        }
        m_apiData.m_device = dev->m_apiData.m_device;

        m_isNullView = false;

        return true;
    }

    bool UnorderedAccessView::InitNullView(Device* /*dev*/, Resource::Type type, Resource::Format fmt, bool isArray)
    {
        m_isNullView = true;
        m_nullViewType = type;
        m_nullIsArray = isArray;
        m_nullIsTypedBuffer = (type == Resource::Type::Buffer && fmt == Resource::Format::Unknown);

        return true;
    }

    bool UnorderedAccessView::Init(Device* dev, Texture* tex, uint32_t mipLevel, uint32_t firstArraySlice, uint32_t arraySize)
    {
        VkImageViewCreateInfo info = {};

        auto getViewType = [](Resource::Type type, bool isArray) -> VkImageViewType {
            switch (type)
            {
            case Resource::Type::Texture1D:
                return isArray ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
            case Resource::Type::Texture2D:
            case Resource::Type::Texture2DMultisample:
                return isArray ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
            case Resource::Type::Texture3D:
                if (isArray)
                    break;
                return VK_IMAGE_VIEW_TYPE_3D;
            case Resource::Type::TextureCube:
                return isArray ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
            default:
                break;
            }

            Log::Fatal(L"Unsupported resource type for a shader resource view.");
            return VkImageViewType(-1);
        };

        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = tex->m_apiData.m_image;
        info.viewType = getViewType(tex->m_type, tex->m_arraySize > 1);
        info.format = Resource::GetVkFormat(tex->m_format);
        info.subresourceRange.aspectMask = Resource::GetVkImageAspectFlags(tex->m_format, true);
        info.subresourceRange.baseMipLevel = mipLevel;
        info.subresourceRange.levelCount = 1;;
        if (tex->m_type == Resource::Type::TextureCube) {
            firstArraySlice *= 6;
            arraySize *= 6;
        }
        info.subresourceRange.baseArrayLayer = firstArraySlice;
        info.subresourceRange.layerCount = arraySize;

        if (vkCreateImageView(dev->m_apiData.m_device, &info, nullptr, &m_apiData.m_imageView) != VK_SUCCESS) {
            Log::Fatal(L"Failed to create a image view (UAV)");
            return false;
        }
        m_apiData.m_device = dev->m_apiData.m_device;

        m_isNullView = false;

        return true;
    }

    bool UnorderedAccessView::Init(Device* dev, Texture* tex)
    {
        return Init(dev, tex, 0, 0, tex->m_arraySize);
    }

    bool UnorderedAccessView::Init(Device* dev, Buffer* buf, uint32_t firstElement, uint32_t elementCount)
    {
#if 0
        if (buf->GetGlobalState() == ResourceState::State::AccelerationStructure) {
            Log::Fatal(L"AccelerationStructure detected. nee to check SDK source code to support it.");
            return false;
        }
#endif

        uint32_t bufferElementSize = buf->m_format == Resource::Format::Unknown ? 1 : Resource::GetFormatBytesPerBlock(buf->m_format);

        m_apiData.m_rawOffsetInBytes = (uint64_t)firstElement * bufferElementSize;
        m_apiData.m_rawSizeInBytes = elementCount == 0xFFFFFFFF ? buf->m_sizeInBytes : (uint64_t)elementCount * bufferElementSize;

        //assert(m_apiData.m_rawOffsetInBytes % 16 == 0);

        if (is_set(buf->m_bindFlags, Resource::BindFlags::AccelerationStructure)) {
            // create an AS for the region.
            VkAccelerationStructureCreateInfoKHR acInfo = {};
            acInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
#if 0
            acInfo.createFlags = VK_ACCELERATION_STRUCTURE_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT_KHR;
#endif
            acInfo.buffer = buf->m_apiData.m_buffer;
            acInfo.offset = m_apiData.m_rawOffsetInBytes;
            acInfo.size = m_apiData.m_rawSizeInBytes;
            acInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR;
#if 0
            acInfo.deviceAddress = buf->m_apiData.m_deviceAddress;
#endif

            if (VK::vkCreateAccelerationStructureKHR(dev->m_apiData.m_device, &acInfo, nullptr, &m_apiData.m_accelerationStructure) != VK_SUCCESS) {
                Log::Fatal(L"Faild to create a acceleration structure.");
                return false;
            }
            m_apiData.m_isTypedBufferView = false;
            m_apiData.m_device = dev->m_apiData.m_device;
        }
        else {
            if (buf->m_format == Resource::Format::Unknown) {
                // raw buffer don't need a view.

                m_apiData.m_isTypedBufferView = false;
                m_apiData.m_device = {};
                m_apiData.m_rawBuffer = buf->m_apiData.m_buffer;
            }
            else {
                // Create views for TypedBuffers
                VkBufferViewCreateInfo cInfo = {};
                cInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
                cInfo.buffer = buf->m_apiData.m_buffer;
                cInfo.offset = m_apiData.m_rawOffsetInBytes;
                cInfo.range = m_apiData.m_rawSizeInBytes;
                cInfo.format = Resource::GetVkFormat(buf->m_format);

                if (vkCreateBufferView(dev->m_apiData.m_device, &cInfo, nullptr, &m_apiData.m_typedBufferView) != VK_SUCCESS) {
                    Log::Fatal(L"Failed to create a typed buffer view");
                    return false;
                }
                m_apiData.m_isTypedBufferView = true;
                m_apiData.m_device = dev->m_apiData.m_device;
            }
        }

        m_isNullView = false;

        return true;
    }

    bool UnorderedAccessView::Init(Device* dev, Buffer* buf)
    {
        return Init(dev, buf, 0, 0xFFFFFFFF);
    }
#endif

    /***************************************************************
     * Abstraction for constant buffer view
     ***************************************************************/
#if defined(GRAPHICS_API_D3D12)
    bool ConstantBufferView::Init(Buffer* buf, uint64_t offsetInBytes, uint32_t sizeInBytes)
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
        desc.BufferLocation = buf->m_apiData.m_resource->GetGPUVirtualAddress();

        if (ALIGN((uint64_t)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, offsetInBytes) != offsetInBytes ||
            ALIGN((uint32_t)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, sizeInBytes) != sizeInBytes) {
            Log::Fatal(L"Faild to init CBV. Alignment violation detected.");
            return false;
        }
        if (offsetInBytes + (uint64_t)sizeInBytes > buf->m_sizeInBytes) {
            Log::Fatal(L"Faild to init CBV. CBV range exceeded the buffer range.");
            return false;
        }

        desc.BufferLocation += offsetInBytes;
        desc.SizeInBytes = sizeInBytes;

        m_apiData.m_desc = desc;
        m_apiData.m_resource = buf->m_apiData.m_resource;

        return true;
    }

    bool ConstantBufferView::Init(Buffer* buf)
    {
        return Init(buf, 0, (uint32_t)buf->m_sizeInBytes);
    }
#elif defined(GRAPHICS_API_VK)
    bool ConstantBufferView::Init(Buffer* buf, uint64_t offsetInBytes, uint32_t sizeInBytes)
    {
        if (Resource::ConstantBufferPlacementAlignment(offsetInBytes) != offsetInBytes ||
            Resource::ConstantBufferPlacementAlignment(sizeInBytes) != sizeInBytes) {
            Log::Fatal(L"Faild to init CBV. Alignment violation detected.");
            return false;
        }
        if (offsetInBytes + (uint64_t)sizeInBytes > buf->m_sizeInBytes) {
            Log::Fatal(L"Faild to init CBV. CBV range exceeded the buffer range.");
            return false;
        }

        m_apiData.m_buffer = buf->m_apiData.m_buffer;
        m_apiData.m_offsetInBytes = offsetInBytes;
        m_apiData.m_sizeInBytes = sizeInBytes;

        return true;
    }

    bool ConstantBufferView::Init(Buffer* buf)
    {
        return Init(buf, 0, (uint32_t)buf->m_sizeInBytes);
    }
#endif


    /***************************************************************
     * CommandList in D3D12
     * CommandBuffer in VK
     ***************************************************************/
#if defined(GRAPHICS_API_D3D12)
    CommandList::~CommandList()
    {
        m_apiData = {};
    }

    void CommandList::SetName(const std::wstring& str)
    {
        SetNameInternal(m_apiData.m_commandList, str.c_str());
    }

    void CommandList::ClearState()
    {
        m_apiData.m_commandList->ClearState(nullptr);
    }

    bool CommandList::InitFromAPIData(ID3D12GraphicsCommandList4* cmdList, ID3D12DebugCommandList1* dbgCmdList)
    {
        if (cmdList == nullptr)
            return false;

        m_apiData.m_commandList = cmdList;
        m_apiData.m_debugCommandList = dbgCmdList;

        return true;
    }

    bool CommandList::SetDescriptorHeap(DescriptorHeap* heap)
    {
        std::vector<ID3D12DescriptorHeap*> descs;
        
        for (auto& h : heap->m_apiData.m_heaps) {
            if (h.m_descHeap != nullptr) {
                descs.push_back(h.m_descHeap);
            }
        }
        m_apiData.m_commandList->SetDescriptorHeaps((uint32_t)descs.size(), descs.data());

        return true;
    }

    bool CommandList::HasDebugCommandList() const
    {
        return m_apiData.m_debugCommandList != nullptr;
    }

    bool CommandList::AssertResourceStates(Resource** resArr, SubresourceRange* subresourceArr, size_t numRes, ResourceState::State* statesToAssert) {
        if (m_apiData.m_debugCommandList) {
            for (size_t i = 0; i < numRes; ++i) {
                Resource* r = resArr[i];
                if (subresourceArr || r->m_globalState.IsTrackingPerSubresource()) {
                    assert(Resource::IsTexture(r->m_type));
                    Texture* t = (Texture*)r;

                    const SubresourceRange& range = subresourceArr ? subresourceArr[i] : SubresourceRange(0, (uint8_t)t->m_arraySize, 0, (uint8_t)t->m_mipLevels);
                    for (uint8_t arraySlice = range.baseArrayLayer; arraySlice < range.baseArrayLayer + range.arrayLayerCount; ++arraySlice) {
                        for (uint8_t mipLevel = range.baseMipLevel; mipLevel < range.baseMipLevel + range.mipLevelCount; ++mipLevel) {
                            uint32_t subresurceIdx = SubresourceRange::CalcSubresource(mipLevel, arraySlice, t->m_mipLevels);
                            m_apiData.m_debugCommandList->AssertResourceState(r->m_apiData.m_resource, subresurceIdx, ResourceState::GetD3D12ResourceState(statesToAssert[i]));
                        }
                    }
                }
            }
        }
        return true;
    }

#if defined(GRAPHICS_API_D3D12)
    // Handy shortcut for D3D12 buffers
    bool CommandList::AssertResourceStates(ID3D12Resource** resArr, size_t numRes, const D3D12_RESOURCE_STATES* statesToAssert)
    {
        if (m_apiData.m_debugCommandList) {
            for (size_t i = 0; i < numRes; ++i) {
				m_apiData.m_debugCommandList->AssertResourceState(resArr[i], D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, statesToAssert[i]);
            }
        }
        return true;
    }
#endif

    bool CommandList::ResourceTransitionBarrier(Resource** resArr, SubresourceRange* subresourceArr, size_t numRes, ResourceState::State* desiredStates)
    {
        std::vector<D3D12_RESOURCE_BARRIER> bArr;

        D3D12_RESOURCE_BARRIER bWrk = {
            D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            D3D12_RESOURCE_BARRIER_FLAG_NONE,
        };

        for (size_t i = 0; i < numRes; ++i) {
            Resource* r = resArr[i];

            if (subresourceArr || r->m_globalState.IsTrackingPerSubresource()) {
                assert(Resource::IsTexture(r->m_type));
                Texture* t = (Texture*)r;

                const SubresourceRange& range = subresourceArr ? subresourceArr[i] : SubresourceRange(0, (uint8_t)t->m_arraySize, 0, (uint8_t)t->m_mipLevels);
                for (uint8_t arraySlice = range.baseArrayLayer; arraySlice < range.baseArrayLayer + range.arrayLayerCount; ++arraySlice) {
                    for (uint8_t mipLevel = range.baseMipLevel; mipLevel < range.baseMipLevel + range.mipLevelCount; ++mipLevel) {
                        uint32_t subresurceIdx = SubresourceRange::CalcSubresource(mipLevel, arraySlice, t->m_mipLevels);
                       
                        if (r->GetGlobalState(subresurceIdx) != desiredStates[i]) {
                            bWrk.Transition.pResource = r->m_apiData.m_resource;
                            bWrk.Transition.Subresource = subresurceIdx;
                            bWrk.Transition.StateBefore = ResourceState::GetD3D12ResourceState(r->GetGlobalState(subresurceIdx));
                            bWrk.Transition.StateAfter = ResourceState::GetD3D12ResourceState(desiredStates[i]);

                            // This happens when transitioning between ShaderResource and NonPixelShader
                            if (bWrk.Transition.StateBefore != bWrk.Transition.StateAfter)
                                bArr.push_back(bWrk);

                            r->SetGlobalState(desiredStates[i], subresurceIdx);
                        }
                    }
                }
            }
            else {

                if (r->GetGlobalState() != desiredStates[i]) {
                    bWrk.Transition.pResource = r->m_apiData.m_resource;
                    bWrk.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    bWrk.Transition.StateBefore = ResourceState::GetD3D12ResourceState(r->GetGlobalState());
                    bWrk.Transition.StateAfter = ResourceState::GetD3D12ResourceState(desiredStates[i]);

                    // This happens when transitioning between ShaderResource and NonPixelShader
                    if (bWrk.Transition.StateBefore != bWrk.Transition.StateAfter)
                        bArr.push_back(bWrk);

                    r->SetGlobalState(desiredStates[i]);
                }
            }
        }

        if (bArr.size() > 0) {

            D3D12_RESOURCE_BARRIER* data = bArr.data();
            uint32_t size = (uint32_t)bArr.size();

            m_apiData.m_commandList->ResourceBarrier(size, data);
        }

        return true;
    }

    bool CommandList::ResourceTransitionBarrier(Resource** resArr, size_t numRes, ResourceState::State* desiredStates)
    {
        return ResourceTransitionBarrier(resArr, nullptr, numRes, desiredStates);
    }

    bool CommandList::ResourceUAVBarrier(Resource** resArr, size_t numRes)
    {
        std::vector<D3D12_RESOURCE_BARRIER> bArr;

        D3D12_RESOURCE_BARRIER bWrk = {
            D3D12_RESOURCE_BARRIER_TYPE_UAV,
            D3D12_RESOURCE_BARRIER_FLAG_NONE,
        };

        for (size_t i = 0; i < numRes; ++i) {
            Resource* r = resArr[i];

            bWrk.UAV.pResource = r->m_apiData.m_resource;
            bArr.push_back(bWrk);
        }

        if (bArr.size() > 0) {
            m_apiData.m_commandList->ResourceBarrier((uint32_t)bArr.size(), &bArr[0]);
        }

        return true;
    }

    bool CommandList::CopyTextureSingleMip(Device* dev, uint32_t mipIndex, Texture* dstTex, Buffer* srcUpBuf)
    {
        D3D12_RESOURCE_DESC desc = {};

        desc.MipLevels = (UINT16)dstTex->m_mipLevels;
        desc.Format = Resource::GetDxgiFormat(dstTex->m_format);
        desc.Width = dstTex->m_width;
        desc.Height = dstTex->m_height;
        desc.Flags = Resource::GetD3D12ResourceFlags(dstTex->m_bindFlags);
        desc.SampleDesc.Count = dstTex->m_sampleCount;
        desc.SampleDesc.Quality = 0;
        desc.Dimension = Resource::GetResourceDimension(dstTex->m_type);
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Alignment = 0;

        if (dstTex->m_type == Resource::Type::TextureCube)
        {
            desc.DepthOrArraySize = (UINT16)(dstTex->m_arraySize * 6);
        }
        else if (dstTex->m_type == Resource::Type::Texture3D)
        {
            desc.DepthOrArraySize = (UINT16)dstTex->m_depth;
        }
        else
        {
            desc.DepthOrArraySize = (UINT16)dstTex->m_arraySize;
        }
        assert(desc.Width > 0 && desc.Height > 0);
        assert(desc.MipLevels > 0 && desc.DepthOrArraySize > 0 && desc.SampleDesc.Count > 0);

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT uploadBufferFootprint;
        UINT numRows;
        UINT64 rowSizeInBytes, totalBytes;
        dev->m_apiData.m_device->GetCopyableFootprints(&desc, mipIndex, 1, 0, &uploadBufferFootprint, &numRows, &rowSizeInBytes, &totalBytes);

        if (totalBytes != srcUpBuf->m_sizeInBytes) {
            Log::Fatal(L"Upload staging buffer didn't fit to the destination texture.");
            return false;
        }

		// upload buffer is always generic read state.
		Resource* resArr[1] = { dstTex };
		ResourceState::State desiredStatesBefore[1] = { ResourceState::State::CopyDest };
        ResourceState::State desiredStatesAfter[1] = { ResourceState::State::ShaderResource };
        ResourceTransitionBarrier(resArr, 1, desiredStatesBefore);

        D3D12_TEXTURE_COPY_LOCATION uploadBufLocation = {
            srcUpBuf->m_apiData.m_resource,
            D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
            uploadBufferFootprint
        };
        D3D12_TEXTURE_COPY_LOCATION defaultBufLocation = {
            dstTex->m_apiData.m_resource,
            D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
            0
        };
        m_apiData.m_commandList->CopyTextureRegion(&defaultBufLocation, 0, 0, 0, &uploadBufLocation, nullptr);

        ResourceTransitionBarrier(resArr, 1, desiredStatesAfter);

        return true;
    }

    void CommandList::CopyBufferRegion(Buffer* dst, uint64_t dstOffset, Buffer* src, uint64_t srcOffset, uint64_t copySizeInBytes)
    {
        m_apiData.m_commandList->CopyBufferRegion(dst->m_apiData.m_resource, dstOffset, src->m_apiData.m_resource, srcOffset, copySizeInBytes);
    }

    void CommandList::CopyTextureRegion(Texture* dst, Texture* src)
    {
        constexpr uint32_t kNumBuf = 2;
        GraphicsAPI::Resource* dstBufArr[kNumBuf]                   = { dst, src };
        GraphicsAPI::ResourceState::State	desiredStateArr[kNumBuf]    = { GraphicsAPI::ResourceState::State::CopyDest, GraphicsAPI::ResourceState::State::CopySource };
        ResourceTransitionBarrier(dstBufArr, kNumBuf, desiredStateArr);


        D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
        dstLoc.pResource            = dst->m_apiData.m_resource;
        dstLoc.Type                 = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex     = 0;

        D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
        srcLoc.pResource            = src->m_apiData.m_resource;
        srcLoc.Type                 = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLoc.SubresourceIndex     = 0;

        constexpr UINT DstX = 0;
        constexpr UINT DstY = 0;
        constexpr UINT DstZ = 0;
        const D3D12_BOX* pSrcBox = nullptr;

        m_apiData.m_commandList->CopyTextureRegion(&dstLoc, DstX, DstY, DstZ, &srcLoc, pSrcBox);
    }

    void CommandList::CopyResource(Texture* dst, Texture* src)
    {
        constexpr uint32_t kNumBuf = 2;
        GraphicsAPI::Resource* dstBufArr[kNumBuf] = { dst, src };
        GraphicsAPI::ResourceState::State	desiredStateArr[kNumBuf] = { GraphicsAPI::ResourceState::State::CopyDest, GraphicsAPI::ResourceState::State::CopySource };
        ResourceTransitionBarrier(dstBufArr, kNumBuf, desiredStateArr);

        m_apiData.m_commandList->CopyResource(dst->m_apiData.m_resource, src->m_apiData.m_resource);
    }

    void CommandList::SetComputeRootDescriptorTable(RootSignature* /*rootSig*/, uint32_t baseSlotIndex, DescriptorTable** tables, size_t numTables)
    {
        for (size_t i = 0; i < numTables; ++i) {
            m_apiData.m_commandList->SetComputeRootDescriptorTable(baseSlotIndex + (uint32_t)i, tables[i]->m_apiData.m_heapAllocationInfo.m_hGPU);
        }
    } 
    void CommandList::SetRayTracingRootDescriptorTable(RootSignature* rootSig, uint32_t baseSlotIndex, DescriptorTable** tables, size_t numTables)
    {
        // D3D12 uses the same binding point for RT.
        return SetComputeRootDescriptorTable(rootSig, baseSlotIndex, tables, numTables);
    }

    void CommandList::SetComputeRootSignature(RootSignature* rootSig)
    {
        m_apiData.m_commandList->SetComputeRootSignature(rootSig->m_apiData.m_rootSignature);
    }
    void CommandList::SetComputePipelineState(ComputePipelineState* pso)
    {
        m_apiData.m_commandList->SetPipelineState(pso->m_apiData.m_pipelineState);
    }
    void CommandList::SetRayTracingPipelineState(RaytracingPipelineState* rtPSO)
    {
        m_apiData.m_commandList->SetPipelineState1(rtPSO->m_apiData.m_rtPSO);
    }

    void CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z)
    {
        m_apiData.m_commandList->Dispatch(x, y, z);
    }

    void CommandList::BeginEvent(const std::array<uint32_t, 3>& color, const std::string& str)
    {
#if defined(USE_PIX)
        PIXBeginEvent(m_apiData.m_commandList, PIX_COLOR((BYTE)color[0], (BYTE)color[1], (BYTE)color[2]), "%s", str.c_str());
#endif
    }

    void CommandList::EndEvent()
    {
#if defined(USE_PIX)
        PIXEndEvent(m_apiData.m_commandList);
#endif
    }
    
#elif defined(GRAPHICS_API_VK)
    CommandList::~CommandList()
    {
        m_apiData = {};
    }

    void CommandList::SetName(const std::wstring& str)
    {
        SetNameInternal(m_apiData.m_device, VkObjectType::VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)m_apiData.m_commandBuffer, str.c_str());
    }

    bool CommandList::InitFromAPIData(VkDevice device, VkCommandBuffer cmdBuf)
    {
        m_apiData.m_device = device;
        m_apiData.m_commandBuffer = cmdBuf;

        return true;
    }

    void CommandList::ClearState()
    {
        // There is no VK clear state.
        assert(false);
    }

    bool CommandList::SetDescriptorHeap(DescriptorHeap* /*heap*/)
    {
        // nothing to do.
        return true;
    }

    bool CommandList::HasDebugCommandList() const
    {
        return false;
    }

    bool CommandList::AssertResourceStates(Resource** , SubresourceRange* , size_t , ResourceState::State* )
    {
        return true;
    }

    bool CommandList::ResourceTransitionBarrier(Resource** resArr, SubresourceRange* subresourceRanges, size_t numRes, ResourceState::State* desiredStates)
    {
        {
            std::vector<VkBufferMemoryBarrier>     bufSHADER2SHADERBarrier;
            std::vector<VkBufferMemoryBarrier>     bufSHADER2CPYBarrier;
            std::vector<VkBufferMemoryBarrier>     bufTop2CPYBarrier;
            std::vector<VkBufferMemoryBarrier>     bufCPY2SHADERBarrier;
            std::vector<VkBufferMemoryBarrier>     bufCPY2CPYBarrier;
            std::vector<VkBufferMemoryBarrier>     bufCPY2HOSTBarrier;
            std::vector<VkImageMemoryBarrier>      imgSHADER2SHADERBBarrier;
            std::vector<VkImageMemoryBarrier>      imgSHADER2CPYBarrier;
            std::vector<VkImageMemoryBarrier>      imgCPY2SHADERBarrier;

            for (size_t i = 0; i < numRes; i++) {
                auto res = resArr[i];
                auto newState = desiredStates[i];

                if (res->m_type == Resource::Type::Buffer) {

                    assert(subresourceRanges == nullptr && "Expecting no subresource ranges for buffers");

                    Buffer* buf = (Buffer*)res;
                    VkBufferMemoryBarrier barrier = {};
                    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                    barrier.srcAccessMask = Resource::GetVkAccessMask(buf->GetGlobalState());
                    barrier.dstAccessMask = Resource::GetVkAccessMask(newState);
                    barrier.buffer = buf->m_apiData.m_buffer;
                    barrier.offset = 0;
                    barrier.size = buf->m_sizeInBytes;

                    if (buf->GetGlobalState() == newState) {
                        continue;
                    }
                    else if (buf->GetGlobalState() == ResourceState::State::UnorderedAccess && newState == ResourceState::State::ShaderResource) {
                        bufSHADER2SHADERBarrier.push_back(barrier);
                    }
                    else if (buf->GetGlobalState() == ResourceState::State::ShaderResource && newState == ResourceState::State::UnorderedAccess) {
                        bufSHADER2SHADERBarrier.push_back(barrier);
                    }
                    else if (buf->GetGlobalState() == ResourceState::State::UnorderedAccess && newState == ResourceState::State::CopySource) {
                        bufSHADER2CPYBarrier.push_back(barrier);
                    }
                    else if (buf->GetGlobalState() == ResourceState::State::UnorderedAccess && newState == ResourceState::State::CopyDest) {
                        bufSHADER2CPYBarrier.push_back(barrier);
                    }
                    else if (buf->GetGlobalState() == ResourceState::State::GenericRead && newState == ResourceState::State::CopySource) {
                        bufTop2CPYBarrier.push_back(barrier);
                    }
                    else if (buf->GetGlobalState() == ResourceState::State::CopyDest && newState == ResourceState::State::UnorderedAccess) {
                        bufCPY2SHADERBarrier.push_back(barrier);
                    }
                    else if (buf->GetGlobalState() == ResourceState::State::CopyDest && newState == ResourceState::State::NonPixelShader) {
                        bufCPY2SHADERBarrier.push_back(barrier);
                    }
                    else if (buf->GetGlobalState() == ResourceState::State::CopyDest && newState == ResourceState::State::CopySource) {
                        bufCPY2CPYBarrier.push_back(barrier);
                    }
                    else if (buf->GetGlobalState() == ResourceState::State::CopyDest && newState == ResourceState::State::CopyDest) {
                        // this is a hacky case that D3D12 doesn't need barrier copy after host read, but VK need a barrier.
                        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                        bufCPY2HOSTBarrier.push_back(barrier);
                    }
                    else if (buf->GetGlobalState() == ResourceState::State::CopySource && newState == ResourceState::State::UnorderedAccess) {
                        bufCPY2SHADERBarrier.push_back(barrier);
                    }
                    else {
                        Log::Fatal(L"Unsupported resource transition type in VK detected.");
                        return false;
                    }
                    buf->SetGlobalState(newState);
                }
                else {

                    auto QueueVkBarrier = [&imgSHADER2SHADERBBarrier,&imgSHADER2CPYBarrier,&imgCPY2SHADERBarrier]
                    (const VkImageMemoryBarrier& barrier, ResourceState::State from, ResourceState::State to){
                        if (from == ResourceState::State::UnorderedAccess && to == ResourceState::State::ShaderResource) {
                            imgSHADER2SHADERBBarrier.push_back(barrier);
                        }
                        else if (from == ResourceState::State::UnorderedAccess && to == ResourceState::State::NonPixelShader) {
                            imgSHADER2SHADERBBarrier.push_back(barrier);
                        }
                        else if (from == ResourceState::State::UnorderedAccess && to == ResourceState::State::CopySource) {
                            imgSHADER2CPYBarrier.push_back(barrier);
                        }
                        else if (from == ResourceState::State::UnorderedAccess && to == ResourceState::State::CopyDest) {
                            imgSHADER2CPYBarrier.push_back(barrier);
                        }
                        else if (from == ResourceState::State::ShaderResource && to == ResourceState::State::UnorderedAccess) {
                            imgSHADER2SHADERBBarrier.push_back(barrier);
                        }
                        else if (from == ResourceState::State::ShaderResource && to == ResourceState::State::CopyDest) {
                            imgSHADER2CPYBarrier.push_back(barrier);
                        }
                        else if (from == ResourceState::State::ShaderResource && to == ResourceState::State::Common) {
                            imgSHADER2SHADERBBarrier.push_back(barrier);
                        }
                        else if (from == ResourceState::State::NonPixelShader && to == ResourceState::State::UnorderedAccess) {
                            imgSHADER2SHADERBBarrier.push_back(barrier);
                        }
                        else if (from == ResourceState::State::NonPixelShader && to == ResourceState::State::Common) {
                            imgSHADER2SHADERBBarrier.push_back(barrier);
                        }
                        else if (from == ResourceState::State::NonPixelShader && to == ResourceState::State::CopyDest) {
                            imgSHADER2CPYBarrier.push_back(barrier);
                        }
                        else if (from == ResourceState::State::CopyDest && to == ResourceState::State::NonPixelShader) {
                            imgCPY2SHADERBarrier.push_back(barrier);
                        }
                        else if (from == ResourceState::State::CopyDest && to == ResourceState::State::Common) {
                            imgCPY2SHADERBarrier.push_back(barrier);
                        }
                        else if (from == ResourceState::State::CopyDest && to == ResourceState::State::UnorderedAccess) {
                            imgCPY2SHADERBarrier.push_back(barrier);
                        }
                        else if (from == ResourceState::State::CopySource && to == ResourceState::State::Common) {
                            imgCPY2SHADERBarrier.push_back(barrier);
                        }
                        else if (from == ResourceState::State::Undefined && to == ResourceState::State::UnorderedAccess) {
                            imgSHADER2SHADERBBarrier.push_back(barrier);
                        }
                        else if (from == ResourceState::State::Undefined && to == ResourceState::State::NonPixelShader) {
                            imgSHADER2SHADERBBarrier.push_back(barrier);
                        }
                        else if (from == ResourceState::State::Undefined && to == ResourceState::State::Common) {
                            imgSHADER2SHADERBBarrier.push_back(barrier);
                        }
                        else if (from == ResourceState::State::Common && to == ResourceState::State::NonPixelShader) {
                            imgSHADER2SHADERBBarrier.push_back(barrier);
                        }
                        else if (from == ResourceState::State::Common && to == ResourceState::State::UnorderedAccess) {
                            imgSHADER2SHADERBBarrier.push_back(barrier);
                        }
                        else if (from == ResourceState::State::Common && to == ResourceState::State::CopyDest) {
                            imgSHADER2CPYBarrier.push_back(barrier);
                        }
                        else if (from == ResourceState::State::Common && to == ResourceState::State::ShaderResource) {
                            imgSHADER2SHADERBBarrier.push_back(barrier);
                        }
                        else {
                            Log::Fatal(L"Unsupported resource transition type in VK detected.");
                        }
                    };

                    // texture (image)
                    Texture* tex = (Texture*)res;
                    if (subresourceRanges || tex->m_globalState.IsTrackingPerSubresource()) {

                        const SubresourceRange& range = subresourceRanges ? subresourceRanges[i] : SubresourceRange(0, (uint8_t)tex->m_arraySize, 0, (uint8_t)tex->m_mipLevels);

                        for (uint8_t arrayIdx = range.baseArrayLayer; arrayIdx < range.baseArrayLayer + range.arrayLayerCount; ++arrayIdx) {
                            for (uint8_t mipIdx = range.baseMipLevel; mipIdx < range.baseMipLevel + range.mipLevelCount; ++mipIdx) {
                                uint32_t subresource = SubresourceRange::CalcSubresource(mipIdx, arrayIdx, tex->m_mipLevels);

                                ResourceState::State oldState = tex->GetGlobalState(subresource);

                                VkImageLayout srcLayout = Resource::GetVkImageLayout(oldState);
                                VkImageLayout dstLayout = Resource::GetVkImageLayout(newState);

                                if (srcLayout != dstLayout)
                                {
                                    VkImageMemoryBarrier barrier = {};
                                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                                    barrier.oldLayout = srcLayout;
                                    barrier.newLayout = dstLayout;
                                    barrier.image = tex->m_apiData.m_image;
                                    barrier.subresourceRange.aspectMask = Resource::GetVkImageAspectFlags(tex->m_format);
                                    barrier.subresourceRange.baseArrayLayer = arrayIdx;
                                    barrier.subresourceRange.baseMipLevel = mipIdx;
                                    barrier.subresourceRange.layerCount = 1;
                                    barrier.subresourceRange.levelCount = 1;
                                    barrier.srcAccessMask = Resource::GetVkAccessMask(oldState);
                                    barrier.dstAccessMask = Resource::GetVkAccessMask(newState);

                                    QueueVkBarrier(barrier, oldState, newState);
                                }

                                tex->SetGlobalState(newState, subresource);
                            }
                        }
                    }
                    else {
                        ResourceState::State oldState = tex->GetGlobalState();

                        VkImageLayout srcLayout = Resource::GetVkImageLayout(oldState);
                        VkImageLayout dstLayout = Resource::GetVkImageLayout(newState);

                        if (srcLayout != dstLayout)
                        {
                            VkImageMemoryBarrier barrier = {};
                            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                            barrier.oldLayout = srcLayout;
                            barrier.newLayout = dstLayout;
                            barrier.image = tex->m_apiData.m_image;
                            barrier.subresourceRange.aspectMask = Resource::GetVkImageAspectFlags(tex->m_format);
                            barrier.subresourceRange.baseArrayLayer = 0;
                            barrier.subresourceRange.baseMipLevel = 0;
                            barrier.subresourceRange.layerCount = tex->m_arraySize;
                            barrier.subresourceRange.levelCount = tex->m_mipLevels;;
                            barrier.srcAccessMask = Resource::GetVkAccessMask(oldState);
                            barrier.dstAccessMask = Resource::GetVkAccessMask(newState);

                            QueueVkBarrier(barrier, oldState, newState);
                        }

                        tex->SetGlobalState(newState);
                    }
                }
            }

            if (bufSHADER2SHADERBarrier.size() > 0) {
                vkCmdPipelineBarrier(m_apiData.m_commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                    0, nullptr,
                    (uint32_t)bufSHADER2SHADERBarrier.size(), bufSHADER2SHADERBarrier.data(),
                    0, nullptr);
            }
            if (bufSHADER2CPYBarrier.size() > 0) {
                vkCmdPipelineBarrier(m_apiData.m_commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                    0, nullptr,
                    (uint32_t)bufSHADER2CPYBarrier.size(), bufSHADER2CPYBarrier.data(),
                    0, nullptr);
            }
            if (bufTop2CPYBarrier.size() > 0) {
                vkCmdPipelineBarrier(m_apiData.m_commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                    0, nullptr,
                    (uint32_t)bufTop2CPYBarrier.size(), bufTop2CPYBarrier.data(),
                    0, nullptr);
            }
            if (bufCPY2SHADERBarrier.size() > 0) {
                vkCmdPipelineBarrier(m_apiData.m_commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                    0, nullptr,
                    (uint32_t)bufCPY2SHADERBarrier.size(), bufCPY2SHADERBarrier.data(),
                    0, nullptr);
            }
            if (bufCPY2CPYBarrier.size() > 0) {
                vkCmdPipelineBarrier(m_apiData.m_commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                    0, nullptr,
                    (uint32_t)bufCPY2CPYBarrier.size(), bufCPY2CPYBarrier.data(),
                    0, nullptr);
            }
            if (bufCPY2HOSTBarrier.size() > 0) {
                vkCmdPipelineBarrier(m_apiData.m_commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                    0, nullptr,
                    (uint32_t)bufCPY2HOSTBarrier.size(), bufCPY2HOSTBarrier.data(),
                    0, nullptr);
            }
            
            if (imgSHADER2SHADERBBarrier.size() > 0) {
                vkCmdPipelineBarrier(m_apiData.m_commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                    0, nullptr,
                    0, nullptr,
                    (uint32_t)imgSHADER2SHADERBBarrier.size(), imgSHADER2SHADERBBarrier.data());
            }
            if (imgSHADER2CPYBarrier.size() > 0) {
                vkCmdPipelineBarrier(m_apiData.m_commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                    0, nullptr,
                    0, nullptr,
                    (uint32_t)imgSHADER2CPYBarrier.size(), imgSHADER2CPYBarrier.data());
            }
            if (imgCPY2SHADERBarrier.size() > 0) {
                vkCmdPipelineBarrier(m_apiData.m_commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                    0, nullptr,
                    0, nullptr,
                    (uint32_t)imgCPY2SHADERBarrier.size(), imgCPY2SHADERBarrier.data());
            }
        }

        return true;
    }

    bool CommandList::ResourceTransitionBarrier(Resource** resArr, size_t numRes, ResourceState::State* desiredStates)
    {
        return ResourceTransitionBarrier(resArr, nullptr, numRes, desiredStates);
    }

    bool CommandList::ResourceUAVBarrier(Resource** resArr, size_t numRes)
    {
        // There is no layout change in UAV barrier.
        std::vector<VkBufferMemoryBarrier>     bufSHADER2SHADERBarrier;
        std::vector<VkBufferMemoryBarrier>     bufAS2ASBarrier;
        std::vector<VkImageMemoryBarrier>      imgBarrier;

        for (size_t i = 0; i < numRes; i++) {
            auto res = resArr[i];

            if (res->m_type == Resource::Type::Buffer) {
                Buffer* buf = (Buffer*)res;
                VkBufferMemoryBarrier barrier = {};
                barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                barrier.buffer = buf->m_apiData.m_buffer;
                barrier.offset = 0;
                barrier.size = buf->m_sizeInBytes;

                if (buf->GetGlobalState() == ResourceState::State::UnorderedAccess) {
                    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    bufSHADER2SHADERBarrier.push_back(barrier);
                }
                else if (buf->GetGlobalState() == ResourceState::State::ShaderResource) {
                    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    bufSHADER2SHADERBarrier.push_back(barrier);
                }
                else if (buf->GetGlobalState() == ResourceState::State::AccelerationStructure) {
                    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
                    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
                    bufAS2ASBarrier.push_back(barrier);
                }
                else {
                    Log::Fatal(L"Unsupported resource transition type in VK detected.");
                    return false;
                }
            }
            else {
                // texture (image)
                Texture* tex = (Texture*)res;
                VkImageLayout srcLayout = Resource::GetVkImageLayout(tex->GetGlobalState());
                VkImageLayout dstLayout = Resource::GetVkImageLayout(tex->GetGlobalState());

                if (srcLayout != dstLayout)
                {
                    VkImageMemoryBarrier barrier = {};
                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    barrier.oldLayout = srcLayout;
                    barrier.newLayout = dstLayout;
                    barrier.image = tex->m_apiData.m_image;
                    barrier.subresourceRange.aspectMask = Resource::GetVkImageAspectFlags(tex->m_format);
                    barrier.subresourceRange.baseArrayLayer = 0;
                    barrier.subresourceRange.baseMipLevel = 0;
                    barrier.subresourceRange.layerCount = tex->m_arraySize;
                    barrier.subresourceRange.levelCount = tex->m_mipLevels;;
                    barrier.srcAccessMask = Resource::GetVkAccessMask(tex->GetGlobalState());
                    barrier.dstAccessMask = Resource::GetVkAccessMask(tex->GetGlobalState());

                    imgBarrier.push_back(barrier);
                }
            }
        }

        if (bufSHADER2SHADERBarrier.size() > 0) {
            vkCmdPipelineBarrier(m_apiData.m_commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                0, nullptr,
                (uint32_t)bufSHADER2SHADERBarrier.size(), bufSHADER2SHADERBarrier.data(),
                0, nullptr);
        }
        if (bufAS2ASBarrier.size() > 0) {
            vkCmdPipelineBarrier(m_apiData.m_commandBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                0, nullptr,
                (uint32_t)bufAS2ASBarrier.size(), bufAS2ASBarrier.data(),
                0, nullptr);
        }
        if (imgBarrier.size() > 0) {
            vkCmdPipelineBarrier(m_apiData.m_commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                0, nullptr,
                0, nullptr,
                (uint32_t)imgBarrier.size(), imgBarrier.data());
        }


        return true;
    }

    bool CommandList::CopyTextureSingleMip(Device* dev, uint32_t mipIndex, Texture* dstTex, Buffer* srcUpBuf)
    {
        uint32_t rowPitch, totalSize;

        if (mipIndex > 0) {
            Log::Fatal(L"Copy texture only support the first mip.");
            return false;
        }
        if (!dstTex->GetUploadBufferFootplint(dev, mipIndex, &rowPitch, &totalSize)) {
            Log::Fatal(L"Faild to get upload size of a texture.");
            return false;
        }
        if (totalSize > srcUpBuf->m_sizeInBytes) {
            Log::Fatal(L"Src buffer size is too small to copy to a texture slice.");
            return false;
        }

        // to TRANSFER_DST_OPTIMAL
        {
            VkImageLayout srcLayout = Resource::GetVkImageLayout(dstTex->GetGlobalState());
            VkImageLayout dstLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

			VkImageMemoryBarrier barrier = {};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = srcLayout;
			barrier.newLayout = dstLayout;
			barrier.image = dstTex->m_apiData.m_image;
			barrier.subresourceRange.aspectMask = Resource::GetVkImageAspectFlags(dstTex->m_format);
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.layerCount = dstTex->m_arraySize;
			barrier.subresourceRange.levelCount = dstTex->m_mipLevels;;
			barrier.srcAccessMask = Resource::GetVkAccessMask(dstTex->GetGlobalState());
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			vkCmdPipelineBarrier(
				m_apiData.m_commandBuffer,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &barrier
			);
        }

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;

        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = mipIndex;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;

        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { dstTex->m_width, dstTex->m_height, dstTex->m_depth };

        vkCmdCopyBufferToImage(
            m_apiData.m_commandBuffer,
            srcUpBuf->m_apiData.m_buffer,
            dstTex->m_apiData.m_image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &region
        );

        // to shader resource for compute shader stage.
        {
            dstTex->SetGlobalState(ResourceState::State::ShaderResource);

            VkImageLayout srcLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            VkImageLayout dstLayout = Resource::GetVkImageLayout(dstTex->GetGlobalState());

            VkImageMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = srcLayout;
            barrier.newLayout = dstLayout;
            barrier.image = dstTex->m_apiData.m_image;
            barrier.subresourceRange.aspectMask = Resource::GetVkImageAspectFlags(dstTex->m_format);
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.layerCount = dstTex->m_arraySize;
            barrier.subresourceRange.levelCount = dstTex->m_mipLevels;;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = Resource::GetVkAccessMask(dstTex->GetGlobalState());

            vkCmdPipelineBarrier(
                m_apiData.m_commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier
            );
        }

        return true;
    }

    void CommandList::CopyBufferRegion(Buffer* dst, uint64_t dstOffset, Buffer* src, uint64_t srcOffset, uint64_t copySizeInBytes)
    {
#if 0
        {
            std::vector<VkBufferMemoryBarrier>     bufBarrier;

            VkBufferMemoryBarrier b = {};
            b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            b.srcAccessMask = 0;
            b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.buffer = dst->m_apiData.m_buffer;
            b.size = dst->m_sizeInBytes;
            bufBarrier.push_back(b);

            b.srcAccessMask = 0;
            b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            b.buffer = src->m_apiData.m_buffer;
            b.size = src->m_sizeInBytes;
            bufBarrier.push_back(b);

            vkCmdPipelineBarrier(m_apiData.m_commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                0, nullptr,
                (uint32_t)bufBarrier.size(), bufBarrier.data(),
                0, nullptr);
        }
#endif
        {
            VkBufferCopy copyRegion{};
            copyRegion.srcOffset = srcOffset;
            copyRegion.dstOffset = dstOffset;
            copyRegion.size = copySizeInBytes;

            vkCmdCopyBuffer(m_apiData.m_commandBuffer, src->m_apiData.m_buffer, dst->m_apiData.m_buffer, 1, &copyRegion);
        }
#if 0
        {
            VkPipelineStageFlagBits     dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

            VkBufferMemoryBarrier b = {};
            b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.buffer = dst->m_apiData.m_buffer;
            b.size = dst->m_sizeInBytes;

            if (dst->GetGlobalState() == Buffer::State::ShaderResource) {
                b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            }
            else if (dst->GetGlobalState() == Buffer::State::UnorderedAccess) {
                b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            }
            else if (dst->GetGlobalState() == Buffer::State::CopyDest && dst->m_cpuAccess == Buffer::CpuAccess::Read) {
                b.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                dstStage = VK_PIPELINE_STAGE_HOST_BIT;
            }

            vkCmdPipelineBarrier(m_apiData.m_commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, dstStage, 0,
                0, nullptr,
                (uint32_t)1, &b,
                0, nullptr);
        }
#endif
        return;
    }

    void CommandList::CopyTextureRegion(Texture* dst, Texture* src)
    {
        constexpr uint32_t kNumBuf = 2;
        GraphicsAPI::Resource* dstBufArr[kNumBuf] = { dst, src };
        GraphicsAPI::ResourceState::State	desiredStateArr[kNumBuf] = { GraphicsAPI::ResourceState::State::CopyDest, GraphicsAPI::ResourceState::State::CopySource };
        ResourceTransitionBarrier(dstBufArr, kNumBuf, desiredStateArr);

        VkImageLayout srcLayout = Resource::GetVkImageLayout(src->GetGlobalState());
        VkImageLayout dstLayout = Resource::GetVkImageLayout(dst->GetGlobalState());

        VkImageCopy region = {};
        region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.srcSubresource.mipLevel = 0;
        region.srcSubresource.baseArrayLayer = 0;
        region.srcSubresource.layerCount = 1;
        region.srcOffset.x = 0;
        region.srcOffset.y = 0;
        region.srcOffset.z = 0;

        region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.dstSubresource.mipLevel = 0;
        region.dstSubresource.baseArrayLayer = 0;
        region.dstSubresource.layerCount = 1;
        region.dstOffset.x = 0;
        region.dstOffset.y = 0;
        region.dstOffset.z = 0;

        region.extent.width = src->m_width;
        region.extent.height = src->m_height;
        region.extent.depth = src->m_depth;

        vkCmdCopyImage(
            m_apiData.m_commandBuffer,
            src->m_apiData.m_image,
            srcLayout,
            dst->m_apiData.m_image,
            dstLayout,
            1, &region);
    }

    void CommandList::CopyResource(Texture* , Texture* )
    {
        assert(false);
    }

    void CommandList::SetComputeRootDescriptorTable(RootSignature* rootSig, uint32_t baseSlotIndex, DescriptorTable** tables, size_t numTables)
    {
        std::vector<VkDescriptorSet>    sets;
        for (size_t i = 0; i < numTables; ++i) {
            sets.push_back(tables[i]->m_apiData.m_heapAllocationInfo.m_descSet);
        }
        
        vkCmdBindDescriptorSets(
            m_apiData.m_commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            rootSig->m_apiData.m_pipelineLayout,
            baseSlotIndex,
            (uint32_t)numTables,
            sets.data(),
            0, nullptr);
    };

    void CommandList::SetRayTracingRootDescriptorTable(RootSignature* rootSig, uint32_t baseSlotIndex, DescriptorTable** tables, size_t numTables)
    {
        std::vector<VkDescriptorSet>    sets;
        for (size_t i = 0; i < numTables; ++i) {
            sets.push_back(tables[i]->m_apiData.m_heapAllocationInfo.m_descSet);
        }

        vkCmdBindDescriptorSets(
            m_apiData.m_commandBuffer,
            VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
            rootSig->m_apiData.m_pipelineLayout,
            baseSlotIndex,
            (uint32_t)numTables,
            sets.data(),
            0, nullptr);
    };

    void CommandList::SetComputeRootSignature(RootSignature* /*rootSig*/)
    {
        // nothing to do in vk??
    }

    void CommandList::SetComputePipelineState(ComputePipelineState* pso)
    {
        vkCmdBindPipeline(m_apiData.m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pso->m_apiData.m_pipeline);
    }

    void CommandList::SetRayTracingPipelineState(RaytracingPipelineState* rtPSO)
    {
        vkCmdBindPipeline(m_apiData.m_commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtPSO->m_apiData.m_pipeline);
    }

    void CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z)
    {
        vkCmdDispatch(m_apiData.m_commandBuffer, x, y, z);
    }

    void CommandList::BeginEvent(const std::array<uint32_t, 3>& color, const std::string& str)
    {
        (void)color;

        VkDebugUtilsLabelEXT s{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, nullptr, str.c_str(), {1.0f, 1.0f, 1.0f, 1.0f} };
        VK::vkCmdBeginDebugUtilsLabelEXT(m_apiData.m_commandBuffer, &s);
    }

    void CommandList::EndEvent()
    {
        VK::vkCmdEndDebugUtilsLabelEXT(m_apiData.m_commandBuffer);
    }

#endif

    namespace Utils {
        ScopedEventObject::ScopedEventObject(CommandList* cmdList, const std::array<uint32_t, 3>& color, const std::string &str)
        {
            m_cmdList = cmdList;
            m_cmdList->BeginEvent(color, str);
        };

        ScopedEventObject::~ScopedEventObject()
        {
            m_cmdList->EndEvent();
        };

#if defined(GRAPHICS_API_D3D12)
        D3D12_SHADER_RESOURCE_VIEW_DESC BufferResourceViewDesc_R32F(UINT64 firstElm, UINT numElm)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

            srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.FirstElement = firstElm;
            srvDesc.Buffer.NumElements = numElm;
            srvDesc.Buffer.StructureByteStride = 0;
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

            return srvDesc;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC BufferResourceViewDesc_R32U(UINT64 firstElm, UINT numElm)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

            srvDesc.Format = DXGI_FORMAT_R32_UINT;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.FirstElement = firstElm;
            srvDesc.Buffer.NumElements = numElm;
            srvDesc.Buffer.StructureByteStride = 0;
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

            return srvDesc;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC BufferResourceViewDesc_R16U(UINT64 firstElm, UINT numElm)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

            srvDesc.Format = DXGI_FORMAT_R16_UINT;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.FirstElement = firstElm;
            srvDesc.Buffer.NumElements = numElm;
            srvDesc.Buffer.StructureByteStride = 0;
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

            return srvDesc;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC BufferResourceViewDesc_Tex2DFloat_SingleSlice(ID3D12Resource* res)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

            auto desc = res->GetDesc();

            switch (desc.Format) {
            case DXGI_FORMAT_R32G32B32A32_TYPELESS:
                srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                break;
            case DXGI_FORMAT_R32G32B32_TYPELESS:
                srvDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
                break;
            case DXGI_FORMAT_R32G32_TYPELESS:
                srvDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
                break;
            case DXGI_FORMAT_R32_TYPELESS:
                srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
                break;

            default:
                srvDesc.Format = desc.Format;
                break;
            }

            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.PlaneSlice = 0;
            srvDesc.Texture2D.ResourceMinLODClamp = 0;

            return srvDesc;
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC BufferAccessViewDesc_R32F(UINT64 firstElm, UINT numElm)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

            uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.FirstElement = firstElm;
            uavDesc.Buffer.NumElements = numElm;
            uavDesc.Buffer.StructureByteStride = 0;
            uavDesc.Buffer.CounterOffsetInBytes = 0;
            uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

            return uavDesc;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC BufferAccessViewDesc_R32U(UINT64 firstElm, UINT numElm)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

            uavDesc.Format = DXGI_FORMAT_R32_UINT;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.FirstElement = firstElm;
            uavDesc.Buffer.NumElements = numElm;
            uavDesc.Buffer.StructureByteStride = 0;
            uavDesc.Buffer.CounterOffsetInBytes = 0;
            uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

            return uavDesc;
        }
#endif
    };


    /***************************************************************
     * Utils
     ***************************************************************/
    namespace Utils
    {
#if defined(GRAPHICS_API_D3D12)
		std::wstring GetName(ID3D12Object* obj)
		{
			static const wchar_t* nullstr = L"NULL_D3DObject";
			static const wchar_t* emptystr = L"EMPTY_NAME";

			if (obj == nullptr)
				return nullstr;

			wchar_t     wBuf[1024];
			UINT        siz = sizeof(wBuf) - 2;
			memset(wBuf, 0, sizeof(wBuf));
			HRESULT hr = obj->GetPrivateData(WKPDID_D3DDebugObjectNameW, &siz, wBuf);
			wBuf[1023] = L'\0';
			if (SUCCEEDED(hr) && siz > 0)
				return std::wstring(wBuf);

			return std::wstring();
		};
#endif
    };

    /***************************************************************
     * QueryPool in VK
     * Not abstracted at all. Just for resource destruction.
     ***************************************************************/
#if defined(GRAPHICS_API_VK)
    QueryPool_VK::~QueryPool_VK()
    {
        if (m_apiData.m_device && m_apiData.m_queryPool) {
            vkDestroyQueryPool(m_apiData.m_device, m_apiData.m_queryPool, nullptr);
        }
    }

    bool QueryPool_VK::Create(Device* dev, const QueryPool_VK::InitInfo& initInfo)
    {
        VkQueryPoolCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        info.flags = initInfo.m_createFlags;
        info.queryType = initInfo.m_queryType;
        info.queryCount = initInfo.m_poolSize;

        if (vkCreateQueryPool(dev->m_apiData.m_device, &info, nullptr, &m_apiData.m_queryPool) != VK_SUCCESS) {
            Log::Fatal(L"Failed to allocate queryPool.");
            return false;

        }
        m_apiData.m_device = dev->m_apiData.m_device;
            
        return true;
    }
#endif
};

