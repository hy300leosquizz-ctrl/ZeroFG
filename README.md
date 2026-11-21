# ZeroFG

> Frame generation for mobile GPUs, built on Vulkan. **Version 1.2.0.**

ZeroFG multiplies a game's frame rate by the factor you choose (x2, x3 or x4)
by creating synthetic frames between the real ones. A game at 30 fps can show
60, 90 or 120.

This repository has:

- **the engine** (`include/`, `src/`, `shaders/`): records the GPU work for a
  generated frame into a command buffer you give it, so it fits any Vulkan host;
- **the presenter** (`host/presenter/`): captures the game's frames, paces the
  generated ones and presents them without slowing the game;
- **the integration notes** ([INTEGRATION.md](INTEGRATION.md)): how to bring
  ZeroFG into your own host.

## How it works

ZeroFG finds how every pixel moved between two real frames, then draws the
frames in between along that motion.

1. **Pyramid.** Both frames are reduced to a stack of smaller versions.
2. **Camera.** On the smallest one, every shift of the whole picture is tried;
   the best is the first guess, so a fast pan does not outrun the search.
3. **Motion, coarse to fine.** Each level refines the guess of the level above,
   also trying "standing still" and the previous frame's answer.
4. **HUD hold.** What stayed in place, with texture, for six frames in a row is
   the HUD, and it stays put while the world moves behind it.
5. **Scene cut.** A real cut shows the later frame whole; a wipe or a fade,
   where what is lit in both frames still matches, is interpolated.
6. **Picture and cleanup.** Both frames are blended along the motion at each
   in-between point, and only the spots where they disagree are softened.

| Cost per generated frame at 1280 x 640 | x2 | x4 (three frames) |
| --- | --- | --- |
| Adreno 840 | 0.78 ms | 2.58 ms |
| Adreno 650 (Turnip) | about 10 ms | |

## What's new in 1.2

