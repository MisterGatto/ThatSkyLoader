#include <cstdio>
#include <vector>
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <vk_layer_dispatch_table.h>
#include "vulkan/vk_platform.h"
#include "vulkan/vulkan_core.h"

#include "include/layer.h"
#include "include/menu.hpp"

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_impl_win32.h>

#include <assert.h>
#include <string.h>
#include <iostream>
#include <mutex>
#include <map>
#include <algorithm>
#include <memory>

#define VK_LAYER_EXPORT extern "C" __declspec(dllexport)

#define vk_foreach_struct(__iter, __start) \
  for (struct VkBaseOutStructure *__iter = (struct VkBaseOutStructure *)(__start); \
    __iter; __iter = __iter->pNext)

std::mutex global_lock;
using scoped_lock = std::lock_guard<std::mutex>;

template<typename DispatchableType>
void* GetKey(DispatchableType inst) {
    if (!inst) return nullptr;
    return *(void**)inst;
}

struct QueueData;

struct InstanceData {
    VkLayerInstanceDispatchTable vtable;
    VkInstance instance;
};

struct DeviceData {
    InstanceData* instance = nullptr;
    PFN_vkSetDeviceLoaderData set_device_loader_data = nullptr;
    VkLayerDispatchTable vtable = {};
    VkDevice device = VK_NULL_HANDLE;
    QueueData* graphic_queue = nullptr;
    std::vector<QueueData*> queues;
};

struct QueueData {
    DeviceData* device;
    VkQueue queue;
    uint32_t family_index = 0;
};

std::map<void*, DeviceData> g_device_dispatch;
std::map<void*, QueueData> g_queue_data;
std::map<void*, VkLayerInstanceDispatchTable> instance_dispatch;
std::map<void*, VkLayerDispatchTable> device_dispatch;

DeviceData* GetDeviceData(void* key) {
    scoped_lock l(global_lock);
    return &g_device_dispatch[GetKey(key)];
}

QueueData* GetQueueData(void* key) {
    scoped_lock l(global_lock);
    return &g_queue_data[key];
}

static QueueData* new_queue_data(VkQueue queue, DeviceData* device_data, uint32_t family_index) {
    QueueData* data = GetQueueData(queue);
    data->device = device_data;
    data->queue = queue;
    data->family_index = family_index;
    if (!device_data->graphic_queue) {
        device_data->graphic_queue = data;
    }
    return data;
}

static void DeviceMapQueues(DeviceData* data, const VkDeviceCreateInfo* pCreateInfo) {
    for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
        uint32_t fam = pCreateInfo->pQueueCreateInfos[i].queueFamilyIndex;
        for (uint32_t j = 0; j < pCreateInfo->pQueueCreateInfos[i].queueCount; j++) {
            VkQueue queue = VK_NULL_HANDLE;
            data->vtable.GetDeviceQueue(data->device, fam, j, &queue);
            if (queue != VK_NULL_HANDLE) {
                if (data->set_device_loader_data) {
                    data->set_device_loader_data(data->device, queue);
                }
                data->queues.push_back(new_queue_data(queue, data, fam));
                scoped_lock l(global_lock);
                device_dispatch[GetKey(queue)] = data->vtable;
            }
        }
    }
}

static VkLayerDeviceCreateInfo* get_device_chain_info(const VkDeviceCreateInfo* pCreateInfo, VkLayerFunction func) {
    vk_foreach_struct(item, pCreateInfo->pNext) {
        if (item->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
            ((VkLayerDeviceCreateInfo*)item)->function == func) {
            return (VkLayerDeviceCreateInfo*)item;
        }
    }
    return nullptr;
}

static VkAllocationCallbacks* g_Allocator = nullptr;
static VkInstance g_Instance = VK_NULL_HANDLE;

VkPhysicalDevice g_PhysicalDevice = VK_NULL_HANDLE;
VkDevice g_Device = VK_NULL_HANDLE;
VkQueue g_GraphicsQueue = VK_NULL_HANDLE;
VkCommandBuffer g_CommandBuffer = VK_NULL_HANDLE;
VkDescriptorPool g_DescriptorPool = VK_NULL_HANDLE;
PFN_vkGetPhysicalDeviceMemoryProperties g_pfnGetPhysicalDeviceMemoryProperties = nullptr;

