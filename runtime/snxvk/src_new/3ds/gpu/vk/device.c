/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "3ds/gpu/vk/vk.h"
#include "3ds/gpu/vk/vk_render.h"
#include <SDL3/SDL_vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    SnxCtrVk vk;
    int up;
} g_device;

SnxCtrVk* snx_ctr_vk(void) { return g_device.up ? &g_device.vk : NULL; }

bool snx_ctr_vk_window(uint32_t pa, uint64_t size, SnxCtrVkWindow* w, uint32_t* inside) {
    const SnxCtrVk* v = &g_device.vk;
    VkDeviceSize offset;
    if (!size || size > UINT32_MAX) return false;
    const SnxCtrVkRam* ram = snx_ctr_vk_ram(pa, (uint32_t)size, &offset);
    if (!ram) return false;
    VkDeviceSize alignment = v->properties.limits.minStorageBufferOffsetAlignment;
    VkDeviceSize start = offset / alignment * alignment;
    VkDeviceSize range = ram->size - start;
    if (range > v->properties.limits.maxStorageBufferRange) range = v->properties.limits.maxStorageBufferRange;
    if (offset - start + size > range) return false;
    *w = (SnxCtrVkWindow){ram->buffer, start, range};
    *inside = (uint32_t)(offset - start);
    return true;
}
uint32_t snx_ctr_vk_memory_type(uint32_t bits, VkMemoryPropertyFlags properties) {
    const VkPhysicalDeviceMemoryProperties* p = &g_device.vk.memory_properties;
    for (uint32_t i = 0; i < p->memoryTypeCount; ++i)
        if ((bits >> i & 1) && (p->memoryTypes[i].propertyFlags & properties) == properties) return i;
    return UINT32_MAX;
}
static bool has_extension(const VkExtensionProperties* list, uint32_t count, const char* name) {
    for (uint32_t i = 0; i < count; ++i)
        if (!strcmp(list[i].extensionName, name)) return true;
    return false;
}
/* A device of this physical device and queue family, if it has what the 3DS needs */
static bool make_device(SnxCtrVk* v, VkPhysicalDevice phys, uint32_t family) {
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(phys, NULL, &count, NULL) != VK_SUCCESS || !count) return false;
    VkExtensionProperties* list = calloc(count, sizeof *list);
    if (!list) return false;
    vkEnumerateDeviceExtensionProperties(phys, NULL, &count, list);
    const char* extensions[4];
    uint32_t n = 0;
    bool ok = has_extension(list, count, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
    extensions[n++] = VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME;
    if (v->surface) {
        ok = ok && has_extension(list, count, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        extensions[n++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    }
    bool index8 = has_extension(list, count, VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
    if (index8) extensions[n++] = VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME;
    free(list);
    if (!ok) return false;
    VkPhysicalDeviceIndexTypeUint8FeaturesEXT index_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT};
    VkPhysicalDeviceVulkan13Features v13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                                            .pNext = index8 ? &index_features : NULL};
    VkPhysicalDeviceVulkan12Features v12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                            .pNext = &v13};
    VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &v12};
    vkGetPhysicalDeviceFeatures2(phys, &features);
    if (!v12.storageBuffer8BitAccess || !v13.dynamicRendering) return false;
    /* what the 3DS uses of them, nothing more */
    VkPhysicalDeviceIndexTypeUint8FeaturesEXT want_index = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT,
        .indexTypeUint8 = index_features.indexTypeUint8};
    VkPhysicalDeviceVulkan13Features want13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                                               .pNext = index8 ? &want_index : NULL,
                                               .dynamicRendering = VK_TRUE,
                                               .synchronization2 = v13.synchronization2};
    VkPhysicalDeviceVulkan12Features want12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                               .pNext = &want13,
                                               .storageBuffer8BitAccess = VK_TRUE,
                                               .shaderInt8 = v12.shaderInt8,
                                               .uniformAndStorageBuffer8BitAccess =
                                                   v12.uniformAndStorageBuffer8BitAccess};
    VkPhysicalDeviceFeatures2 want = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &want12};
    want.features.logicOp = features.features.logicOp;
    want.features.fragmentStoresAndAtomics = features.features.fragmentStoresAndAtomics;
    want.features.shaderClipDistance = features.features.shaderClipDistance;
    want.features.shaderInt16 = features.features.shaderInt16;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                  .queueFamilyIndex = family,
                                  .queueCount = 1,
                                  .pQueuePriorities = &priority};
    VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                             .pNext = &want,
                             .queueCreateInfoCount = 1,
                             .pQueueCreateInfos = &qi,
                             .enabledExtensionCount = n,
                             .ppEnabledExtensionNames = extensions};
    if (vkCreateDevice(phys, &di, NULL, &v->device) != VK_SUCCESS) return false;
    v->phys = phys;
    v->family = family;
    v->index8 = index8 && index_features.indexTypeUint8;
    v->logic_op = features.features.logicOp;
    v->clip_distance = features.features.shaderClipDistance;
    VkPhysicalDeviceVulkan12Properties p12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};
    VkPhysicalDeviceProperties2 properties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &p12};
    vkGetPhysicalDeviceProperties2(phys, &properties);
    v->preserve_infnan = p12.shaderSignedZeroInfNanPreserveFloat32;
    return true;
}
static bool import_ram(SnxCtrVk* v, SnxCtrMemory* memory, char* error, size_t error_size) {
    const SnxCtrProfile* profile = snx_ctr_memory_profile(memory);
    VkPhysicalDeviceExternalMemoryHostPropertiesEXT host = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2 properties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &host};
    vkGetPhysicalDeviceProperties2(v->phys, &properties);
    for (size_t i = 0; i < profile->region_count; ++i) {
        unsigned char* view = snx_ctr_memory_region(memory, i);
        const SnxCtrRegion* r = &profile->regions[i];
        if (!view) continue;
        uint64_t alignment = host.minImportedHostPointerAlignment ? host.minImportedHostPointerAlignment : 4096;
        if (((uintptr_t)view | r->size) & (alignment - 1)) {
            snprintf(error, error_size, "the %s region is not aligned to the %llu bytes the driver imports", r->name,
                     (unsigned long long)alignment);
            return false;
        }
        VkMemoryHostPointerPropertiesEXT pointer = {.sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
        if (vkGetMemoryHostPointerPropertiesEXT(v->device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, view,
                                                &pointer) != VK_SUCCESS) {
            snprintf(error, error_size, "the driver cannot import the %s region", r->name);
            return false;
        }
        SnxCtrVkRam* ram = &v->ram[v->ram_count];
        VkExternalMemoryBufferCreateInfo external = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
                                                     .handleTypes =
                                                         VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
        VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                 .pNext = &external,
                                 .size = r->size,
                                 .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                          VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                 .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
        if (vkCreateBuffer(v->device, &bi, NULL, &ram->buffer) != VK_SUCCESS) {
            snprintf(error, error_size, "cannot make a buffer of the %s region", r->name);
            return false;
        }
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(v->device, ram->buffer, &req);
        /* host-coherent: the guest's CPU writes before a submit are seen by it, its writes after the fence by the CPU */
        uint32_t type = snx_ctr_vk_memory_type(req.memoryTypeBits & pointer.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkImportMemoryHostPointerInfoEXT import = {.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
                                                   .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
                                                   .pHostPointer = view};
        VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                   .pNext = &import,
                                   .allocationSize = r->size,
                                   .memoryTypeIndex = type};
        if (type == UINT32_MAX || vkAllocateMemory(v->device, &ai, NULL, &ram->memory) != VK_SUCCESS ||
            vkBindBufferMemory(v->device, ram->buffer, ram->memory, 0) != VK_SUCCESS) {
            vkDestroyBuffer(v->device, ram->buffer, NULL);
            snprintf(error, error_size, "the driver cannot import the %s region", r->name);
            return false;
        }
        ram->base = r->base;
        ram->size = r->size;
        ++v->ram_count;
    }
    return true;
}
bool snx_ctr_vk_init(SnxCtrMemory* memory, char* error, size_t error_size) {
    SnxCtrVk* v = &g_device.vk;
    if (g_device.up) return true;
    memset(v, 0, sizeof *v);
    if (volkInitialize() != VK_SUCCESS) {
        snprintf(error, error_size, "no Vulkan loader");
        return false;
    }
    /* a window first: a display server and a Vulkan surface make it the output; without one frames go to the frame
       dumps only */
    if (SDL_InitSubSystem(SDL_INIT_VIDEO))
        v->window = SDL_CreateWindow("ScarletNX 3DS", 600, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                             .pApplicationName = "scarletnx-ctr",
                             .apiVersion = VK_API_VERSION_1_3};
    Uint32 extension_count = 0;
    const char* const* extensions = v->window ? SDL_Vulkan_GetInstanceExtensions(&extension_count) : NULL;
    VkInstanceCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                               .pApplicationInfo = &app,
                               .enabledExtensionCount = v->window ? extension_count : 0,
                               .ppEnabledExtensionNames = v->window ? extensions : NULL};
    if (vkCreateInstance(&ii, NULL, &v->instance) != VK_SUCCESS) {
        if (v->window) SDL_DestroyWindow(v->window);
        v->window = NULL;
        ii.enabledExtensionCount = 0;
        ii.ppEnabledExtensionNames = NULL;
        if (vkCreateInstance(&ii, NULL, &v->instance) != VK_SUCCESS) {
            snprintf(error, error_size, "no Vulkan 1.3 instance");
            return false;
        }
    }
    volkLoadInstance(v->instance);
    if (v->window && !SDL_Vulkan_CreateSurface(v->window, v->instance, NULL, &v->surface)) {
        SDL_DestroyWindow(v->window);
        v->window = NULL;
    }
    uint32_t count = 0;
    VkPhysicalDevice devices[16];
    if (vkEnumeratePhysicalDevices(v->instance, &count, NULL) != VK_SUCCESS || !count) {
        snprintf(error, error_size, "no Vulkan device");
        snx_ctr_vk_free();
        return false;
    }
    if (count > 16) count = 16;
    vkEnumeratePhysicalDevices(v->instance, &count, devices);
    for (uint32_t i = 0; i < count && !v->device; ++i) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(devices[i], &p);
        if (p.apiVersion < VK_API_VERSION_1_3) continue;
        uint32_t families = 0;
        VkQueueFamilyProperties props[32];
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &families, NULL);
        if (families > 32) families = 32;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &families, props);
        for (uint32_t f = 0; f < families && !v->device; ++f) {
            VkBool32 present = VK_TRUE;
            if (v->surface && vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], f, v->surface, &present) != VK_SUCCESS)
                present = VK_FALSE;
            if ((props[f].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) !=
                    (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT) ||
                !present)
                continue;
            make_device(v, devices[i], f);
        }
    }
    if (!v->device) {
        snprintf(error, error_size,
                 "no Vulkan 1.3 device with 8-bit storage buffers, dynamic rendering and VK_EXT_external_memory_host");
        snx_ctr_vk_free();
        return false;
    }
    volkLoadDevice(v->device);
    vkGetDeviceQueue(v->device, v->family, 0, &v->queue);
    vkGetPhysicalDeviceProperties(v->phys, &v->properties);
    vkGetPhysicalDeviceMemoryProperties(v->phys, &v->memory_properties);
    VkCommandPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                  .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                  .queueFamilyIndex = v->family};
    VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    /* a recording's sets: a draw takes two (three uniform blocks, three LUTs and three textures), a compute dispatch
       one; running out finishes the recording early (vk/render.c) */
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8192},
                                    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 * 4096},
                                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4096},
                                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 * 4096},
                                    {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 3 * 4096}};
    VkDescriptorPoolCreateInfo dp = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                     .maxSets = 12288,
                                     .poolSizeCount = sizeof sizes / sizeof *sizes,
                                     .pPoolSizes = sizes};
    VkCommandBufferAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                      .commandBufferCount = 1};
    bool made = vkCreateCommandPool(v->device, &pi, NULL, &v->pool) == VK_SUCCESS;
    ai.commandPool = v->pool;
    for (unsigned i = 0; i < SNX_CTR_VK_SLOTS && made; ++i)
        made = vkCreateFence(v->device, &fi, NULL, &v->slots[i].fence) == VK_SUCCESS &&
               vkCreateDescriptorPool(v->device, &dp, NULL, &v->slots[i].descriptors) == VK_SUCCESS &&
               vkAllocateCommandBuffers(v->device, &ai, &v->slots[i].cb) == VK_SUCCESS;
    if (!made) {
        snprintf(error, error_size, "cannot make the Vulkan device's command pool");
        snx_ctr_vk_free();
        return false;
    }
    g_device.up = 1;
    if (!import_ram(v, memory, error, error_size)) {
        snx_ctr_vk_free();
        return false;
    }
    /* the renderer answers for the pages the GPU keeps (memory/ctr_memory.h) */
    v->memory = memory;
    snx_ctr_track_handler(memory, snx_ctr_vk_track, NULL);
    return true;
}
void snx_ctr_vk_free(void) {
    SnxCtrVk* v = &g_device.vk;
    if (v->device) {
        snx_ctr_vk_render_free();
        vkDeviceWaitIdle(v->device);
        if (v->memory) { /* nothing is the GPU's any more: every page the CPU's, untrapped */
            snx_ctr_track_handler(v->memory, NULL, NULL);
            for (unsigned i = 0; i < v->ram_count; ++i)
                snx_ctr_track_set(v->memory, v->ram[i].base, v->ram[i].size, SNX_CTR_TRACK_NONE);
        }
        for (unsigned i = 0; i < v->ram_count; ++i) {
            vkDestroyBuffer(v->device, v->ram[i].buffer, NULL);
            vkFreeMemory(v->device, v->ram[i].memory, NULL);
        }
        for (unsigned i = 0; i < SNX_CTR_VK_SLOTS; ++i) {
            if (v->slots[i].descriptors) vkDestroyDescriptorPool(v->device, v->slots[i].descriptors, NULL);
            if (v->slots[i].fence) vkDestroyFence(v->device, v->slots[i].fence, NULL);
        }
        if (v->pool) vkDestroyCommandPool(v->device, v->pool, NULL);
        vkDestroyDevice(v->device, NULL);
    }
    if (v->surface) vkDestroySurfaceKHR(v->instance, v->surface, NULL);
    if (v->instance) vkDestroyInstance(v->instance, NULL);
    if (v->window) SDL_DestroyWindow(v->window);
    memset(v, 0, sizeof *v);
    g_device.up = 0;
}
/* a submission done: its slot free again, and what the renderer kept for it */
static void retire(SnxCtrVk* v, unsigned i) {
    vkResetFences(v->device, 1, &v->slots[i].fence);
    vkResetCommandBuffer(v->slots[i].cb, 0);
    vkResetDescriptorPool(v->device, v->slots[i].descriptors, 0);
    if (v->slots[i].serial > v->completed) v->completed = v->slots[i].serial;
    snx_ctr_vk_render_retire(i, v->slots[i].serial);
    v->slots[i].serial = 0;
}
/* the slots in submission order, the oldest first: the next slot to record is the one submitted longest ago (the
   open recording's slot is not in flight, and is skipped) */
