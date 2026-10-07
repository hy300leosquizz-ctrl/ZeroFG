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
| `zerofg_main_surface_egress.h/.cc` | Presentation on the game's own Android surface from ZeroFG's device: one present per display refresh, never waiting for the display, learning system frame caps, and the GPU guard (two refreshes per output once a throttled GPU makes the image stutter) |
| `zerofg_device_handoff.h/.inc` | Moving each real frame from the game's Vulkan device to ZeroFG's (Android hardware buffers and sync files) |
| `zerofg_completion_owner.h` | Proving GPU completion without ever blocking (timelines and sync files) |
| `vulkan_presenter_zerofg_device_context.inc` | Creating ZeroFG's own Vulkan device and its output pipelines |
| `vulkan_presenter_zerofg_source_adapter.inc` | Publishing each finished game frame to the presenter |
| `zerofg_config.h` | The user settings: mode (off, zero, reallyzero) and the GPU guard |
| `zerofg_xenia_adapter.h/.cc` | The thin wrapper XenDroid uses around `zerofg::Interpolator` |
| `xendroid_glue/vulkan_presenter_zerofg_glue.cc` | The other half: the callbacks inside XenDroid's own presenter that record a Generation, run the normal output pipeline on a frame (the Post) and hand the surface over |

The rest of the integration lives inside XenDroid's own presenter
(`vulkan_presenter.cc`, a Xenia file). Its ZeroFG functions are copied in
`xendroid_glue/` as an excerpt: `ProcessZeroFGGeneration` and
`PollZeroFGGeneration` record and watch a generation, `ProcessZeroFGPost` runs
the normal output pipeline on a frame, and the surface functions hand the
screen over.

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

## Porting it to another host

1. **Add the engine** to your build (see [INTEGRATION.md](../../INTEGRATION.md)).
2. **Give ZeroFG its own Vulkan device** on the same GPU, with a queue that can
   present, Vulkan 1.3 and synchronization2. `vulkan_presenter_zerofg_device_context.inc`
   shows what XenDroid enables.
3. **Replace the XenDroid types** in the table above with yours: the device
   wrapper, the output settings, the frame timestamps, the logger and the
   settings.
4. **Publish your frames**: call `PublicationCommitted` when a game frame is
   finished, and answer `IngressSourceAcquireCallback` with it.
5. **Implement the callbacks** following `xendroid_glue/`: a Generation records
   `zerofg::Interpolator::Interpolate` and submits it; a Post runs your own
   output pipeline.
6. **Hand the screen over** with `Connect` and the surface methods, so your
   presenter and ZeroFG never present at the same time.

Making this semi-automatic (the presenter behind a small host interface, so it
builds outside XenDroid unchanged) is the next step for this repository.

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

Apache-2.0, like the rest of this repository, with two exceptions that keep
Xenia's BSD 3-Clause license ([LICENSE-Xenia](LICENSE-Xenia)):
`vulkan_presenter_zerofg_device_context.inc`, adapted from Xenia's presenter, and
`xendroid_glue/vulkan_presenter_zerofg_glue.cc`, an excerpt of it. The XenDroid
and Xenia files it depends on keep their own licenses.
