// Copyright 2026 ZeroFG authors. SPDX-License-Identifier: Apache-2.0
//
// The portable host kit's stand-in for the parts of Xenia's VulkanPresenter
// the ZeroFG presenter reads: the convention for the game frame the host hands
// over (its format, and the layout, access and stage it was last read in).
#ifndef ZEROFG_PORTABLE_XENIA_UI_VULKAN_VULKAN_PRESENTER_H_
#define ZEROFG_PORTABLE_XENIA_UI_VULKAN_VULKAN_PRESENTER_H_

#include <vulkan/vulkan.h>

namespace xe {
namespace ui {
namespace vulkan {

class VulkanPresenter {
 public:
  static constexpr VkFormat kGuestOutputFormat =
      VK_FORMAT_A2B10G10R10_UNORM_PACK32;
  static constexpr VkPipelineStageFlagBits kGuestOutputInternalStageMask =
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  static constexpr VkAccessFlags kGuestOutputInternalAccessMask =
      VK_ACCESS_SHADER_READ_BIT;
  static constexpr VkImageLayout kGuestOutputInternalLayout =
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // ZEROFG_PORTABLE_XENIA_UI_VULKAN_VULKAN_PRESENTER_H_
