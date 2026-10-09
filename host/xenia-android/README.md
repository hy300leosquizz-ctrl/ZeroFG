# The ZeroFG presenter (reference host: XenDroid on Android)

The engine makes frames; the presenter turns that into frame generation. It
decides which real frames to keep, when to generate, when each output is shown,
and in what order, and it carries every frame from the game's GPU device to the
screen. This folder is the presenter exactly as it ships in
[XenDroid-ZeroFG 1.0.2](https://github.com/hy300leosquizz-ctrl/XenDroid-ZeroFG),
about 25,000 lines.

It is a reference, not a library: it builds inside XenDroid-ZeroFG and depends
on parts of XenDroid listed below. Read it to see how every problem in
[INTEGRATION.md](../../INTEGRATION.md) was solved for real, and port the pieces
your host needs.

## The files

| File | What it does |
| --- | --- |
| `zerofg_independent_presenter.h/.cc` | The presenter: capture and residency of real frames, admission of generated frames, the pacing clock, production (generation and post), the order of what is shown, the fallback |
| `zerofg_main_surface_egress.h/.cc` | Presentation on the game's own Android surface from ZeroFG's device: one present per display refresh, never waiting for the display, learning system frame caps, and the display refresh the presenter can run ahead by (its output shaper, the old GPU guard, is off since 1.0) |
| `zerofg_device_handoff.h/.inc` | Moving each real frame from the game's Vulkan device to ZeroFG's (Android hardware buffers and sync files) |
| `zerofg_completion_owner.h` | Proving GPU completion without ever blocking (timelines and sync files) |
| `zerofg_build_guard.h` | Surviving a driver that crashes compiling ZeroFG's pipelines: a marker around each build, Compat at the next launch, off after a second crash, per driver and app build |
| `vulkan_presenter_zerofg_device_context.inc` | Creating ZeroFG's own Vulkan device and its output pipelines |
| `vulkan_presenter_zerofg_source_adapter.inc` | Publishing each finished game frame to the presenter |
| `zerofg_config.h` | The user settings (mode: off, zero, reallyzero; the game frame rate cap) and the flag that says the presenter is live, which the guest pacing reads |
| `zerofg_xenia_adapter.h/.cc` | The thin wrapper XenDroid uses around `zerofg::Interpolator` |
| `adrenotools/kgsl-context-priority.patch` | GPU priority for ZeroFG's device on Adreno: the change to XenDroid's libadrenotools (a hook that sets the KGSL priority of the contexts a custom driver creates) and to `vulkan_device.cc`/`vulkan_instance.cc` that uses it ([README](adrenotools/README.md)) |
| `xendroid_glue/guest-pacing.patch` | Guest pacing, in XenDroid's GPU code (command processor, vblank limiter, PM4 wait): a vblank probe learns the address a game waits on for its flip, a frame already late for it gets its vblank at once (the vblank it replaces is skipped, so the game never speeds up), and the game frame rate cap holds swaps at least 1/limit apart. Both only while the presenter is live |
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
3. **Give that device GPU priority** over the game's. On Adreno with a custom
   Turnip driver, apply `adrenotools/kgsl-context-priority.patch` (or the same
   idea in your driver loader); elsewhere use `VK_KHR_global_priority` if the
   driver honours it.
4. **Replace the XenDroid types** in the table above with yours: the device
   wrapper, the output settings, the frame timestamps, the logger and the
   settings.
5. **Publish your frames**: call `PublicationCommitted` when a game frame is
   finished, and answer `IngressSourceAcquireCallback` with it.
6. **Implement the callbacks** following `xendroid_glue/`: a Generation records
   `zerofg::Interpolator::Interpolate` and submits it; a Post runs your own
   output pipeline.
7. **Hand the screen over** with `Connect` and the surface methods, so your
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

## What changed in 1.0.2

Pacing, from the 1.1 work, measured in play on the Adreno 840 (Halo 3, Rayman
Origins, Modern Warfare 3):

- **The rate lock.** The regime estimate learns only from clean intervals and
  moves only when a window clusters; a game on a 60 Hz guest vsync that takes
  two or three vblanks per frame (33/50 ms in a drifting mix) never clusters,
  and under backpressure every interval is censored, so the period stayed where
  it last caught a cluster: above the game's rate the pools filled and the game
  waited (~450 ms wells in Arkham City). The lattice now follows the trimmed
  mean of the last eight intervals, each without the backpressure wait in it
  (a waited sample pulls at most a fifth below the window), and runs up to a
  fifth faster while committed real frames sit more than one quantum deeper
  than Better D operates at. Changes go through the frequency confirmation and
  future-only reanchors with a 3 % dead band; Phase Debt stands down while the
  lock owns the lattice. Nothing is skipped.
- **The reanchor cutover.** A frequency reanchor happens when a real frame is
  accepted, while the first frame the new epoch will commit still has its
  predecessor and generated frame in the old epoch. The new epoch could start
  before that frame's floor, a slot nothing can fill: the frame took the next
  tick and the empty one counted as a hold. The epoch now starts at that floor
  when it is later (the next frame's pair boundary, or the oldest uncommitted
  frame's floor once its predecessor is committed). No commitment moves and no
  floor is lowered. Modern Warfare 3, heavy scenes: holds 3.6 % -> 0.7 %, the
  worst interval per window one refresh shorter.
- **Better D's references.** A need sample uses its pair's own quantum (a
  reanchor can change the global one inside the window), and a pair counts as
  late against the rate lock's period, not a stale regime period.
- **Guest pacing** (`xendroid_glue/guest-pacing.patch`): the vblank pull and
  the game frame rate cap, acting only while the presenter is connected and has
  not failed open. A cap pays in a game that keeps the GPU at 100 %: Modern
  Warfare 3 at 40 keeps ~80 fps on screen with 55 ms of lag instead of 70 and
  1 % of missed refreshes instead of 12; Halo 3 at 20 halves its lag. A game
  whose logic runs per frame slows under a cap below its own rate.
- **Always-S.** On its own device the presenter has no authority to retire a
  generated frame that was admitted: the real frame after it waits until it is
  ready. The unreachable retire path that remained is gone (no change).

## What changed in 1.0.1

Latency and the cadence of a game below 30 fps, from the 1.1 work, measured in
play on the Adreno 840:

- **Two game frames in flight, not three.** This lives in XenDroid's command
  processor (`frames_in_flight_limit_`), not here: with GPU priority the third
  frame only queued a frame more of latency and let the period lock at 20 fps
  under stalls (37-90 ms on two against 80-540 on three).
- **Pacing section 7 is off.** The physical operating point was built for the
  pre-priority GPU queue; with priority it only locked the output at 40 fps
  with about 540 ms of delay after a few hiccups.
- **The game's wait for its own GPU is not backpressure.** The handoff's
  Publish waits for its depth slot while the previous frame's copy still runs
  on the game's device; that wait no longer counts as ZeroFG's backpressure.
  Counting it censored every period sample of a GPU-bound game, froze the
  period at 30 fps while the game ran 20-25, and sent pairs out back to back.
- **The presentation advance.** The physical presentation may run a few
  refreshes ahead of the semantic target to drain the latency well that late
  commitments leave behind. Nothing is skipped and the order and lattice are
  unchanged. It is damped (a dead band of two refreshes, streaks), watches the
  generated frames and the capacity gate and steps down at once when the gate
  refuses, and is capped at 250 ms.
- **The fail-open drain polls the capture transfers,** so the handback
  completes: Halo 3 froze on ZeroFG's last frame after the engine refused its
  picture size.
- **The build guard** (`zerofg_build_guard.h`, used in `xendroid_glue/`).

## License

Apache-2.0, like the rest of this repository, with two exceptions that keep
Xenia's BSD 3-Clause license ([LICENSE-Xenia](LICENSE-Xenia)):
`vulkan_presenter_zerofg_device_context.inc`, adapted from Xenia's presenter, and
`xendroid_glue/vulkan_presenter_zerofg_glue.cc`, an excerpt of it. The
adrenotools patch changes libadrenotools, which keeps its BSD 2-Clause license;
`xendroid_glue/guest-pacing.patch` changes Xenia files, which keep theirs.
The XenDroid and Xenia files it depends on keep their own licenses.
