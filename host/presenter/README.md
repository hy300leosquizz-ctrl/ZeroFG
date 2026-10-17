# The ZeroFG Independent Presenter

The engine makes frames; the presenter turns that into frame generation. It
decides which real frames to keep, when to generate, when each output is shown,
and in what order, and it carries every frame from the game's GPU device to the
screen, on ZeroFG's own Vulkan device. About 25,000 lines, Android.

It builds in any Android host through [`../portable/`](../portable/), which
stands in for the few Xenia-shaped types it includes. Two hosts run it:
XenDroid (Xbox 360, [`../xendroid/`](../xendroid/)) and ARMSX2 (PS2). Read it
to see how every problem in [INTEGRATION.md](../../INTEGRATION.md) was solved
for real.

## The files

| File | What it does |
| --- | --- |
| `zerofg_independent_presenter.h/.cc` | The presenter: capture and residency of real frames, admission of generated frames, the pacing clock, production (generation and post), the order of what is shown, the fallback |
| `zerofg_main_surface_egress.h/.cc` | Presentation on the game's own Android surface from ZeroFG's device: one present per display refresh, never waiting for the display, learning system frame caps, and the display refresh the presenter can run ahead by (its output shaper, the old GPU guard, is off since 1.0) |
| `zerofg_device_handoff.h/.inc` | Moving each real frame from the game's Vulkan device to ZeroFG's (Android hardware buffers and sync files) |
| `zerofg_completion_owner.h` | Proving GPU completion without ever blocking (timelines and sync files) |
| `zerofg_build_guard.h` | Surviving a driver that crashes compiling ZeroFG's pipelines: a marker around each build, Compat at the next launch, off after a second crash, per driver and app build |
| `zerofg_config.h` | The user settings (mode: off, zero, reallyzero; the game frame rate cap) and the flag that says the presenter is live, which a host's guest pacing reads |

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

## What it needs from a host

It includes a few Xenia-shaped headers; [`../portable/`](../portable/) provides
all of them, so a host supplies only what the right-hand column says.

| Dependency | Used for | What the host gives |
| --- | --- | --- |
| `xenia/ui/vulkan/vulkan_device.h` | ZeroFG's own device: creation profile, function table, enabled features, queue access under a lock | its instance and device handles (`VulkanDevice::Wrap`) |
| `xenia/ui/presenter.h` | the output settings type (`GuestOutputPaintConfig`) | its output settings |
| `xenia/base/frame_stats.h` | when each game frame was issued and published, the game's frame rate | timestamps from its frame loop |
| `xenia/base/logging.h`, `xenia/base/cvar.h`, `xenia/base/platform.h` | logs, settings, platform switches | a log sink and the mode |
| `emulator.h` | nothing (an include left over) | — |
| Android: `AHardwareBuffer`, sync files, `ANativeWindow`, `VK_GOOGLE_display_timing` | moving frames between devices, presenting, reading when frames reached the screen | the same on Android; equivalents elsewhere |

## Porting it to another host

1. **Add the engine** to your build (see [INTEGRATION.md](../../INTEGRATION.md)).
2. **Give ZeroFG its own Vulkan device** on the same GPU, with a queue that can
   present, Vulkan 1.3 and synchronization2.
   `../xendroid/vulkan_presenter_zerofg_device_context.inc` shows what XenDroid
   enables.
3. **Give that device GPU priority** over the game's. On Adreno with a custom
   Turnip driver, apply the idea of `../xendroid/adrenotools/kgsl-context-priority.patch`
   in your driver loader; elsewhere use `VK_KHR_global_priority` if the driver
   honours it.
4. **Build it with [`../portable/`](../portable/)**, and wrap your devices.
5. **Publish your frames**: call `PublicationCommitted` when a game frame is
   finished, and answer `IngressSourceAcquireCallback` with it.
