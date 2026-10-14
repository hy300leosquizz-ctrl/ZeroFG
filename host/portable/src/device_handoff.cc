// Copyright 2026 ZeroFG authors. SPDX-License-Identifier: Apache-2.0
//
// The Source -> device B handoff (host/xenia-android/zerofg_device_handoff.inc),
// built as its own translation unit: in XenDroid it is included by Xenia's
// vulkan_presenter.cc inside xe::ui::vulkan, after the headers below.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

#include <errno.h>
#include <poll.h>
#include <unistd.h>

#if defined(__ANDROID__)
#include <android/hardware_buffer.h>
#endif

#include "xenia/base/frame_stats.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/ui/vulkan/vulkan_device.h"
#include "xenia/ui/vulkan/vulkan_presenter.h"
#include "xenia/ui/vulkan/vulkan_util.h"
#include "xenia/ui/vulkan/zerofg_device_handoff.h"

namespace xe {
namespace ui {
namespace vulkan {

#include "xenia/ui/vulkan/zerofg_device_handoff.inc"

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
