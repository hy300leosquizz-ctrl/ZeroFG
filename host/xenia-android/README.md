# The ZeroFG presenter (reference host: XenDroid on Android)

The engine makes frames; the presenter turns that into frame generation. It
decides which real frames to keep, when to generate, when each output is shown,
and in what order, and it carries every frame from the game's GPU device to the
screen. This folder is the presenter exactly as it ships in
[XenDroid-ZeroFG 1.0](https://github.com/hy300leosquizz-ctrl/XenDroid-ZeroFG),
about 24,500 lines.

It is a reference, not a library: it builds inside XenDroid-ZeroFG and depends
on parts of XenDroid listed below. Read it to see how every problem in
[INTEGRATION.md](../../INTEGRATION.md) was solved for real, and port the pieces
your host needs.

## The files

| File | What it does |
| --- | --- |
| `zerofg_independent_presenter.h/.cc` | The presenter: capture and residency of real frames, admission of generated frames, the pacing clock, production (generation and post), the order of what is shown, the fallback |
| `zerofg_main_surface_egress.h/.cc` | Presentation on the game's own Android surface from ZeroFG's device: one present per display refresh, never waiting for the display, learning system frame caps, the GPU guard |
| `zerofg_device_handoff.h/.inc` | Moving each real frame from the game's Vulkan device to ZeroFG's (Android hardware buffers and sync files) |
| `zerofg_completion_owner.h` | Proving GPU completion without ever blocking (timelines and sync files) |
| `vulkan_presenter_zerofg_device_context.inc` | Creating ZeroFG's own Vulkan device and its output pipelines |
| `vulkan_presenter_zerofg_source_adapter.inc` | Publishing each finished game frame to the presenter |
| `zerofg_config.h` | The user settings: mode (off, zero, reallyzero) and the GPU guard |
| `zerofg_xenia_adapter.h/.cc` | The thin wrapper XenDroid uses around `zerofg::Interpolator` |

The rest of the integration lives in XenDroid's `vulkan_presenter.cc`: the
callbacks that record a generation (`ProcessZeroFGGeneration`,
`PollZeroFGGeneration`), run the normal XenDroid output pipeline on a frame
(`ProcessZeroFGPost`), and hand the surface over.

## How it is driven

```text
 game thread           presenter thread                 egress thread
 ───────────           ────────────────                 ─────────────
 frame N done ──▶ PublicationCommitted()
                  capture N into residency
                  generate S(N-1, N)  ──callback──▶ host records zerofg::Interpolate
                  post R(N-1), S       ──callback──▶ host runs its output pipeline
                  apply in order: R, S, R, S…  ───────▶ copy into a free buffer,
                                                         present on its own refresh
```

`Create` takes the callbacks. Each one is a narrow job the host does on its own
device:

| Callback | The host's job |
| --- | --- |
| `IngressSourceAcquireCallback` | hand over the latest finished game frame (image, size, completion) |
| `PaintConfigProvider` | the current output settings (scaling, sharpening, effects) |
| `GenerationCallback` / `GenerationPollCallback` | record and submit one `Interpolate` on ZeroFG's device; report when it completes, never by waiting |
| `SyntheticReleaseCallback`, `GenerationShutdownCallback` | recycle and destroy generation resources |
| `PostProcessCallback` / `PostProcessReleaseCallback` / `PostProcessShutdownCallback` | run the host's normal output pipeline on a real or generated frame into a final output |
| `PresenterDeviceDrainCallback` | make ZeroFG's device idle before teardown |

`Connect` hands it the Android window and surface; the surface methods
(`MainSurfaceReleasedByA`, `NoteMainSurfaceProducedByA` and their friends) make
sure the game's presenter and ZeroFG never produce into the screen at the same
time.

## What it needs from XenDroid

| Dependency | Used for | To port it |
| --- | --- | --- |
| `xenia/ui/vulkan/vulkan_device.h` | ZeroFG's own device: creation profile, function table, enabled features, queue access under a lock | your device wrapper |
| `xenia/ui/presenter.h` | the output settings type (`GuestOutputPaintConfig`) | your output settings |
| `xenia/base/frame_stats.h` | when each game frame was issued and published, the game's frame rate | timestamps from your frame loop |
| `xenia/base/logging.h`, `xenia/base/cvar.h`, `xenia/base/platform.h` | logs, settings, platform switches | your logger and settings |
| `emulator.h` (XenDroid's Android host) | nothing (an include left over; drop it) | — |
| Android: `AHardwareBuffer`, sync files, `ANativeWindow`, `VK_GOOGLE_display_timing` | moving frames between devices, presenting, reading when frames reached the screen | the same on Android; equivalents elsewhere |

## The rules it keeps

These took most of the two months, and each has a measured reason behind it:

- **Real frames are sovereign** until they leave ZeroFG: never dropped, never
  reordered. A generated frame that is late is retired, not forced in.
- **The screen never slows the game.** The egress copies only into buffers the
  system has already released, and it learns the system's frame cap from how
  long its own presents sleep, so the game keeps its speed under a cap.
- **One output per refresh, in order,** on the refresh lattice the display
  actually reports.
- **Nothing blocks on the GPU.** Completion is observed with sync files and
  non-waiting checks; some drivers turn a zero-timeout wait into a wait forever.
- **Fail open.** Any structural failure hands the screen back to the host's
  normal path for the rest of the session.

## License

Apache-2.0, like the rest of this repository. The XenDroid and Xenia files it
depends on keep their own licenses.