6. **Implement the callbacks**: a Generation records
   `zerofg::Interpolator::Interpolate` and submits it; a Post runs your own
   output pipeline (`../xendroid/xendroid_glue/` has XenDroid's).
7. **Hand the screen over** with `Connect` and the surface methods, so your
   presenter and ZeroFG never present at the same time.

The kit's README lists what a host still has to do that the presenter does not
tell you (activation, one size per connection, the hand-back, the refresh
rate), learned bringing up ARMSX2.

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

## Tuning a host's pacing

Measure the pacing again for every new host. Hosts differ in ways the
presenter cannot see:

- **Who sets the frame clock.** The host may pull the game's frames
  (XenDroid's guest vblank pull), or an emulator's own frame limiter may set
  it (PPSSPP, ARMSX2).
- **The copy.** ZeroFG's device may have one queue or two, so the final copy
  can be slow or fast.
- **The screen.** Other layers can update above the game, and the system may
  cap the app's frame rate (HyperOS throttles to 90 or 60 Hz when hot).

The defaults below are the ones measured in play on three hosts. Each tool is
a setting from `zerofg_config.h` that the host sets before it creates the
presenter. [`../portable/`](../portable/) defines them; a host with its own
settings system defines them itself.

| Setting | Default | What it does | When to change it |
| --- | --- | --- | --- |
| `zerofg_phase_recovery` | on | Keeps the output calendar on the game's own clock (see 1.0.4 below). | Off only for an A/B. Without it, a game on its own clock drifts the calendar into uneven 1-3 refresh pairs at 30 -> 60. |
| `zerofg_vsync_quantizer` | on | Gives each output a refresh of its own, in order, the first one its copy can make. | Off only for an A/B. Without it, two outputs can meet in one refresh and only the newest is shown. |
| `zerofg_free_output` | on | Never waits for the screen. It learns the system's frame cap from how long its own presents sleep and drops the oldest output, not the game's speed. | Off only for an A/B. |
| `zerofg_vsync_tight_reach` | **off** | Optional. The first refresh an output can make is bound by two deadlines instead of their sum: queued one refresh plus 2 ms ahead, and copied 1.5 ms ahead (an unsignaled latch). Past 5 % late presents among those only it placed, it switches itself off for the session. | Try it when the copy is slow (one queue, the copy behind Generation) and the game's layer updates alone on the screen. In XenDroid 6-14 % of the presents only it placed came back late. On PPSSPP and ARMSX2 it held (0-3 % late) but no latency gain was measured. |
| `zerofg_fps_limit` | 0 (off) | A cap on the game's frame rate, for a host that paces the game through ZeroFG. | A game whose own rate the screen cannot double. |
| `SetPublishDepth` (handoff) | 1 | Publications that may wait for ZeroFG's device before `Publish` blocks. | A game that runs ahead of its GPU. |

How to measure, before and after any change:

- **The screen's own record.** `adb shell dumpsys SurfaceFlinger --latency
  '<your SurfaceView layer>'` gives the times the last ~128 frames were shown.
  Each interval, in refreshes, should be 1 at 60 -> 120 and 2 at 30 -> 60.
  Uneven 1-3 pairs mean the calendar is behind. Game Turbo-style overlays
  count the frames that reached the screen. A ZeroFG overlay built on the
  applied counter counts the frames sent to it.
- **`ZeroFGPacing`.** The lattice and rate lock periods, the excess (how far
  committed frames sit from Better D's depth), and phase recovery's reference
  period, spread and recoveries (frequency, lag, drain).
- **`ZeroFGMainSurface` summary.** Applied against presented frames,
  `desired_to_actual`, the actual interval distribution, vsync hit/late,
  `tight_only/late`, and `present_limiter`. An engaged limiter with a period
  of ~11.1 ms is a 90 Hz system cap, not a pacing defect.
- **`ZeroFGPhaseCInvariants`.** It must stay 0.

Better D's depth is not a tool. Its margin is the measured tail of the
generated frame's post completion, and cutting it 4 ms took generated frames
from 1.7 to 12 % late in God of War II.

## What changed in 1.0.4

Phase recovery, found on the hosts that cannot pull their game's frames
(PPSSPP, ARMSX2) and measured in play on the Adreno 840 (PPSSPP Naruto
Shippuden: Ultimate Ninja Impact and God of War: Ghost of Sparta, ARMSX2
Resident Evil 4 and God of War II, XenDroid Rayman Origins and Batman: Arkham
City under a 30 fps cap):

- **The calendar stays on the game's clock.** Under the rate lock the 3 % dead
  band could keep a period a little short, and nothing pulled a calendar that
  fell behind back to the game. A game on its own clock then drifted it away:
  PPSSPP Naruto at 30 reached -1.4 s, with every generated frame shown next to
  its real one (uneven 1-3 refresh pairs at 30 -> 60). The game's period is now
  measured from the issue times of committed real frames: the median slope
  between frames half of a 256-frame window apart, trusted when its spread is
  within 0.3 %, else the regime estimate while it agrees with the lock's own
  period within 3 %. That period is the one frequency authority, and the
  lock's own excess correction yields to it. A pair period off by more than
  max(0.3 %, four times the spread), at most 1.5 %, is reinstalled.
- **Phase, with a cushion.** Over 4 s of the epoch's real frames, both halves
  agreeing, a calendar shallower than Better D's depth (a quantum short at one
  output per refresh) is moved back a cushion of max(O/4, 2 ms) above it. One
  more than two cushions deep drains through the rate down to one: at most 2 %
  a pair, over 64 pairs. Each change is a future-only reanchor, so nothing
  already committed moves, nothing is skipped and the real frames keep their
  order.
- **Measured.**
  - PPSSPP Naruto at 30 -> 60: 2 2 again.
  - ARMSX2 RE4: about 13 ms less from the game's frame to its target, with 0-3 %
    of generated frames late.
  - PPSSPP Ghost of Sparta: the reanchor storm of a noisy rate lock gone.
  - XenDroid Arkham under a 30 cap: 79-89 % of intervals at two refreshes,
    from 38 %.
  - Invariants 0 throughout.
- **Kept.** Better D's depth: cutting it 4 ms takes generated frames from 1.7
  to 12 % late in God of War II. Its margin is the tail of the generated
  frame's post completion.

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
- **Guest pacing** (`../xendroid/xendroid_glue/guest-pacing.patch`): the vblank pull and
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

- **Two game frames in flight, not three.** This lives in the XenDroid host's command
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
- **The build guard** (`zerofg_build_guard.h`, used in `../xendroid/xendroid_glue/`).

## License

Apache-2.0, like the rest of this repository.
