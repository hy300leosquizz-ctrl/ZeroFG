// Copyright 2026 ZeroFG authors. SPDX-License-Identifier: Apache-2.0

#include <cstring>

#include "xenia/base/logging.h"
#include "xenia/ui/vulkan/vulkan_device.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace ui {
namespace vulkan {

std::unique_ptr<VulkanInstance> VulkanInstance::Wrap(
    VkInstance instance, PFN_vkGetInstanceProcAddr get_instance_proc_addr,
    uint32_t api_version, bool debug_utils_enabled) {
  if (instance == VK_NULL_HANDLE || !get_instance_proc_addr) {
    return nullptr;
  }
  std::unique_ptr<VulkanInstance> wrapped(new VulkanInstance());
  wrapped->instance_ = instance;
  wrapped->api_version_ = api_version;
  Functions& f = wrapped->functions_;
  f.vkGetInstanceProcAddr = get_instance_proc_addr;
#define ZEROFG_PORTABLE_INSTANCE_FUNCTION(name) \
  f.name = PFN_##name(get_instance_proc_addr(instance, #name))
  ZEROFG_PORTABLE_INSTANCE_FUNCTION(vkGetPhysicalDeviceFormatProperties);
  ZEROFG_PORTABLE_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceSupportKHR);
  ZEROFG_PORTABLE_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
  ZEROFG_PORTABLE_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceFormatsKHR);
  if (debug_utils_enabled) {
    ZEROFG_PORTABLE_INSTANCE_FUNCTION(vkSetDebugUtilsObjectNameEXT);
  }
#undef ZEROFG_PORTABLE_INSTANCE_FUNCTION
  wrapped->extensions_.ext_EXT_debug_utils =
      debug_utils_enabled && f.vkSetDebugUtilsObjectNameEXT;
  if (!f.vkGetPhysicalDeviceFormatProperties ||
      !f.vkGetPhysicalDeviceSurfaceSupportKHR ||
      !f.vkGetPhysicalDeviceSurfaceCapabilitiesKHR ||
      !f.vkGetPhysicalDeviceSurfaceFormatsKHR) {
    XELOGE("ZeroFG host: the instance lacks a surface entry point");
    return nullptr;
  }
  return wrapped;
}

std::unique_ptr<VulkanDevice> VulkanDevice::Wrap(
    const Description& description) {
  const VulkanInstance* instance = description.instance;
  if (!instance || description.physical_device == VK_NULL_HANDLE ||
      description.device == VK_NULL_HANDLE ||
      description.queue_family == UINT32_MAX || description.queues.empty()) {
    return nullptr;
  }
  const auto get_device_proc_addr = PFN_vkGetDeviceProcAddr(
      instance->functions().vkGetInstanceProcAddr(instance->instance(),
                                                  "vkGetDeviceProcAddr"));
  if (!get_device_proc_addr) {
    return nullptr;
  }

  std::unique_ptr<VulkanDevice> wrapped(new VulkanDevice());
  wrapped->instance_ = instance;
  wrapped->physical_device_ = description.physical_device;
  wrapped->device_ = description.device;
  wrapped->queue_family_ = description.queue_family;
  for (VkQueue queue : description.queues) {
    wrapped->queues_.push_back(std::make_unique<Queue>(queue));
  }
  wrapped->properties_ = description.properties;
  wrapped->extensions_ = description.extensions;

  // Everything is required except display timing, which the egress reads only
  // when the extension is enabled.
  Functions& f = wrapped->functions_;
  const char* missing = nullptr;
#define ZEROFG_PORTABLE_DEVICE_FUNCTION(name)                           \
  f.name = PFN_##name(get_device_proc_addr(description.device, #name)); \
  if (!f.name && !missing) missing = #name;
  // An optional function promoted to core (synchronization2, timelines) is
  // only under its KHR name on an older device that enabled the extension.
#define ZEROFG_PORTABLE_DEVICE_FUNCTION_OPTIONAL(name)                      \
  f.name = PFN_##name(get_device_proc_addr(description.device, #name));     \
  if (!f.name)                                                              \
    f.name = PFN_##name(get_device_proc_addr(description.device, #name "KHR"));
#include "xenia/ui/vulkan/zerofg_portable_device_functions.inc"
#undef ZEROFG_PORTABLE_DEVICE_FUNCTION_OPTIONAL
#undef ZEROFG_PORTABLE_DEVICE_FUNCTION
  if (missing) {
    XELOGE("ZeroFG host: device B lacks {}", missing);
    return nullptr;
  }

  wrapped->get_physical_device_image_format_properties2_ =
      PFN_vkGetPhysicalDeviceImageFormatProperties2(
          instance->functions().vkGetInstanceProcAddr(
              instance->instance(),
              "vkGetPhysicalDeviceImageFormatProperties2"));

  VkPhysicalDeviceMemoryProperties memory_properties = {};
  const auto get_memory_properties = PFN_vkGetPhysicalDeviceMemoryProperties(
      instance->functions().vkGetInstanceProcAddr(
          instance->instance(), "vkGetPhysicalDeviceMemoryProperties"));
  if (!get_memory_properties) {
    return nullptr;
  }
  get_memory_properties(description.physical_device, &memory_properties);
  for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
    const uint32_t bit = uint32_t(1) << i;
    const VkMemoryPropertyFlags flags =
        memory_properties.memoryTypes[i].propertyFlags;
    if (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
      wrapped->memory_types_.device_local |= bit;
    }
    if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
      wrapped->memory_types_.host_visible |= bit;
    }
    if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
      wrapped->memory_types_.host_coherent |= bit;
    }
    if (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
      wrapped->memory_types_.host_cached |= bit;
    }
  }
  return wrapped;
}