static uint32_t g_QueueFamily = static_cast<uint32_t>(-1);
static std::vector<VkQueueFamilyProperties> g_QueueFamilies;

static VkPipelineCache g_PipelineCache = VK_NULL_HANDLE;
static uint32_t g_MinImageCount = 1;
static VkRenderPass g_RenderPass = VK_NULL_HANDLE;
static ImGui_ImplVulkanH_Frame g_Frames[16] = {};
static ImGui_ImplVulkanH_FrameSemaphores g_FrameSemaphores[16] = {};

static HWND g_Hwnd = nullptr;
static VkExtent2D g_ImageExtent = {};
static VkFormat g_ImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

static void CleanupRenderTarget() {
    if (g_Device == VK_NULL_HANDLE) {
        return;
    }
    
    for (uint32_t i = 0; i < RTL_NUMBER_OF(g_Frames); ++i) {
        if (g_Frames[i].Fence != VK_NULL_HANDLE) {
            vkWaitForFences(g_Device, 1, &g_Frames[i].Fence, VK_TRUE, 1000000000);
            vkDestroyFence(g_Device, g_Frames[i].Fence, g_Allocator);
            g_Frames[i].Fence = VK_NULL_HANDLE;
        }
        
        if (g_Frames[i].CommandBuffer != VK_NULL_HANDLE && g_Frames[i].CommandPool != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(g_Device, g_Frames[i].CommandPool, 1, &g_Frames[i].CommandBuffer);
            g_Frames[i].CommandBuffer = VK_NULL_HANDLE;
        }
        
        if (g_Frames[i].CommandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(g_Device, g_Frames[i].CommandPool, g_Allocator);
            g_Frames[i].CommandPool = VK_NULL_HANDLE;
        }
        
        if (g_Frames[i].BackbufferView != VK_NULL_HANDLE) {
            vkDestroyImageView(g_Device, g_Frames[i].BackbufferView, g_Allocator);
            g_Frames[i].BackbufferView = VK_NULL_HANDLE;
        }
        
        if (g_Frames[i].Framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(g_Device, g_Frames[i].Framebuffer, g_Allocator);
            g_Frames[i].Framebuffer = VK_NULL_HANDLE;
        }
        
        g_Frames[i].Backbuffer = VK_NULL_HANDLE;
    }

    for (uint32_t i = 0; i < RTL_NUMBER_OF(g_FrameSemaphores); ++i) {
        if (g_FrameSemaphores[i].ImageAcquiredSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(g_Device, g_FrameSemaphores[i].ImageAcquiredSemaphore, g_Allocator);
            g_FrameSemaphores[i].ImageAcquiredSemaphore = VK_NULL_HANDLE;
        }
        if (g_FrameSemaphores[i].RenderCompleteSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(g_Device, g_FrameSemaphores[i].RenderCompleteSemaphore, g_Allocator);
            g_FrameSemaphores[i].RenderCompleteSemaphore = VK_NULL_HANDLE;
        }
    }
    
    if (g_RenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(g_Device, g_RenderPass, g_Allocator);
        g_RenderPass = VK_NULL_HANDLE;
    }
}

