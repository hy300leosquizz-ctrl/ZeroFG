# The portable host kit

The presenter in [`../xenia-android/`](../xenia-android/) was written inside XenDroid against a
handful of Xenia types. This kit is small stand-ins for exactly those types, so the presenter,
its main-surface egress and the device handoff build **unchanged** in another Android host. It is
the step [`../xenia-android/README.md`](../xenia-android/README.md) names as next: the presenter
behind a small host interface.

## What is in it

| Header | Stands in for | What the host does with it |
| --- | --- | --- |
| `include/zerofg_host/host.h` | — | `SetLogSink`: where the presenter's log lines go |
| `include/xenia/ui/vulkan/vulkan_device.h` | Xenia's `VulkanInstance`, `VulkanDevice` | `VulkanInstance::Wrap`, `VulkanDevice::Wrap`: its own instance and device handles, queues and enabled facts |
| `include/xenia/base/frame_stats.h` | Xenia's frame stats (the XenDroid file, as is) | the Source timing: call the issue and publish recorders from the frame loop |
| `include/xenia/ui/presenter.h` | `Presenter::GuestOutputPaintConfig` | the output settings, carried unread from the paint provider to the post callback |
| `include/xenia/base/{logging,cvar,platform}.h` | Xenia's logging, cvars, platform switches | sets `cvars::zerofg_mode` before creating the presenter |
| `include/xenia/ui/vulkan/{vulkan_presenter,vulkan_util}.h` | three constants and four helpers | nothing |
| `include/emulator.h` | an include the presenter leaves unused | nothing |

`src/` implements the wrappers, the log sink, the cvar definitions and the handoff as its own
translation unit. `CMakeLists.txt` stages the presenter's headers under the `xenia/ui/vulkan/` path
it includes them by and builds the static library `zerofg::host`. It is Android only.

## Using it

```cmake
set(ZEROFG_HOST_VULKAN_HEADERS_DIR "<the Vulkan headers you build with>")
set(ZEROFG_HOST_FMT_TARGET fmt::fmt)          # a {fmt} target
add_subdirectory(<zerofg>/host/portable zerofg_host)
target_link_libraries(my_host PRIVATE zerofg::host)
```

Wrap device B (ZeroFG's own, Vulkan 1.3 with synchronization2 and timelines, plus the swapchain,
AHardwareBuffer and sync-file extensions) and the game's device A (AHardwareBuffer, sync files and
synchronization2, as an extension on an older device). Then `ZeroFGDeviceHandoff(a, b).SelfTest()`
qualifies the path between them, and `ZeroFGIndependentPresenter::Create` takes device B and the
callbacks of [`../xenia-android/README.md`](../xenia-android/README.md).

On a device older than 1.3 the promoted entry points (synchronization2, timelines) resolve under
their `KHR` names; everything newer than Vulkan 1.0 is optional in `VulkanDevice::Wrap`, and the
presenter and the handoff check what they need themselves.

## Status

First host: ARMSX2 (PS2), 2026-10-14.
- **Compiled:** the presenter, the egress, the handoff and the kit, for Android arm64 with the
  NDK (clang 21), without one change to the files in `../xenia-android/`.
- **Pending:** the handoff self-test on a phone, then the presenter's callbacks in the host.
