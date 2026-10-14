// Copyright 2026 ZeroFG authors. SPDX-License-Identifier: Apache-2.0
//
// The portable host kit's stand-in for Xenia's VulkanInstance and VulkanDevice:
// thin wrappers around an instance and a device the HOST created (ZeroFG's own
// device B), exposing exactly the members the presenter uses. They never create
// or destroy the Vulkan objects they wrap.
#ifndef ZEROFG_PORTABLE_XENIA_UI_VULKAN_VULKAN_DEVICE_H_
#define ZEROFG_PORTABLE_XENIA_UI_VULKAN_VULKAN_DEVICE_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

// The Android types below (AHardwareBuffer queries) must be declared the same
// way in every source that includes this header. A host that includes
// vulkan.h first must define VK_USE_PLATFORM_ANDROID_KHR itself, or this
// header fails to compile rather than silently changing the struct layout.
#if defined(__ANDROID__) && !defined(VK_USE_PLATFORM_ANDROID_KHR)
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.h>

namespace xe {
namespace ui {
namespace vulkan {

class VulkanInstance {
 public:
  struct Functions {
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
    PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties =
        nullptr;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR
        vkGetPhysicalDeviceSurfaceSupportKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR
        vkGetPhysicalDeviceSurfaceFormatsKHR = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT = nullptr;
  };
  struct Extensions {
    bool ext_EXT_debug_utils = false;
  };

  // Wraps a host instance. get_instance_proc_addr is the host's loader entry,
  // the one the instance was created through.
  static std::unique_ptr<VulkanInstance> Wrap(
      VkInstance instance, PFN_vkGetInstanceProcAddr get_instance_proc_addr,
      uint32_t api_version, bool debug_utils_enabled);

  VkInstance instance() const { return instance_; }
  uint32_t api_version() const { return api_version_; }
  const Functions& functions() const { return functions_; }
  const Extensions& extensions() const { return extensions_; }

 private:
  VulkanInstance() = default;
  VkInstance instance_ = VK_NULL_HANDLE;
  uint32_t api_version_ = VK_API_VERSION_1_0;
  Functions functions_;
  Extensions extensions_;
};

class VulkanDevice {
 public:
  // What the host enabled on the device, which is all the presenter may use.
  struct Properties {
    bool synchronization2 = false;
    bool timelineSemaphore = false;
    // Limits, from VkPhysicalDeviceLimits.
    uint32_t maxImageDimension2D = 0;
    uint32_t maxFramebufferWidth = 0;
    uint32_t maxFramebufferHeight = 0;
  };
  struct Extensions {
    bool ext_1_1_KHR_dedicated_allocation = true;  // core since 1.1
    bool ext_KHR_swapchain = false;
    bool ext_ANDROID_external_memory_android_hardware_buffer = false;
    bool ext_KHR_external_semaphore_fd = false;
    bool ext_GOOGLE_display_timing = false;
  };
  struct MemoryTypes {
    uint32_t device_local = 0;
    uint32_t host_visible = 0;
    uint32_t host_coherent = 0;
    uint32_t host_cached = 0;
  };
  struct Functions {
#define ZEROFG_PORTABLE_DEVICE_FUNCTION(name) PFN_##name name = nullptr;
#include "xenia/ui/vulkan/zerofg_portable_device_functions.inc"
#undef ZEROFG_PORTABLE_DEVICE_FUNCTION
  };

  // Host access to queues must be externally synchronized in Vulkan: every
  // presenter submission and present takes the queue's lock, and a host that
  // also submits to one of these queues must take it too (AcquireQueue).
  struct Queue {
    std::recursive_mutex mutex;
    VkQueue queue = VK_NULL_HANDLE;

    explicit Queue(VkQueue queue) : queue(queue) {}

    class Acquisition {
     public:
      explicit Acquisition(Queue& queue)
          : lock_(queue.mutex), queue_(queue.queue) {}
      Acquisition(Queue& queue, std::defer_lock_t)
          : lock_(queue.mutex, std::defer_lock), queue_(queue.queue) {}

      VkQueue queue() const { return queue_; }
      bool TryLock() { return lock_.try_lock(); }

     private:
      std::unique_lock<std::recursive_mutex> lock_;
      VkQueue queue_;
    };

    Acquisition Acquire() { return Acquisition(*this); }
    std::optional<Acquisition> TryAcquire() {
      Acquisition acquisition(*this, std::defer_lock);
      if (!acquisition.TryLock()) {
        return std::nullopt;
      }
      return std::optional<Acquisition>(std::move(acquisition));
    }
  };

  struct Description {
    const VulkanInstance* instance = nullptr;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    // The graphics+compute family the presenter works on, and its queues in
    // index order (one or two: B:0 for production, B:1 for the egress when
    // there is a second).
    uint32_t queue_family = UINT32_MAX;
    std::vector<VkQueue> queues;
    Properties properties;
    Extensions extensions;
  };

  // Wraps a host device, resolving every function through vkGetDeviceProcAddr.
  // Null when a required entry point is missing.
  static std::unique_ptr<VulkanDevice> Wrap(const Description& description);

  VulkanDevice(const VulkanDevice&) = delete;
  VulkanDevice& operator=(const VulkanDevice&) = delete;