VulkanDevice::Queue::Acquisition VulkanDevice::AcquireQueue(
    uint32_t queue_family_index, uint32_t queue_index) const {
  (void)queue_family_index;
  return queues_[queue_index]->Acquire();
}

std::optional<VulkanDevice::Queue::Acquisition> VulkanDevice::TryAcquireQueue(
    uint32_t queue_family_index, uint32_t queue_index) const {
  (void)queue_family_index;
  return queues_[queue_index]->TryAcquire();
}

bool VulkanDevice::DrainZeroFGForTeardown() {
  if (zerofg_teardown_idle()) {
    return true;
  }
  // Every queue of B, then one device-wide attempt if a queue reported an
  // error: a queue-idle error alone does not prove retirement. A lost device
  // is terminal, so nothing remains to retire.
  VkResult result = VK_SUCCESS;
  for (uint32_t i = 0; i < uint32_t(queues_.size()); ++i) {
    Queue::Acquisition queue = AcquireQueue(queue_family_, i);
    const VkResult queue_result = functions_.vkQueueWaitIdle(queue.queue());
    if (queue_result != VK_SUCCESS && result == VK_SUCCESS) {
      result = queue_result;
    }
  }
  bool retired = result == VK_SUCCESS;
  if (result == VK_ERROR_DEVICE_LOST) {
    SetLost();
    retired = true;
  } else if (!retired) {
    const VkResult device_result = functions_.vkDeviceWaitIdle(device_);
    XELOGW("ZeroFGDeviceB teardown_queue_idle fallback_device_idle result={}",
           int32_t(device_result));
    retired = device_result == VK_SUCCESS;
    if (device_result == VK_ERROR_DEVICE_LOST) {
      SetLost();
      retired = true;
    }
  }
  zerofg_teardown_idle_.store(retired, std::memory_order_release);
  XELOGI("ZeroFGDeviceB teardown_queue_idle result={} retired={}",
         int32_t(result), retired);
  return retired;
}

namespace util {

namespace {

// Device-local first, any supported type otherwise; host-visible for upload,
// and host-visible preferring cached for readback. UINT32_MAX when none fits.
uint32_t ChooseMemoryType(const VulkanDevice::MemoryTypes& types,
                          uint32_t supported, MemoryPurpose purpose) {
  uint32_t candidates = supported;
  if (purpose == MemoryPurpose::kDeviceLocal) {
    if (candidates & types.device_local) {
      candidates &= types.device_local;
    }
  } else {
    candidates &= types.host_visible;
    if (purpose == MemoryPurpose::kReadback &&
        (candidates & types.host_cached)) {
      candidates &= types.host_cached;
    }
  }
  for (uint32_t i = 0; i < 32; ++i) {
    if (candidates & (uint32_t(1) << i)) {
      return i;
    }
  }
  return UINT32_MAX;
}

// A dedicated allocation for the image or the buffer of `dedicated`, bound to
// it. False leaves nothing allocated.
bool AllocateDedicated(const VulkanDevice* vulkan_device,
                       const VkMemoryRequirements& requirements,
                       MemoryPurpose purpose,
                       const VkMemoryDedicatedAllocateInfo& dedicated,
                       VkDeviceMemory& memory_out, uint32_t& memory_type_out) {
  const uint32_t memory_type = ChooseMemoryType(
      vulkan_device->memory_types(), requirements.memoryTypeBits, purpose);
  if (memory_type == UINT32_MAX) {
    return false;
  }
  VkMemoryAllocateInfo allocate = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                   &dedicated};
  allocate.allocationSize = requirements.size;
  allocate.memoryTypeIndex = memory_type;
  if (vulkan_device->functions().vkAllocateMemory(
          vulkan_device->device(), &allocate, nullptr, &memory_out) !=
      VK_SUCCESS) {
    return false;
  }
  memory_type_out = memory_type;
  return true;
}

void SyncMappedMemory(const VulkanDevice* vulkan_device, VkDeviceMemory memory,
                      uint32_t memory_type, bool flush) {
  if (memory_type < 32 && (vulkan_device->memory_types().host_coherent &
                           (uint32_t(1) << memory_type))) {
    return;
  }
  VkMappedMemoryRange range = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
  range.memory = memory;
  range.offset = 0;
  range.size = VK_WHOLE_SIZE;
  const VulkanDevice::Functions& dfn = vulkan_device->functions();
  if (flush) {
    dfn.vkFlushMappedMemoryRanges(vulkan_device->device(), 1, &range);
  } else {
    dfn.vkInvalidateMappedMemoryRanges(vulkan_device->device(), 1, &range);
  }
}

}  // namespace