static void CreateRenderTarget(VkDevice device, VkSwapchainKHR swapchain) {
    VkResult result = VK_SUCCESS;
    
    uint32_t uImageCount = 0;
    result = vkGetSwapchainImagesKHR(device, swapchain, &uImageCount, nullptr);
    if (result != VK_SUCCESS || uImageCount == 0) {
        return;
    }
    
    if (uImageCount > RTL_NUMBER_OF(g_Frames)) {
        uImageCount = RTL_NUMBER_OF(g_Frames);
    }

    VkImage backbuffers[16] = {};
    result = vkGetSwapchainImagesKHR(device, swapchain, &uImageCount, backbuffers);
    if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
        return;
    }
    
    g_MinImageCount = uImageCount;
    
    for (uint32_t i = 0; i < uImageCount; ++i) {
        g_Frames[i].Backbuffer = backbuffers[i];

        ImGui_ImplVulkanH_Frame* fd = &g_Frames[i];
        ImGui_ImplVulkanH_FrameSemaphores* fsd = &g_FrameSemaphores[i];
        
        {
            VkCommandPoolCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            info.queueFamilyIndex = (g_QueueFamily != static_cast<uint32_t>(-1)) ? g_QueueFamily : 0;

            result = vkCreateCommandPool(device, &info, g_Allocator, &fd->CommandPool);
            if (result != VK_SUCCESS) {
                return;
            }
        }
        
        {
            VkCommandBufferAllocateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            info.commandPool = fd->CommandPool;
            info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            info.commandBufferCount = 1;

            result = vkAllocateCommandBuffers(device, &info, &fd->CommandBuffer);
            if (result != VK_SUCCESS) {
                return;
            }
        }
        
        {
            VkFenceCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            result = vkCreateFence(device, &info, g_Allocator, &fd->Fence);
            if (result != VK_SUCCESS) {
                return;
            }
        }

        {
            VkSemaphoreCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            result = vkCreateSemaphore(device, &info, g_Allocator, &fsd->ImageAcquiredSemaphore);
            if (result != VK_SUCCESS) {
                return;
            }

            result = vkCreateSemaphore(device, &info, g_Allocator, &fsd->RenderCompleteSemaphore);
            if (result != VK_SUCCESS) {
                return;
            }
        }
    }

    if (g_RenderPass == VK_NULL_HANDLE) {
        VkAttachmentDescription attachment = {};
        attachment.format = (g_ImageFormat != VK_FORMAT_UNDEFINED) ? g_ImageFormat : VK_FORMAT_B8G8R8A8_UNORM;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference color_attachment = {};
        color_attachment.attachment = 0;
        color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment;

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments = &attachment;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;

        result = vkCreateRenderPass(device, &info, g_Allocator, &g_RenderPass);
        if (result != VK_SUCCESS) {
            return;
        }
    }

    {
        VkImageViewCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = (g_ImageFormat != VK_FORMAT_UNDEFINED) ? g_ImageFormat : VK_FORMAT_B8G8R8A8_UNORM;
        info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.baseMipLevel = 0;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount = 1;
        
        for (uint32_t i = 0; i < uImageCount; ++i) {
            ImGui_ImplVulkanH_Frame* fd = &g_Frames[i];
            info.image = fd->Backbuffer;

            result = vkCreateImageView(device, &info, g_Allocator, &fd->BackbufferView);
            if (result != VK_SUCCESS) {
                return;
            }
        }
    }

    {
        const uint32_t width = (g_ImageExtent.width > 0) ? g_ImageExtent.width : 2560;
        const uint32_t height = (g_ImageExtent.height > 0) ? g_ImageExtent.height : 1440;
        
        for (uint32_t i = 0; i < uImageCount; ++i) {
            ImGui_ImplVulkanH_Frame* fd = &g_Frames[i];
            VkImageView attachment[1] = { fd->BackbufferView };

            VkFramebufferCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            info.renderPass = g_RenderPass;
            info.attachmentCount = 1;
            info.pAttachments = attachment;
            info.layers = 1;
            info.width = width;
            info.height = height;

            result = vkCreateFramebuffer(device, &info, g_Allocator, &fd->Framebuffer);
            if (result != VK_SUCCESS) {
                return;
            }
        }
    }

    if (!g_DescriptorPool) {
        constexpr VkDescriptorPoolSize pool_sizes[] = {
            {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
        };

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
        pool_info.poolSizeCount = static_cast<uint32_t>(IM_ARRAYSIZE(pool_sizes));
        pool_info.pPoolSizes = pool_sizes;

        result = vkCreateDescriptorPool(device, &pool_info, g_Allocator, &g_DescriptorPool);
        if (result != VK_SUCCESS) {
            return;
        }
    }
}