static unsigned oldest(const SnxCtrVk* v, unsigned n) { return (v->slot + n) % SNX_CTR_VK_SLOTS; }
uint64_t snx_ctr_vk_poll(void) {
    SnxCtrVk* v = &g_device.vk;
    if (!g_device.up) return 0;
    for (unsigned n = 0; n < SNX_CTR_VK_SLOTS; ++n) {
        unsigned i = oldest(v, n);
        if (!v->slots[i].serial || (v->recording && i == v->slot)) continue;
        if (vkGetFenceStatus(v->device, v->slots[i].fence) != VK_SUCCESS) break; /* in order: the later ones too */
        retire(v, i);
    }
    return v->completed;
}
bool snx_ctr_vk_wait(uint64_t serial, uint64_t timeout_ns) {
    SnxCtrVk* v = &g_device.vk;
    if (!g_device.up || serial <= v->completed) return true;
    if (serial > v->submitted) snx_ctr_vk_submit();
    for (unsigned n = 0; n < SNX_CTR_VK_SLOTS && v->completed < serial; ++n) {
        unsigned i = oldest(v, n);
        if (!v->slots[i].serial || v->slots[i].serial <= v->completed || (v->recording && i == v->slot)) continue;
        VkResult r = vkWaitForFences(v->device, 1, &v->slots[i].fence, VK_TRUE, timeout_ns);
        if (r != VK_SUCCESS) return false; /* the timeout, or a lost device */
        retire(v, i);
    }
    return v->completed >= serial;
}
/* the open slot free for a recording: its last submission waited for when it is still in flight */
static void ready(SnxCtrVk* v) {
    if (!v->recording && v->slots[v->slot].serial) (void)snx_ctr_vk_wait(v->slots[v->slot].serial, UINT64_MAX);
}
VkCommandBuffer snx_ctr_vk_commands(void) {
    SnxCtrVk* v = &g_device.vk;
    if (!v->recording) {
        ready(v);
        VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                       .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        vkBeginCommandBuffer(v->slots[v->slot].cb, &bi);
        v->recording = true;
        snx_ctr_vk_render_begin(v->slot);
    }
    return v->slots[v->slot].cb;
}
uint64_t snx_ctr_vk_serial(void) { return g_device.vk.submitted + 1; }
unsigned snx_ctr_vk_slot(void) { return g_device.vk.slot; }
uint64_t snx_ctr_vk_submit(void) { return snx_ctr_vk_submit_with(VK_NULL_HANDLE, VK_NULL_HANDLE); }
uint64_t snx_ctr_vk_submit_with(VkSemaphore wait, VkSemaphore signal) {
    SnxCtrVk* v = &g_device.vk;
    if (!g_device.up) return 0;
    if (!v->recording && !wait && !signal) return v->submitted;
    VkCommandBuffer cb = snx_ctr_vk_commands();
    snx_ctr_vk_render_close(); /* a rendering pass ends with its command buffer */
    /* everything written is the host's to read once the fence is signaled */
    VkMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                            .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                            .dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, NULL, 0,
                         NULL);
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                       .waitSemaphoreCount = wait ? 1 : 0,
                       .pWaitSemaphores = &wait,
                       .pWaitDstStageMask = &stage,
                       .commandBufferCount = 1,
                       .pCommandBuffers = &cb,
                       .signalSemaphoreCount = signal ? 1 : 0,
                       .pSignalSemaphores = &signal};
    v->recording = false;
    if (vkEndCommandBuffer(cb) != VK_SUCCESS || vkQueueSubmit(v->queue, 1, &si, v->slots[v->slot].fence) != VK_SUCCESS) {
        /* a lost device: nothing more runs, and the slot is free as if it had */
        vkResetCommandBuffer(cb, 0);
        return v->submitted;
    }
    v->slots[v->slot].serial = ++v->submitted;
    v->slot = (v->slot + 1) % SNX_CTR_VK_SLOTS;
    return v->submitted;
}
bool snx_ctr_vk_finish(void) {
    if (!g_device.up) return true;
    return snx_ctr_vk_wait(snx_ctr_vk_submit(), UINT64_MAX);
}
VkDescriptorSet snx_ctr_vk_set(VkDescriptorSetLayout layout) {
    SnxCtrVk* v = &g_device.vk;
    snx_ctr_vk_commands(); /* the open recording's slot, free */
    VkDescriptorSetAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                      .descriptorPool = v->slots[v->slot].descriptors,
                                      .descriptorSetCount = 1,
                                      .pSetLayouts = &layout};
    VkDescriptorSet set = VK_NULL_HANDLE;
    return vkAllocateDescriptorSets(v->device, &ai, &set) == VK_SUCCESS ? set : VK_NULL_HANDLE;
}
const SnxCtrVkRam* snx_ctr_vk_ram(uint32_t pa, uint32_t size, VkDeviceSize* offset) {
    const SnxCtrVk* v = &g_device.vk;
    for (unsigned i = 0; i < v->ram_count; ++i) {
        const SnxCtrVkRam* r = &v->ram[i];
        if (pa >= r->base && (uint64_t)pa - r->base + size <= r->size) {
            *offset = pa - r->base;
            return r;
        }
    }
    return NULL;
}
bool snx_ctr_vk_compute(const uint32_t* words, size_t count, unsigned buffers, unsigned images, uint32_t push,
                        SnxCtrVkCompute* out) {
    SnxCtrVk* v = &g_device.vk;
    VkDescriptorSetLayoutBinding bindings[16];
    memset(out, 0, sizeof *out);
    if (buffers + images > 16) return false;
    for (unsigned i = 0; i < buffers + images; ++i)
        bindings[i] = (VkDescriptorSetLayoutBinding){
            i, i < buffers ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
            VK_SHADER_STAGE_COMPUTE_BIT, NULL};
    VkDescriptorSetLayoutCreateInfo si = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                          .bindingCount = buffers + images,
                                          .pBindings = bindings};
    VkPushConstantRange range = {VK_SHADER_STAGE_COMPUTE_BIT, 0, push};
    VkPipelineLayoutCreateInfo li = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                     .setLayoutCount = 1,
                                     .pushConstantRangeCount = push ? 1 : 0,
                                     .pPushConstantRanges = &range};
    VkShaderModuleCreateInfo mi = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                   .codeSize = count * 4,
                                   .pCode = words};
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(v->device, &si, NULL, &out->set) != VK_SUCCESS) return false;
    li.pSetLayouts = &out->set;
    if (vkCreatePipelineLayout(v->device, &li, NULL, &out->layout) != VK_SUCCESS ||
        vkCreateShaderModule(v->device, &mi, NULL, &module) != VK_SUCCESS) {
        snx_ctr_vk_compute_free(out);
        return false;
    }
    VkComputePipelineCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                                                .module = module,
                                                .pName = "main"},
                                      .layout = out->layout};
    bool ok = vkCreateComputePipelines(v->device, VK_NULL_HANDLE, 1, &ci, NULL, &out->pipeline) == VK_SUCCESS;
    vkDestroyShaderModule(v->device, module, NULL);
    if (!ok) snx_ctr_vk_compute_free(out);
    return ok;
}
void snx_ctr_vk_compute_free(SnxCtrVkCompute* c) {
    SnxCtrVk* v = &g_device.vk;
    if (!v->device) return;
    if (c->pipeline) vkDestroyPipeline(v->device, c->pipeline, NULL);
    if (c->layout) vkDestroyPipelineLayout(v->device, c->layout, NULL);
    if (c->set) vkDestroyDescriptorSetLayout(v->device, c->set, NULL);
    memset(c, 0, sizeof *c);
}
void snx_ctr_vk_barrier(VkCommandBuffer cb) {
    VkMemoryBarrier b = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                         .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                         .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &b, 0, NULL,
                         0, NULL);
}
