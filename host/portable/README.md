# The portable host kit

The ZeroFG Independent Presenter in [`../presenter/`](../presenter/) was first built inside a
Xenia-based host, against a handful of Xenia types. This kit is small stand-ins for exactly those
types, so the presenter, its main-surface egress and the device handoff build **unchanged** in any
Android host: the presenter behind a small host interface.

## What is in it

| Header | Stands in for | What the host does with it |
| --- | --- | --- |
| `include/zerofg_host/host.h` | — | `SetLogSink`: where the presenter's log lines go |
| `include/xenia/ui/vulkan/vulkan_device.h` | Xenia's `VulkanInstance`, `VulkanDevice` | `VulkanInstance::Wrap`, `VulkanDevice::Wrap`: its own instance and device handles, queues and enabled facts |
| `include/xenia/base/frame_stats.h` | Xenia-style frame stats (as is; BSD 3-Clause, see License) | the Source timing: call the issue and publish recorders from the frame loop |
| `include/xenia/ui/presenter.h` | `Presenter::GuestOutputPaintConfig` | the output settings, carried unread from the paint provider to the post callback |
| `include/xenia/base/{logging,cvar,platform}.h` | Xenia's logging, cvars, platform switches | sets `cvars::zerofg_mode` before creating the presenter, and the pacing tools only after measuring ([presenter README](../presenter/README.md#tuning-a-hosts-pacing)) |
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
callbacks of [`../presenter/README.md`](../presenter/README.md).

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
   out-of-date surface. The host's rule: you released the window and `owns_final_output()` is
   false, so rebuild the game's swap chain on the kept surface.
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
   does not have it. Two things win over those votes: on Android 15+ an app that declares itself
   a game (`android:isGame`, `android:appCategory="game"`) gets a 60 fps cap from GameManager
   until the system lets go (`GameFrameRateOverrides` in `dumpsys SurfaceFlinger`), and an OEM's
   thermal policy caps the panel at 90 or 60 as the skin warms (`PRIORITY_THERMAL_LIMIT_REFRESH_RATE`
   among the DisplayModeDirector votes in `dumpsys display`, from ~44-46 °C of skin on HyperOS).
   Read those before blaming the pacing for a 60 Hz panel.
8. **Switching in game is disconnect and connect.** Off: take the presenter off the window
   (`BeginSurfaceDisconnect`, the game's device idle, `DestroySurfaceResourcesAfterSourceIdle`,
   drain) and rebuild the game's swap chain. On: `Connect` again; it qualifies the cadence again in
   about half a second. A new engine mode: off, rebuild the engine and the presenter, on.
9. **GPU priority** for device B, as [`../presenter/README.md`](../presenter/README.md) step 3
   says: `VK_KHR_global_priority` for drivers that honour it, and on Adreno with a custom Turnip the
   KGSL context priority through adrenotools ([`../xendroid/adrenotools/`](../xendroid/adrenotools/)).
   Without it the two devices are not isolated in time when the game saturates the GPU. Turned
   off on ARMSX2, device B fell behind the game in a heavy scene (31 + 31 on screen instead of
   42 + 42, Source backpressure 25-50 %).
10. **Capture what your present draws.** Take the game's frame from the texture your present
    actually samples, with the rectangle it samples: an upscaler or a sharpener that runs before
    the present replaces the texture and its coordinates (ARMSX2 at 1x with FSR: a 1600x1200
    rectangle read from a 640x448 texture filled a third of the screen with smeared edges).
11. **Draw nothing for your own present while the presenter owns the window.** Nothing you draw
    there reaches the screen; skip the render pass and the blit, and keep only the capture, the
    overlay's draw data and the submit.
12. **Publish depth.** `ZeroFGDeviceHandoff::SetPublishDepth` (1 by default) sets how many
    publications may wait for device B before `Publish` blocks the game. A Source that limits its
    own frames in flight is fine at 1; a GS-style thread that runs a frame or two ahead of its
    GPU can take 2. It stays lossless: the game still waits once the depth is full.

## Status

First host: ARMSX2 (PS2), 2026-10-14, on a Xiaomi 25102PCBEG (Adreno 840; Turnip "Purple" T30,
God of War II).
- **Compiled:** the presenter, the egress, the handoff and the kit, for Android arm64 with the NDK
  (clang 21), without one change to the files in `../presenter/`.
- **Runtime validated:** the handoff self-test (PASS, no mismatches); the presenter taking the
  window from the game's swap chain; at 120 Hz a 60 fps game shown at 118-121 fps, Real and
  generated one to one, 19-32 ms from the game's issue to the screen (median); switching ZeroFG
  off and on in game; the game's overlay drawn by the Post; the GPU priority (global priority
  high, a KGSL draw context created at priority 4 by the hook); the capture behind an upscaler
  (item 10). God of War II and Devil May Cry 3 SE run clean.
- **Compiled, not yet runtime validated:** switching between Zero and ReallyZero in game.
- **Observed:** with the panel held at 60 Hz (the GameManager cap or the thermal cap above) and a
  60 fps game, the presenter keeps generating and its Source backpressure holds the game at 30
  fps; in an emulator paced by its emulated vblank that is slow motion. A host should let the
  user switch frame generation off there.
- **Observed:** a game whose frame is heavy in render passes and copies (Shadow of the Colossus in
  its 60 fps mode: ~290 passes and ~200 texture copies a frame, against ~30 and ~10 in God of
  War II) slows to 74-78 % with ZeroFG on, while its 30 fps mode and other games run clean. Not
  explained yet.

## License

Apache-2.0, like the rest of this repository, with one exception that keeps Xenia's BSD 3-Clause
license ([`../xendroid/LICENSE-Xenia`](../xendroid/LICENSE-Xenia)):
`include/xenia/base/frame_stats.h`, from XenDroid's Xenia source tree, written there by XenDroid's
author and the ZeroFG authors. A build that ships the kit carries that notice with it.