static bool DoesQueueSupportGraphic(VkQueue queue, VkQueue* pGraphicQueue) {
    if (queue == VK_NULL_HANDLE || g_Device == VK_NULL_HANDLE) {
        return false;
    }
    
    bool queueSupportsGraphics = false;
    
    for (uint32_t i = 0; i < g_QueueFamilies.size(); ++i) {
        const VkQueueFamilyProperties& family = g_QueueFamilies[i];
        const bool familySupportsGraphics = (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        
        for (uint32_t j = 0; j < family.queueCount; ++j) {
            VkQueue currentQueue = VK_NULL_HANDLE;
            vkGetDeviceQueue(g_Device, i, j, &currentQueue);
            
            if (currentQueue == VK_NULL_HANDLE) {
                continue;
            }

            if (pGraphicQueue && familySupportsGraphics && *pGraphicQueue == VK_NULL_HANDLE) {
                *pGraphicQueue = currentQueue;
            }

            if (queue == currentQueue && familySupportsGraphics) {
                queueSupportsGraphics = true;
                if (!pGraphicQueue || *pGraphicQueue != VK_NULL_HANDLE) {
                    return true;
                }
            }
        }
    }
    return queueSupportsGraphics;
}

static VkResult RenderImGui_Vulkan(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    if (!queue || !pPresentInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    QueueData* queue_data = GetQueueData(queue);
    if (!queue_data || !queue_data->device) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    g_Device = queue_data->device->device;
    if (g_Device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = VK_SUCCESS;
    VkQueue graphicQueue = queue_data->device->graphic_queue ? queue_data->device->graphic_queue->queue : queue;
    DoesQueueSupportGraphic(queue, &graphicQueue);

    if (g_QueueFamily == static_cast<uint32_t>(-1)) {
        if (queue_data->device->graphic_queue) {
            g_QueueFamily = queue_data->device->graphic_queue->family_index;
        } else {
            g_QueueFamily = 0;
        }
    }
    
    Menu::InitializeContext(g_Hwnd);

    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; ++i) {
        VkSwapchainKHR swapchain = pPresentInfo->pSwapchains[i];
        uint32_t image_index = pPresentInfo->pImageIndices[i];
        if (image_index >= RTL_NUMBER_OF(g_Frames)) {
            if (queue_data->device->vtable.QueuePresentKHR) {
                return queue_data->device->vtable.QueuePresentKHR(queue, pPresentInfo);
            }
            return VK_SUCCESS;
        }

        if (g_Frames[image_index].Framebuffer == VK_NULL_HANDLE) {
            CreateRenderTarget(g_Device, swapchain);
        }

        if (g_Frames[image_index].Framebuffer == VK_NULL_HANDLE) {
            if (queue_data->device->vtable.QueuePresentKHR) {
                return queue_data->device->vtable.QueuePresentKHR(queue, pPresentInfo);
            }
            return VK_SUCCESS;
        }
        
        ImGui_ImplVulkanH_Frame* fd = &g_Frames[image_index];
        ImGui_ImplVulkanH_FrameSemaphores* fsd = &g_FrameSemaphores[image_index];
        
        if (fd->Fence != VK_NULL_HANDLE) {
            result = vkWaitForFences(g_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
            if (result != VK_SUCCESS) {
                if (queue_data->device->vtable.QueuePresentKHR) {
                    return queue_data->device->vtable.QueuePresentKHR(queue, pPresentInfo);
                }
                return result;
            }
            result = vkResetFences(g_Device, 1, &fd->Fence);
            if (result != VK_SUCCESS) {
                if (queue_data->device->vtable.QueuePresentKHR) {
                    return queue_data->device->vtable.QueuePresentKHR(queue, pPresentInfo);
                }
                return result;
            }
        }

        if (fd->CommandBuffer == VK_NULL_HANDLE) {
            if (queue_data->device->vtable.QueuePresentKHR) {
                return queue_data->device->vtable.QueuePresentKHR(queue, pPresentInfo);
            }
            return VK_SUCCESS;
        }
        
        result = vkResetCommandBuffer(fd->CommandBuffer, 0);
        if (result != VK_SUCCESS) {
            if (queue_data->device->vtable.QueuePresentKHR) {
                return queue_data->device->vtable.QueuePresentKHR(queue, pPresentInfo);
            }
            return result;
        }

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        result = vkBeginCommandBuffer(fd->CommandBuffer, &beginInfo);
        if (result != VK_SUCCESS) {
            if (queue_data->device->vtable.QueuePresentKHR) {
                return queue_data->device->vtable.QueuePresentKHR(queue, pPresentInfo);
            }
            return result;
        }
        
        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = g_RenderPass;
        renderPassInfo.framebuffer = fd->Framebuffer;
        
        if (g_ImageExtent.width == 0 || g_ImageExtent.height == 0) {
            renderPassInfo.renderArea.extent.width = 2560;
            renderPassInfo.renderArea.extent.height = 1440;
        } else {
            renderPassInfo.renderArea.extent = g_ImageExtent;
        }

        vkCmdBeginRenderPass(fd->CommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        if (!ImGui::GetIO().BackendRendererUserData) {
            ImGui_ImplVulkan_InitInfo init_info = {};
            init_info.Instance = g_Instance;
            init_info.PhysicalDevice = g_PhysicalDevice;
            init_info.Device = g_Device;
            init_info.QueueFamily = g_QueueFamily;
            init_info.Queue = graphicQueue;
            init_info.PipelineCache = g_PipelineCache;
            init_info.DescriptorPool = g_DescriptorPool;
            init_info.RenderPass = g_RenderPass;
            init_info.Subpass = 0;
            init_info.MinImageCount = g_MinImageCount;
            init_info.ImageCount = g_MinImageCount;
            init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            init_info.Allocator = g_Allocator;
            
            ImGui_ImplVulkan_Init(&init_info);
            ImGui_ImplVulkan_CreateFontsTexture();
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        Menu::Render();

        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), fd->CommandBuffer);

        vkCmdEndRenderPass(fd->CommandBuffer);
        
        result = vkEndCommandBuffer(fd->CommandBuffer);
        if (result != VK_SUCCESS) {
            if (queue_data->device->vtable.QueuePresentKHR) {
                return queue_data->device->vtable.QueuePresentKHR(queue, pPresentInfo);
            }
            return result;
        }

        uint32_t waitSemaphoresCount = i == 0 ? pPresentInfo->waitSemaphoreCount : 0;
        std::vector<VkPipelineStageFlags> stages_wait(waitSemaphoresCount > 0 ? waitSemaphoresCount : 1, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &fd->CommandBuffer;
        submitInfo.pWaitDstStageMask = stages_wait.data();
        submitInfo.waitSemaphoreCount = waitSemaphoresCount;
        submitInfo.pWaitSemaphores = pPresentInfo->pWaitSemaphores;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &fsd->ImageAcquiredSemaphore;
        
        result = vkQueueSubmit(graphicQueue, 1, &submitInfo, fd->Fence);
        if (result != VK_SUCCESS) {
            if (queue_data->device->vtable.QueuePresentKHR) {
                return queue_data->device->vtable.QueuePresentKHR(queue, pPresentInfo);
            }
            return result;
        }

        VkPresentInfoKHR present_info = *pPresentInfo;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &swapchain;
        present_info.pImageIndices = &image_index;
        present_info.pWaitSemaphores = &fsd->ImageAcquiredSemaphore;
        present_info.waitSemaphoreCount = 1;

        VkResult chain_result = VK_SUCCESS;
        if (queue_data->device->vtable.QueuePresentKHR) {
            chain_result = queue_data->device->vtable.QueuePresentKHR(queue, &present_info);
        } else {
            scoped_lock l(global_lock);
            auto it = device_dispatch.find(GetKey(queue));
            if (it != device_dispatch.end() && it->second.QueuePresentKHR) {
                chain_result = it->second.QueuePresentKHR(queue, &present_info);
            }
        }
        
        if (pPresentInfo->pResults) {
            pPresentInfo->pResults[i] = chain_result;
        }
        
        if (chain_result != VK_SUCCESS && result == VK_SUCCESS) {
            result = chain_result;
        }
    }
    
    return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL ModLoader_CreateInstance(
    const VkInstanceCreateInfo*                 pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkInstance*                                 pInstance)
{
  VkLayerInstanceCreateInfo *layerCreateInfo = (VkLayerInstanceCreateInfo *)pCreateInfo->pNext;

  while(layerCreateInfo && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO ||
                            layerCreateInfo->function != VK_LAYER_LINK_INFO))
  {
    layerCreateInfo = (VkLayerInstanceCreateInfo *)layerCreateInfo->pNext;
  }

  if(layerCreateInfo == NULL || !layerCreateInfo->u.pLayerInfo)
  {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  PFN_vkGetInstanceProcAddr gpa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
  layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

  PFN_vkCreateInstance createFunc = (PFN_vkCreateInstance)gpa(VK_NULL_HANDLE, "vkCreateInstance");
  if (!createFunc) {
      return VK_ERROR_INITIALIZATION_FAILED;
  }

  VkResult ret = createFunc(pCreateInfo, pAllocator, pInstance);
  if (ret != VK_SUCCESS || !pInstance || !*pInstance) {
      return ret;
  }

  g_Instance = *pInstance;
  g_pfnGetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)gpa(*pInstance, "vkGetPhysicalDeviceMemoryProperties");

  VkLayerInstanceDispatchTable dispatchTable = {};
  dispatchTable.GetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)gpa(*pInstance, "vkGetInstanceProcAddr");
  if (!dispatchTable.GetInstanceProcAddr) {
      dispatchTable.GetInstanceProcAddr = gpa;
  }
  dispatchTable.DestroyInstance = (PFN_vkDestroyInstance)gpa(*pInstance, "vkDestroyInstance");

  {
    scoped_lock l(global_lock);
    instance_dispatch[GetKey(*pInstance)] = dispatchTable;
  }

  return VK_SUCCESS;
}

VK_LAYER_EXPORT void VKAPI_CALL ModLoader_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator)
{
  if (!instance) return;
  PFN_vkDestroyInstance destroyFunc = nullptr;
  {
    scoped_lock l(global_lock);
    auto it = instance_dispatch.find(GetKey(instance));
    if (it != instance_dispatch.end()) {
        destroyFunc = it->second.DestroyInstance;
        instance_dispatch.erase(it);
    }
  }
  if (destroyFunc) destroyFunc(instance, pAllocator);
  if (g_Instance == instance) g_Instance = VK_NULL_HANDLE;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL ModLoader_CreateDevice(
    VkPhysicalDevice                            physicalDevice,
    const VkDeviceCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDevice*                                   pDevice)
{
  VkLayerDeviceCreateInfo *layerCreateInfo = (VkLayerDeviceCreateInfo *)pCreateInfo->pNext;

  while(layerCreateInfo && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO ||
                            layerCreateInfo->function != VK_LAYER_LINK_INFO))
  {
    layerCreateInfo = (VkLayerDeviceCreateInfo *)layerCreateInfo->pNext;
  }

  if(layerCreateInfo == NULL || !layerCreateInfo->u.pLayerInfo)
  {
    return VK_ERROR_INITIALIZATION_FAILED;
  }
  
  PFN_vkGetInstanceProcAddr gipa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
  PFN_vkGetDeviceProcAddr gdpa = layerCreateInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
  layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

  PFN_vkCreateDevice createFunc = (PFN_vkCreateDevice)gipa(VK_NULL_HANDLE, "vkCreateDevice");
  if (!createFunc) {
      return VK_ERROR_INITIALIZATION_FAILED;
  }

  VkResult ret = createFunc(physicalDevice, pCreateInfo, pAllocator, pDevice);
  if (ret != VK_SUCCESS || !pDevice || !*pDevice) {
      return ret;
  }

  g_PhysicalDevice = physicalDevice;
  g_Device = *pDevice;
  if (!g_pfnGetPhysicalDeviceMemoryProperties && gipa) {
      g_pfnGetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)gipa(g_Instance, "vkGetPhysicalDeviceMemoryProperties");
  }

  VkLayerDispatchTable dispatchTable = {};
  dispatchTable.GetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)gdpa(*pDevice, "vkGetDeviceProcAddr");
  if (!dispatchTable.GetDeviceProcAddr) {
      dispatchTable.GetDeviceProcAddr = gdpa;
  }
  dispatchTable.DestroyDevice = (PFN_vkDestroyDevice)gdpa(*pDevice, "vkDestroyDevice");
  dispatchTable.QueuePresentKHR = (PFN_vkQueuePresentKHR)gdpa(*pDevice, "vkQueuePresentKHR");
  dispatchTable.CreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)gdpa(*pDevice, "vkCreateSwapchainKHR");
  dispatchTable.AcquireNextImageKHR = (PFN_vkAcquireNextImageKHR)gdpa(*pDevice, "vkAcquireNextImageKHR");
  dispatchTable.GetDeviceQueue = (PFN_vkGetDeviceQueue)gdpa(*pDevice, "vkGetDeviceQueue");

  DeviceData* devData = GetDeviceData(*pDevice);
  if (devData) {
      devData->vtable = dispatchTable;
      devData->device = *pDevice;

      VkLayerDeviceCreateInfo *load_data_info = get_device_chain_info(pCreateInfo, VK_LOADER_DATA_CALLBACK);
      if (load_data_info) {
          devData->set_device_loader_data = load_data_info->u.pfnSetDeviceLoaderData;
      }
      DeviceMapQueues(devData, pCreateInfo);
  }

  {
    scoped_lock l(global_lock);
    device_dispatch[GetKey(*pDevice)] = dispatchTable;
  }

  return VK_SUCCESS;
}

