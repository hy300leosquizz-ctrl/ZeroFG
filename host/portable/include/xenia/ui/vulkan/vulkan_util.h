// Copyright 2026 ZeroFG authors. SPDX-License-Identifier: Apache-2.0
//
// The portable host kit's stand-in for the Vulkan helpers the presenter uses.
#ifndef ZEROFG_PORTABLE_XENIA_UI_VULKAN_VULKAN_UTIL_H_
#define ZEROFG_PORTABLE_XENIA_UI_VULKAN_VULKAN_UTIL_H_

#include <cstdint>

#include "xenia/ui/vulkan/vulkan_device.h"

namespace xe {
namespace ui {
namespace vulkan {
namespace util {

enum class MemoryPurpose {
  kDeviceLocal,
  kUpload,
  kReadback,
};

inline VkImageSubresourceRange InitializeSubresourceRange(
    VkImageAspectFlags aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
    uint32_t base_mip_level = 0, uint32_t level_count = VK_REMAINING_MIP_LEVELS,
    uint32_t base_array_layer = 0,
    uint32_t layer_count = VK_REMAINING_ARRAY_LAYERS) {
  VkImageSubresourceRange range;
  range.aspectMask = aspect_mask;
  range.baseMipLevel = base_mip_level;
  range.levelCount = level_count;
  range.baseArrayLayer = base_array_layer;
  range.layerCount = layer_count;
  return range;
}

// The largest 2D image the device can also render into.
inline VkExtent2D GetMax2DFramebufferExtent(
    const VulkanDevice::Properties& properties) {
  VkExtent2D extent;
  extent.width = properties.maxFramebufferWidth < properties.maxImageDimension2D
                     ? properties.maxFramebufferWidth
                     : properties.maxImageDimension2D;
  extent.height =
      properties.maxFramebufferHeight < properties.maxImageDimension2D
          ? properties.maxFramebufferHeight
          : properties.maxImageDimension2D;
  return extent;
}

// A buffer with its own (dedicated) memory allocation.
bool CreateDedicatedAllocationBuffer(const VulkanDevice* vulkan_device,
                                     VkDeviceSize size,
                                     VkBufferUsageFlags usage,
                                     MemoryPurpose memory_purpose,
                                     VkBuffer& buffer_out,
                                     VkDeviceMemory& memory_out,
                                     uint32_t* memory_type_out = nullptr,
                                     VkDeviceSize* memory_size_out = nullptr);

// Makes CPU writes to mapped memory visible to the device, and device writes
// visible to the CPU. No-ops on host-coherent memory. The kit always flushes
// and invalidates the whole allocation, which is valid at any size.
void FlushMappedMemoryRange(const VulkanDevice* vulkan_device,
                            VkDeviceMemory memory, uint32_t memory_type,
                            VkDeviceSize offset = 0,
                            VkDeviceSize memory_size = VK_WHOLE_SIZE,
                            VkDeviceSize size = VK_WHOLE_SIZE);
void InvalidateMappedMemoryRange(const VulkanDevice* vulkan_device,
                                 VkDeviceMemory memory, uint32_t memory_type,
                                 VkDeviceSize offset = 0,
                                 VkDeviceSize memory_size = VK_WHOLE_SIZE,
                                 VkDeviceSize size = VK_WHOLE_SIZE);

// An image with its own (dedicated) memory allocation.
bool CreateDedicatedAllocationImage(const VulkanDevice* vulkan_device,
                                    const VkImageCreateInfo& create_info,
                                    MemoryPurpose memory_purpose,
                                    VkImage& image_out,
                                    VkDeviceMemory& memory_out,
                                    uint32_t* memory_type_out = nullptr,
                                    VkDeviceSize* memory_size_out = nullptr);

}  // namespace util
}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // ZEROFG_PORTABLE_XENIA_UI_VULKAN_VULKAN_UTIL_H_