bool CreateDedicatedAllocationBuffer(const VulkanDevice* vulkan_device,
                                     VkDeviceSize size,
                                     VkBufferUsageFlags usage,
                                     MemoryPurpose memory_purpose,
                                     VkBuffer& buffer_out,
                                     VkDeviceMemory& memory_out,
                                     uint32_t* memory_type_out,
                                     VkDeviceSize* memory_size_out) {
  const VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  VkBufferCreateInfo create_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  create_info.size = size;
  create_info.usage = usage;
  create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VkBuffer buffer = VK_NULL_HANDLE;
  if (dfn.vkCreateBuffer(device, &create_info, nullptr, &buffer) !=
      VK_SUCCESS) {
    return false;
  }
  VkMemoryRequirements requirements;
  dfn.vkGetBufferMemoryRequirements(device, buffer, &requirements);
  VkMemoryDedicatedAllocateInfo dedicated = {
      VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
  dedicated.buffer = buffer;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  uint32_t memory_type = UINT32_MAX;
  if (!AllocateDedicated(vulkan_device, requirements, memory_purpose,
                         dedicated, memory, memory_type)) {
    dfn.vkDestroyBuffer(device, buffer, nullptr);
    return false;
  }
  if (dfn.vkBindBufferMemory(device, buffer, memory, 0) != VK_SUCCESS) {
    dfn.vkDestroyBuffer(device, buffer, nullptr);
    dfn.vkFreeMemory(device, memory, nullptr);
    return false;
  }
  buffer_out = buffer;
  memory_out = memory;
  if (memory_type_out) {
    *memory_type_out = memory_type;
  }
  if (memory_size_out) {
    *memory_size_out = requirements.size;
  }
  return true;
}

bool CreateDedicatedAllocationImage(const VulkanDevice* vulkan_device,
                                    const VkImageCreateInfo& create_info,
                                    MemoryPurpose memory_purpose,
                                    VkImage& image_out,
                                    VkDeviceMemory& memory_out,
                                    uint32_t* memory_type_out,
                                    VkDeviceSize* memory_size_out) {
  const VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  VkImage image = VK_NULL_HANDLE;
  if (dfn.vkCreateImage(device, &create_info, nullptr, &image) != VK_SUCCESS) {
    return false;
  }
  VkMemoryRequirements requirements;
  dfn.vkGetImageMemoryRequirements(device, image, &requirements);
  VkMemoryDedicatedAllocateInfo dedicated = {
      VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
  dedicated.image = image;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  uint32_t memory_type = UINT32_MAX;
  if (!AllocateDedicated(vulkan_device, requirements, memory_purpose,
                         dedicated, memory, memory_type)) {
    dfn.vkDestroyImage(device, image, nullptr);
    return false;
  }
  if (dfn.vkBindImageMemory(device, image, memory, 0) != VK_SUCCESS) {
    dfn.vkDestroyImage(device, image, nullptr);
    dfn.vkFreeMemory(device, memory, nullptr);
    return false;
  }
  image_out = image;
  memory_out = memory;
  if (memory_type_out) {
    *memory_type_out = memory_type;
  }
  if (memory_size_out) {
    *memory_size_out = requirements.size;
  }
  return true;
}

void FlushMappedMemoryRange(const VulkanDevice* vulkan_device,
                            VkDeviceMemory memory, uint32_t memory_type,
                            VkDeviceSize, VkDeviceSize, VkDeviceSize) {
  SyncMappedMemory(vulkan_device, memory, memory_type, true);
}

void InvalidateMappedMemoryRange(const VulkanDevice* vulkan_device,
                                 VkDeviceMemory memory, uint32_t memory_type,
                                 VkDeviceSize, VkDeviceSize, VkDeviceSize) {
  SyncMappedMemory(vulkan_device, memory, memory_type, false);
}

}  // namespace util

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