VK_LAYER_EXPORT void VKAPI_CALL ModLoader_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
{
  if (!device) return;
  PFN_vkDestroyDevice destroyFunc = nullptr;
  {
    scoped_lock l(global_lock);
    auto it = device_dispatch.find(GetKey(device));
    if (it != device_dispatch.end()) {
        destroyFunc = it->second.DestroyDevice;
        device_dispatch.erase(it);
    }
  }
  if (destroyFunc) destroyFunc(device, pAllocator);
  if (g_Device == device) g_Device = VK_NULL_HANDLE;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL ModLoader_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo){
    QueueData* queue_data = GetQueueData(queue);
    if (!Menu::bShowMenu || !g_Hwnd || !queue_data || !queue_data->device || !queue_data->device->vtable.QueuePresentKHR) {
        if (queue_data && queue_data->device && queue_data->device->vtable.QueuePresentKHR) {
            return queue_data->device->vtable.QueuePresentKHR(queue, pPresentInfo);
        }
        scoped_lock l(global_lock);
        auto it = device_dispatch.find(GetKey(queue));
        if (it != device_dispatch.end() && it->second.QueuePresentKHR) {
            return it->second.QueuePresentKHR(queue, pPresentInfo);
        }
        return VK_SUCCESS;
    }
    return RenderImGui_Vulkan(queue, pPresentInfo);
}