  const VulkanInstance* vulkan_instance() const { return instance_; }
  VkPhysicalDevice physical_device() const { return physical_device_; }
  VkDevice device() const { return device_; }
  const Properties& properties() const { return properties_; }
  const Extensions& extensions() const { return extensions_; }
  const MemoryTypes& memory_types() const { return memory_types_; }
  const Functions& functions() const { return functions_; }

  PFN_vkCmdPipelineBarrier2 vkCmdPipelineBarrier2() const {
    return functions_.vkCmdPipelineBarrier2;
  }
  PFN_vkDeviceWaitIdle vkDeviceWaitIdle() const {
    return functions_.vkDeviceWaitIdle;
  }
  // Instance level, resolved through the device's instance.
  PFN_vkGetPhysicalDeviceImageFormatProperties2
  vkGetPhysicalDeviceImageFormatProperties2() const {
    return get_physical_device_image_format_properties2_;
  }
  PFN_vkGetSemaphoreCounterValue vkGetSemaphoreCounterValue() const {
    return functions_.vkGetSemaphoreCounterValue;
  }
  PFN_vkWaitSemaphores vkWaitSemaphores() const {
    return functions_.vkWaitSemaphores;
  }
  PFN_vkGetSemaphoreFdKHR vkGetSemaphoreFdKHR() const {
    return functions_.vkGetSemaphoreFdKHR;
  }
  PFN_vkImportSemaphoreFdKHR vkImportSemaphoreFdKHR() const {
    return functions_.vkImportSemaphoreFdKHR;
  }
  PFN_vkGetRefreshCycleDurationGOOGLE vkGetRefreshCycleDurationGOOGLE() const {
    return functions_.vkGetRefreshCycleDurationGOOGLE;
  }
  PFN_vkGetPastPresentationTimingGOOGLE vkGetPastPresentationTimingGOOGLE()
      const {
    return functions_.vkGetPastPresentationTimingGOOGLE;
  }
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
  PFN_vkGetAndroidHardwareBufferPropertiesANDROID
  vkGetAndroidHardwareBufferPropertiesANDROID() const {
    return functions_.vkGetAndroidHardwareBufferPropertiesANDROID;
  }
#endif

  template <typename Object>
  void SetObjectName(VkObjectType object_type, Object object_handle,
                     const char* object_name) const {
    if (!instance_->extensions().ext_EXT_debug_utils ||
        !instance_->functions().vkSetDebugUtilsObjectNameEXT) {
      return;
    }
    VkDebugUtilsObjectNameInfoEXT info = {
        VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType = object_type;
    info.objectHandle = (uint64_t)object_handle;
    info.pObjectName = object_name;
    instance_->functions().vkSetDebugUtilsObjectNameEXT(device_, &info);
  }

  uint32_t queue_family_graphics_compute() const { return queue_family_; }
  // Generation, Post and transfers stay on B:0; B:1, when there is one,
  // belongs to the main-surface egress.
  uint32_t queue_index_zerofg_presenter() const { return 0u; }
  uint32_t queue_index_zerofg_main_surface_present() const {
    return queues_.size() > 1 ? 1u : 0u;
  }
  // Every device this kit wraps is a presenter device B.
  bool is_zerofg_presenter_device() const { return true; }

  Queue::Acquisition AcquireQueue(uint32_t queue_family_index,
                                  uint32_t queue_index) const;
  std::optional<Queue::Acquisition> TryAcquireQueue(
      uint32_t queue_family_index, uint32_t queue_index) const;

  // GPU busy share, for anything that must confirm saturation. The kit has no
  // portable reading of it.
  std::optional<uint32_t> QueryGpuBusyPermille() const { return std::nullopt; }

  // Teardown: once the host has made device B idle for teardown, no presenter
  // submission may follow until a new surface begins.
  bool DrainZeroFGForTeardown();
  bool zerofg_teardown_idle() const {
    return zerofg_teardown_idle_.load(std::memory_order_acquire);
  }
  void BeginZeroFGSurface() {
    zerofg_teardown_idle_.store(false, std::memory_order_release);
  }
  bool RejectZeroFGSubmitAfterTeardownIdle() const {
    return zerofg_teardown_idle();
  }

  // Returns whether a device loss is observed for the first time.
  bool SetLost() noexcept {
    return !lost_.exchange(true, std::memory_order_acq_rel);
  }
  bool IsLost() const noexcept { return lost_.load(std::memory_order_acquire); }
  VkResult SubmitAndUpdateLost(VkQueue queue, uint32_t submit_count,
                               const VkSubmitInfo* submits, VkFence fence) {
    const VkResult result =
        functions_.vkQueueSubmit(queue, submit_count, submits, fence);
    if (result == VK_ERROR_DEVICE_LOST) {
      SetLost();
    }
    return result;
  }

 private:
  VulkanDevice() = default;

  const VulkanInstance* instance_ = nullptr;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  uint32_t queue_family_ = UINT32_MAX;
  std::vector<std::unique_ptr<Queue>> queues_;
  Properties properties_;
  Extensions extensions_;
  MemoryTypes memory_types_;
  Functions functions_;
  PFN_vkGetPhysicalDeviceImageFormatProperties2
      get_physical_device_image_format_properties2_ = nullptr;
  std::atomic<bool> zerofg_teardown_idle_{false};
  std::atomic<bool> lost_{false};
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // ZEROFG_PORTABLE_XENIA_UI_VULKAN_VULKAN_DEVICE_H_