- **One engine, On or Off.** No modes to choose: what we learned in the old
  Zero now lives in the dense-flow engine, at the cost of the 1.1 ReallyZero
  (see [A word about the old Zero](#a-word-about-the-old-zero)).
- **x3 and x4.** The presenter caps the factor by what the screen can show (x4
  from 30 fps is x2 on a 60 Hz screen, x3 at 90 Hz), so a refresh drop never
  queues frames it cannot show.
- **No latency wells.** When the factor times the game's rate equals the
  refresh (x4 at 30 on 120 Hz), delays used to pile up. Not anymore: Gears of
  War 3 at x4 on the Adreno 840 averaged 74.7 ms from the game's frame to the
  screen, with the display moving between 120, 90 and 60 Hz.

**1.2g** is 1.2 for hosts that see how the game draws its HUD (ARMSX2's
ZeroGS, from the PS2's GS): the HUD layer (`HudPlanes`) now runs on every
generated frame of x2, x3 and x4. The first frame of a pair takes the still
HUD out of both real frames; the others reuse it and only put it back. On the
Adreno 840 that is 5-15 % at x2 and 1-3 % for a whole x4 pair, for up to
1.4 dB more on the HUD where the camera moves under it. Without HUD planes it
is 1.2.0, frame for frame.

## How 1.2 compares

### 30 -> 60, the real case

A 30 fps game shown at 60. We take frames from 60 fps gameplay videos, give the
generators every other frame and measure against the real one they skipped.
Each cell is **PSNR / HF/GT**: PSNR in dB, higher is better, best of each row in
bold; HF/GT is how much fine detail survives (1.0 is all of it, below is
softer, above 1 is dirt). Same pairs for everyone; LSFG 3.1 runs its own
shaders from the user's Lossless.dll in the same harness.

| Clip (30 -> 60) | ZeroFG 1.2 | ReallyZero 1.1 | Zero 1.1 | LSFG 3.1 |
| --- | --- | --- | --- | --- |
| Spider-Man 2 (web 4K60, 720p) | **21.11** / 0.49 | 20.77 / 0.54 | 21.08 / 0.46 | 20.97 / 0.44 |
| Uncharted 4 (web 4K60, 720p) | **20.65** / 0.38 | 20.40 / 0.42 | 20.55 / 0.40 | 20.39 / 0.44 |
| Forza Horizon 5 (web 4K60, 720p) | **21.87** / 0.50 | 21.78 / 0.51 | 21.70 / 0.45 | 21.65 / 0.38 |
| Arkham Knight (web 4K60, 1080p) | 28.07 / 0.87 | 28.03 / 0.87 | **28.17** / 0.76 | 27.94 / 0.64 |
| Arkham Knight (web 4K60, 720p) | **28.65** / 0.76 | 28.62 / 0.75 | 28.59 / 0.67 | 28.49 / 0.60 |
| Arkham Knight, One Man Army (web 1080p60, 720p) | **31.74** / 0.83 | 31.65 / 0.83 | 31.40 / 0.79 | 31.61 / 0.72 |

ZeroFG 1.2 is ahead of LSFG 3.1 and ReallyZero 1.1 on all six clips and of
Zero 1.1 on five, and keeps more fine detail than LSFG 3.1 on five of six.
Pairs where each falls more than 0.5 dB behind 1.2: LSFG 3.1 160, Zero 1.1
121, ReallyZero 1.1 32.

### 15 -> 30, the stress test

The same on 30 fps sources (Arkham City, God of War II, Katamari Damacy, a
Rayman transition, a reel of scene cuts): twice the motion. ZeroFG 1.2 is ahead
of ReallyZero 1.1 and Zero 1.1 on all seven clips and of LSFG 3.1 on five
(level on two). Largest gaps: a fast Arkham City pan (25.14 against LSFG's
24.69) and the Rayman wipe (20.04 against 19.36).

### The raycaster

Our lab renders the true middle frame itself: 15 scenes built to break frame
generation (fine grain under a fast pan, wires, occluders, a HUD over motion,
periodic textures, blinking light, a rolling Katamari ball, sparks), 16 pairs
each at 1280 x 720. Sum over the 15: **ZeroFG 1.2 421.0 dB**, ReallyZero 1.1
418.1, Zero 1.1 408.8, LSFG 3.1 390.2; first on 8 of 15. Zero 1.1 still leads
blinking light and the HUD over motion; LSFG 3.1 leads occluders and dirt.

## A word about the old Zero

The layered Zero of 1.1 was the most ambitious thing we built: surfaces that
move as a whole, its own motion for the nearest object, a screen layer,
occlusion mattes, a judge handing regions back to a dense picture. Up to 213
GPU dispatches per generated frame, and a lot of careful work to fit it on a
phone. It was a work of art.

It also became its own obstacle. Every layer added a way to be wrong, every
fix another rule, and its cost (three to five times ReallyZero's, out of reach
on an Adreno 650) bought less and less. So 1.2 keeps its three best ideas, the
camera, the HUD and the wipe, and lets the rest go. Its code stays here as
`Mode::kZero`, for anyone who wants to see how far a layered generator on a
phone can go.

## The presenter

In 1.2 the ZeroFG Independent Presenter generates x2, x3 or x4
(`zerofg_multiplier`), capped by what the display refresh holds, and drains
the latency wells of one output per refresh (`zerofg_mfg_tick_retire`, on). It
carries the scene-cut mark (`content_break`) with the frame and generates
nothing across it. Measured in play on the Adreno 840:

| Measurement | Before | After |
| --- | --- | --- |
| Xendroid+, Gears of War 3 at x4: from the game's frame to the screen | wells of 120-200 ms | 74.7 ms average, no wells, the display at 120, 90 and 60 Hz (1.2) |
| PPSSPP, Naruto at 30 -> 60: refreshes per frame | uneven 1-3 | an even 2 · 2 (1.0.4) |
| ARMSX2, Resident Evil 4: from the game's frame to its target | | about 13 ms sooner, 0-3 % of generated frames late (1.0.4) |
| XenDroid, Arkham City under a 30 fps cap: intervals at two refreshes | 38 % | 79-89 % (1.0.4) |
| XenDroid, Arkham City with the GPU 95-99 % busy: the frame copy | 40-170 ms | 1.3 ms, ZeroFG's device at high priority (1.0) |
| XenDroid, Modern Warfare 3 capped at 40: lag, ~80 fps on screen | 70 ms | 55 ms (1.0.2) |

The [presenter's README](host/presenter/README.md) has the details, and how to
tune it for a new host.

## Requirements

- A Vulkan 1.3 device with `synchronization2` and
  `shaderStorageImageExtendedFormats` enabled.
- `A2B10G10R10_UNORM_PACK32` storage images that can be blitted from, and an
  input format that can be sampled with linear filtering (`R8_UNORM` storage
  images too for the HUD path).
- A picture from 64 x 64 to 16384 x 16384.

Optional features the device has enabled make it faster (half-precision
colour, 64-wide subgroups, cubic filtering). Every backend gives the same
logical result.

## Building the engine

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This builds the static library `zerofg` (alias `zerofg::zerofg`). The shaders
ship compiled (`shaders/prebuilt`), so a host needs no shader tools. With
Python 3, `glslangValidator` and `spirv-opt` from the Vulkan SDK (set
`VULKAN_SDK`, or put both on `PATH`) they are compiled from `shaders/`
instead; `-DZEROFG_PREBUILT_SHADERS=ON|OFF` forces one or the other. After
changing a shader, build `zerofg_update_prebuilt_shaders` and commit
`shaders/prebuilt` with it (`zerofg_check_prebuilt_shaders` fails while they
differ). The Vulkan headers come from
`find_package(Vulkan)`, or from `-DZEROFG_VULKAN_HEADERS_DIR=<dir>`; with the
Android NDK toolchain, the NDK's own headers are used. The library never links
a Vulkan loader: every entry point comes from the `vkGetInstanceProcAddr` you
pass in.

```cmake
add_subdirectory(third_party/ZeroFG)
target_link_libraries(my_host PRIVATE zerofg::zerofg)
```

Or let CMake fetch it:

```cmake
include(FetchContent)
FetchContent_Declare(zerofg
  GIT_REPOSITORY https://github.com/hy300leosquizz-ctrl/ZeroFG.git
  GIT_TAG v1.2.0)
FetchContent_MakeAvailable(zerofg)
target_link_libraries(my_host PRIVATE zerofg::zerofg)
```

Built and checked on Windows (clang, llvm-mingw) and Android (NDK r29,
arm64-v8a). The presenter builds in any Android host through
[`host/portable`](host/portable/), whose README lists what a host has to do.

ZeroFG 1.2 has been runtime-validated in games on the Adreno 840 (Xendroid+,
x2 to x4); its engine is also timed on the Adreno 650 and checked against
ground truth on a Radeon Vega in the laboratory.

## By the numbers

Two months, August to October 2026: 736 commits, more than 137,000 lines
written and 64,000 rewritten away, about 64,000 lines of original code (the
engines from RC1 to Zero, the presenter, and a laboratory that tests the engine
against ground truth) and 34,000 lines of engineering notes.

## From V1 to 1.0

V1 (August 2026) was the first engine that worked: block motion, a confidence
value and a blend. It produced real frames and strong ghosting, and it had no
presenter at all. It stays in this repository's history. 1.0 replaces all of
it: a new motion estimator verified at full resolution, a temporal prior, an
exposure model, a continuity guard, a new resolve, and the presenter, measured
against ground truth in the laboratory and in play.

## What's new in 1.1

Two new engines, under the same two names:

- **Zero is now a layered engine** (RC7): surfaces, the nearest moving object
  and a screen layer over a dense-flow picture, with a judge that takes regions
  back where the layers disagree. It leads when the camera moves: on a Shadow
  of the Colossus camera pan, 36.67 dB against ReallyZero's 34.32 and LSFG
  3.1's 31.1 (the same pairs, the same ruler).
- **ReallyZero is now a dense optical flow** with a cleanup pass: the cheapest
  ZeroFG yet (0.94 ms at 1280 x 896 on the Adreno 840), and close to Zero
  wherever the camera holds still.
- **The 1.0 engine (RC3) is retired.** `Mode::kZero` and `Mode::kReallyZero`
  now create the new engines, so a 1.0 host gets them without a code change;
  `Mode::kRC7` and `Mode::kDenseFlow` name the same engines.
- **Any picture size** from 64 x 64 to 16384 x 16384, no cell grid to fit.
- **Scene cuts.** Both engines show the later frame whole across a cut, and a
  host that knows where its cuts are can tell the presenter
  (`content_break`, see [INTEGRATION.md](INTEGRATION.md#6-pace)).
- **The HUD path** (`HudPlanes`): a host that sees how the game draws its HUD
  hands it over, and it stays crisp and in place over a moving world: up to
  6 dB more on the HUD in our PS2 measurements. ARMSX2's ZeroGS mode draws it
  from the PS2's GS ([INTEGRATION.md](INTEGRATION.md#4b-the-hud-optional)).

## What's new in 1.0.4

The engine is unchanged; this release is the presenter's pacing, from the
hosts that cannot pull their game's frames
([details](host/presenter/README.md#what-changed-in-104)):

- **Phase recovery.** A game on its own clock (an emulator's frame limiter,
  not one the host can pull) no longer drifts the output calendar away from it.
  Before, at 30 -> 60 that drift showed as uneven 1-3 refresh pairs, a
  generated frame next to its real one. The presenter now measures the game's
  period from its frame times. It brings a calendar that fell behind back to a
  small cushion above the depth it needs, and drains one far ahead through the
  rate, without skipping anything.
- **Measured** on the Adreno 840 in PPSSPP (PSP), ARMSX2 (PS2) and XenDroid
  (Xbox 360):
  - a PSP game at 30 -> 60 is back to even pairs;
  - a PS2 game shows its frames about 13 ms sooner;
  - an Xbox 360 game under a 30 fps cap went from 38 % to 79-89 % of intervals
    at two refreshes.
- **Pacing tools for hosts.** Every host differs: who sets the frame clock, the
  copy, what else is on the screen. Pacing has to be measured again on each
  one. The presenter's switches are now settings a host can change after
  measuring: phase recovery, the free output, the vsync quantizer and an
  optional tight vsync reach, which is off by default. The
  [presenter README](host/presenter/README.md#tuning-a-hosts-pacing) lists them
  with how to measure.

## What's new in 1.0.3

The engine is unchanged (the same as 1.0.2); this release is for hosts:

- **The presenter is ZeroFG's own.** The ZeroFG Independent Presenter lives in
  [`host/presenter`](host/presenter/), and [`host/portable`](host/portable/)
  stands in for the few Xenia-shaped types it includes, so the presenter, its
  egress and the device handoff build unchanged in any Android host.
  XenDroid's own integration moved to [`host/xendroid`](host/xendroid/), as an
  example host.
- **A second host: ARMSX2 (PS2).** A 60 fps game shown at 120 fps on a 120 Hz
  phone, switched on and off in game. The kit's README lists what a host has
  to do that the presenter does not tell you.
- **No Vulkan SDK needed to build.** The shaders ship compiled
  (`shaders/prebuilt`); the SDK's tools are only needed to change them.
- **A host-chosen handoff publish depth** (`SetPublishDepth`, 1 by default), for
  a Source that runs ahead of its GPU.

## What's new in 1.0.2

The engine is unchanged; this release is the reference host's pacing
([details](host/presenter/README.md#what-changed-in-102)):

- **The output follows the game's real rate.** The presenter's clock follows
  the average of the game's last frames, measured without the time the game
  spent waiting on ZeroFG, and runs a little faster while frames sit deeper
  than needed. A game that alternates 33 and 50 ms frames used to leave the
  clock stuck, with about half a second of delay in Arkham City.
- **Fewer stutters when the frame rate changes.** When the clock changes pace,
  the new timing starts where the next real frame can actually go, instead of
  leaving a slot nothing fills.
- **Guest pacing in the emulator host**: a late game frame gets its vblank at
  once, and a game frame rate cap gives a GPU-bound game's frame generation
  room to work (Modern Warfare 3 at 40: the same ~80 fps on screen, 70 -> 55 ms
  of lag). A patch to XenDroid's GPU code, in `host/xendroid/xendroid_glue/`.

## What's new in 1.0.1

- **Lower latency and a steadier cadence below 30 fps**, in the presenter:
  the presentation can run a few refreshes ahead to drain accumulated delay,
  the game's wait for its own GPU no longer counts as backpressure, and the
  pre-priority operating point is off
  ([details](host/presenter/README.md#what-changed-in-101)).
- **Any picture size.** A picture the cell grid does not divide runs padded to
  the next one it does (Halo 3, 1152 x 640); a tiled picture is bit-identical
  to 1.0.
- **A build guard** in the reference host: a driver that crashes compiling
  ZeroFG's pipelines runs Compat at the next launch, and ZeroFG turns off
  after a second crash.

## Contact

Questions, integration help and collaboration: **fgzerofg@gmail.com**.

## Development and credits

ZeroFG is developed through human–AI collaboration.

- **hy300leosquizz** ([`hy300leosquizz-ctrl`](https://github.com/hy300leosquizz-ctrl)) — creator and maintainer. Lawyer by trade, Formula 1 podcast presenter by passion, and software architect by sheer determination and curiosity, developing latent talents after asking himself whether he could modify a few features in emulators and falling down the rabbit hole for months: he drew the architecture, set the direction, had an instinct trust rate of 98%, became an expert in Vulkan and in frame generation features and capabilities, made every final call, and ran every test on his own phones, usually a hot one.
- **Zeromeia** — the project name for an AI development collaborator powered by ChatGPT by OpenAI: architecture, runtime and log analysis, experiment design, code review, documentation and release preparation.
- **Zé Raio** — the project name for an AI development collaborator powered by Claude by Anthropic: implementation, the measurement laboratory, log analysis, documentation and release preparation.

AI-generated analysis, designs, code and documentation are engineering inputs. Final decisions, device testing, validation and publication stay with the human maintainer. The names Zeromeia and Zé Raio describe the project's use of ChatGPT and Claude and do not imply sponsorship or endorsement by OpenAI or Anthropic.

## License

Dual-licensed: you may use ZeroFG under the **Apache License, Version 2.0**
([`LICENSE`](LICENSE)) or under the **GNU General Public License, version 2 or
(at your option) any later version** ([`LICENSE-GPL-2.0`](LICENSE-GPL-2.0)), at
your option (`SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later`).

The GPL option lets ZeroFG go into hosts that are GPL-2.0-only, such as RPCS3
and its ports, which cannot take Apache-2.0 code. Every other host can keep
using it under Apache-2.0 exactly as before.

Unless you state otherwise, a contribution you submit for inclusion in ZeroFG
is dual-licensed the same way, with no additional terms or conditions.

Three files keep Xenia's BSD 3-Clause license, which goes with either option
([`LICENSE-Xenia`](host/xendroid/LICENSE-Xenia)): in `host/xendroid/`,
`vulkan_presenter_zerofg_device_context.inc` (adapted from Xenia's presenter)
and `xendroid_glue/vulkan_presenter_zerofg_glue.cc` (an excerpt of it); and
`host/portable/include/xenia/base/frame_stats.h`, from XenDroid's Xenia source
tree. A provenance check found no code shared with Xenia or XenDroid in any
other file.