VK_LAYER_EXPORT VkResult VKAPI_CALL ModLoader_CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) {
    CleanupRenderTarget();
    if (pCreateInfo) {
        g_ImageExtent = pCreateInfo->imageExtent;
        g_ImageFormat = pCreateInfo->imageFormat;
    }
    scoped_lock l(global_lock);
    auto it = device_dispatch.find(GetKey(device));
    if (it != device_dispatch.end() && it->second.CreateSwapchainKHR) {
        return it->second.CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    }
    return VK_ERROR_INITIALIZATION_FAILED;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL ModLoader_AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex) {
    scoped_lock l(global_lock);
    auto it = device_dispatch.find(GetKey(device));
    if (it != device_dispatch.end() && it->second.AcquireNextImageKHR) {
        return it->second.AcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);
    }
    return VK_SUCCESS;
}

#define GETPROCADDR(func) if(!strcmp(pName, "vk" #func)) return (PFN_vkVoidFunction)&ModLoader_##func;

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL ModLoader_GetDeviceProcAddr(VkDevice device, const char *pName)
{
  if (!pName) return nullptr;

  GETPROCADDR(GetDeviceProcAddr);
  GETPROCADDR(CreateDevice);
  GETPROCADDR(DestroyDevice);
  GETPROCADDR(QueuePresentKHR);
  GETPROCADDR(CreateSwapchainKHR);

  if (device) {
    scoped_lock l(global_lock);
    auto it = device_dispatch.find(GetKey(device));
    if (it != device_dispatch.end() && it->second.GetDeviceProcAddr) {
        return it->second.GetDeviceProcAddr(device, pName);
    }
  }

  return nullptr;
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL ModLoader_GetInstanceProcAddr(VkInstance instance, const char *pName)
{
  if (!pName) return nullptr;

  GETPROCADDR(GetInstanceProcAddr);
  GETPROCADDR(CreateInstance);
  GETPROCADDR(DestroyInstance);

  GETPROCADDR(GetDeviceProcAddr);
  GETPROCADDR(CreateDevice);
  GETPROCADDR(DestroyDevice);

  if (instance) {
    scoped_lock l(global_lock);
    auto it = instance_dispatch.find(GetKey(instance));
    if (it != instance_dispatch.end() && it->second.GetInstanceProcAddr) {
        return it->second.GetInstanceProcAddr(instance, pName);
    }
  }

  return nullptr;
}

void layer::setup(HWND hwnd){
    g_Hwnd = hwnd;
}