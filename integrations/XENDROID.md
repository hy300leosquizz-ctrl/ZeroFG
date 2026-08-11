# XenDroid Reference Integration

XenDroid is the first functional host for ZeroFG and serves as the reference integration/proof of concept. The engine itself remains independent.

## Repository roles

- `hy300leosquizz-ctrl/XenDroid-ZeroFG` — integration/reference POC intended for public consumption;
- `hy300leosquizz-ctrl/XenDroid-ZeroFG-DEV` — private development, experimentation and integration repository;
- `hy300leosquizz-ctrl/ZeroFG` — independent ZeroFG engine.

The active development reference used for this extraction is `XenDroid-ZeroFG-DEV/zerofg-v2-migration`.

## Where ZeroFG enters XenDroid

The reference integration attaches ZeroFG to XenDroid's Vulkan presentation path. The in-tree ZeroFG module is linked into the Vulkan UI/presenter target and a thin `zerofg::xenia::Adapter` forwards host data into the generic `Interpolator` API.

The adapter itself performs almost no policy: it constructs `CreateInfo`, calls `Interpolator::Create`, forwards `Resize`, and forwards `Interpolate`.

Queue submission, synchronization, swapchain ownership, presentation and pacing remain in XenDroid's `VulkanPresenter`.

## Activation

ZeroFG is gated by the `zerofg_frame_generation` CVar and is off by default. The normal XenDroid presentation path must remain unchanged when the feature is disabled or unavailable.

## Lifecycle

The reference host pattern is:

1. initialize/create the ZeroFG adapter after the relevant Vulkan instance/device context exists;
2. configure three frame contexts;
3. call `Resize` when output extent/format changes;
4. retain a private completed previous color frame on the host side;
5. build `zerofg::Image` descriptors for previous, current and synthetic destination;
6. call midpoint `Interpolate` in a context that is safe to reuse;
7. submit/present through the existing XenDroid completion and presentation infrastructure.

The private previous-frame copy is a host invariant because mailbox/presenter-owned images may otherwise be recycled before ZeroFG can safely consume them.

## Presentation order and pacing

For the functional 2× path, XenDroid schedules the logical pair as:

`synthetic → real`

The presenter owns the physical cadence between those presentations. Timing telemetry, GPU timestamps, midpoint pacing, adaptive backpressure and source-rate estimation were developed in the host integration and are deliberately not part of the standalone core.

## Fallback

The real current frame is the safe path. If ZeroFG initialization/interpolation fails, or frame generation is disabled, the integration should continue through normal XenDroid presentation rather than sacrificing the real frame.

## Main integration touchpoints in XenDroid

The development history shows ZeroFG-specific integration work primarily in:

- the isolated `zerofg/` module;
- the Xenia-specific ZeroFG adapter;
- the Vulkan UI target's build wiring;
- `vulkan_presenter.cc` / `vulkan_presenter.h` for lifecycle, history, scheduling, presentation, pacing and telemetry;
- configuration/frontend paths used to expose activation or mode selection.

Only the isolated engine module was extracted as code. Presenter/frontend code remains in XenDroid.

## Reproducing the integration in another XenDroid-like host

Use a thin adapter rather than moving presenter logic into ZeroFG:

1. translate the host's Vulkan handles into `VulkanContext`;
2. maintain host-owned previous/current frame lifetime;
3. provide a distinct output image;
4. map the host's frames-in-flight/completion mechanism onto `frame_context_index` reuse;
5. let the engine record interpolation commands;
6. keep queue submission, fallback, `synth → real` policy and pacing in the host.

This boundary is the key architectural lesson of the current XenDroid POC.
