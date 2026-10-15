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
| `include/xenia/base/frame_stats.h` | Xenia's frame stats (the XenDroid file, as is; BSD 3-Clause, see License) | the Source timing: call the issue and publish recorders from the frame loop |
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

## What a host has to do

Learned bringing up the first host. The presenter does not tell you these; each one cost a run.

1. **Device B on the host's instance, presenting on the host's surface.** Android connects a window
   to one `VkSurfaceKHR` at a time, so device B cannot make its own: create it from the game's
   instance (raised to 1.3 while ZeroFG is on) and pass the game's surface to `Connect`. When the
   presenter takes the window, the game destroys its swap chain but keeps the surface.
2. **Activation is yours to ask for.** Record the Source issue and publish timestamps
   (`xe::RecordGuestPresent`, `xe::RecordSourcePresent`, then the publication telemetry) for every
   new game frame, and call `TryActivateFromNativeSourceCadence()` once per frame while the
   presenter is connected and not `accepting()`. Without the call it never qualifies the game's
   cadence and never takes a frame.
3. **Publish one size per connection.** Any change of the published extent between two accepted
   Real frames is a structural break: the presenter fails open (`extent_change`). Choose the
   generation size once per connection, from the picture's footprint on the screen, never past
   the size you connected with, and scale every later frame into it. Games change resolution and
   aspect under you (God of War II widens its picture when progressive scan goes on).
4. **Detect the hand-back yourself.** After a failure the presenter stops its egress, releases the
   window and calls the UI-request callback; `main_surface_reconnect_requested()` covers only an
   out-of-date surface. The host's rule, as XenDroid's normal presenter has it: you released the
   window and `owns_final_output()` is false, so rebuild the game's swap chain on the kept surface.
   Missing it leaves a frozen last frame on screen while the game keeps running.
5. **A presenter that failed refuses the window for good** (its Main Surface Authority session).
   To try again, destroy it and create a new one. Cap the retries.
6. **The Post's contract.** The FinalOutput images are `COLOR_ATTACHMENT | TRANSFER_SRC`: draw
   into them with a render pass that ends in `TRANSFER_SRC_OPTIMAL`, signal the job's timeline,
   and export the sync file the egress waits on (`acquire_fence_fd`). Anything you draw over the
   picture (an OSD, a performance overlay) belongs in the Post, so it is the same on Real and
   generated frames: snapshot it on the game thread, draw it on device B.
7. **The refresh rate is the app's job.** Ask for the panel's top mode from the Java side while
   ZeroFG is on: `preferredDisplayModeId`, `Surface.setFrameRate(…, CHANGE_FRAME_RATE_ALWAYS)` and,
   on Android 15+, `View.setRequestedFrameRate(REQUESTED_FRAME_RATE_CATEGORY_HIGH)`. From native
   code, `ANativeWindow_setFrameRate` lives in `libnativewindow.so`: `dlopen` it, the global scope
   does not have it. And on Android 15+ an app that declares itself a game (`android:isGame`,
   `android:appCategory="game"`) gets a 60 fps cap from GameManager that wins over those votes
   until the system lets go (`GameFrameRateOverrides` in `dumpsys SurfaceFlinger`).
8. **Switching in game is disconnect and connect.** Off: take the presenter off the window
   (`BeginSurfaceDisconnect`, the game's device idle, `DestroySurfaceResourcesAfterSourceIdle`,
   drain) and rebuild the game's swap chain. On: `Connect` again; it qualifies the cadence again in
   about half a second. A new engine mode: off, rebuild the engine and the presenter, on.
9. **GPU priority** for device B, as [`../xenia-android/README.md`](../xenia-android/README.md)
   step 3 says. Without it the two devices are not isolated in time when the game saturates the
   GPU.

## Status

First host: ARMSX2 (PS2), 2026-10-14, on a Xiaomi 25102PCBEG (Adreno 840; Turnip "Purple" T30,
God of War II).
- **Compiled:** the presenter, the egress, the handoff and the kit, for Android arm64 with the NDK
  (clang 21), without one change to the files in `../xenia-android/`.
- **Runtime validated:** the handoff self-test (PASS, no mismatches); the presenter taking the
  window from the game's swap chain; at 120 Hz a 60 fps game shown at 118-121 fps, Real and
  generated one to one, 19-32 ms from the game's issue to the screen (median); switching ZeroFG
  off and on in game; the game's overlay drawn by the Post.
- **Observed:** with the panel held at 60 Hz (the GameManager cap above) and a 60 fps game, the
  presenter keeps generating and its Source backpressure holds the game at 30 fps. A host should
  let the user switch frame generation off there.
- **Not done in ARMSX2 yet:** GPU priority (item 9).

## License

Apache-2.0, like the rest of this repository, with one exception that keeps Xenia's BSD 3-Clause
license ([`../xenia-android/LICENSE-Xenia`](../xenia-android/LICENSE-Xenia)):
`include/xenia/base/frame_stats.h`, XenDroid's file from its Xenia source tree, written there by
XenDroid's author and the ZeroFG authors. A build that ships the kit carries that notice with it.
