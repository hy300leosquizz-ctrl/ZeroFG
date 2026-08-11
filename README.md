# ZeroFG

> Open-source, platform-agnostic frame generation engine focused on mobile platforms.

**Status: Experimental — Functional Proof of Concept**

ZeroFG is an experimental Vulkan Compute frame-generation project with an initial focus on mobile platforms. A functional implementation is integrated in XenDroid and has produced real intermediate frames in device workloads. The engine is not ready for general-purpose use: image quality, motion estimation, disocclusion handling, pacing integration and GPU overhead are still active development areas.

## Current implementation

The current extracted engine represents the latest ZeroFG core present in `XenDroid-ZeroFG-DEV/zerofg-v2-migration`. Its implemented MVP pipeline is intentionally small:

1. extract luma from the previous and current frames;
2. estimate block motion with an 8×8 SAD search over a ±4-pixel radius;
3. retain a simple confidence value for the selected motion vector;
4. warp previous/current color samples toward the midpoint;
5. blend warped and non-warped fallback samples according to confidence;
6. write an internal synthetic image and blit it to the host-provided output.

The public API exposes `Interpolator::Create`, `Resize` and `Interpolate`. ZeroFG records work into a host-provided Vulkan command buffer; queue submission, swapchain ownership, presentation and frame pacing remain host responsibilities.

The current MVP targets one midpoint frame (`phase == 0.5`) for a 2× presentation path. The default integration uses three independent frame-resource contexts.

## Proof of concept

The XenDroid integration is the current functional reference:

- **XenDroid-ZeroFG** — public integration/reference implementation and functional proof of concept;
- **XenDroid-ZeroFG-DEV** — private development, experimentation and integration laboratory;
- **ZeroFG** — this independent engine repository.

Runtime work in the XenDroid POC has confirmed effective synthetic-frame generation and a `synth → real` presentation path. Host-side pacing and scheduling work remains in the XenDroid integration and is deliberately not part of the extracted core.

## Known limitations

The current implementation has observed limitations:

- strong ghosting/smearing during camera motion;
- motion estimation with insufficient search range and spatial robustness;
- disocclusion and edge artifacts;
- high GPU overhead for the current quality level;
- pacing/scheduling still evolving in the host integration;
- standalone build/tooling is not yet validated;
- the current core is the reconstructed V1-quality estimator, while the more advanced V2 estimator is still planned work.

See [`docs/LIMITATIONS.md`](docs/LIMITATIONS.md) and [`docs/STATUS.md`](docs/STATUS.md) for the evidence-based project state.

## Goals

ZeroFG aims to become:

- an independent Vulkan frame-generation engine;
- mobile-first and low-overhead;
- straightforward to integrate into different Vulkan hosts;
- explicit about host/core ownership and synchronization;
- configurable through cost/quality presets such as **Zero** and **ReallyZero**;
- progressively stronger in motion estimation, synthesis, disocclusion handling and pacing interoperability.

## Repository layout

- `include/zerofg/` — public engine API;
- `src/` — current Vulkan implementation and internal helpers;
- `shaders/` — ZeroFG-owned GLSL compute shader sources;
- `docs/` — architecture, integration contract, status, limitations, roadmap and licensing/provenance notes;
- `integrations/XENDROID.md` — reference integration architecture for XenDroid.

Generated embedded SPIR-V headers retained under `src/` correspond to the included ZeroFG shader sources and are required by the currently extracted implementation. Raw `.spv` build outputs are intentionally not included.

## Building

A standalone build system is **not yet claimed to be functional**. The implementation validated so far is built through the XenDroid Android/Gradle integration. The previous in-tree CMake target depended on XenDroid/Xenia's vendored Vulkan-Headers path, so it has not been promoted as a standalone build definition here.

The standalone build task is to establish a normal Vulkan-Headers dependency and reproducible shader compilation/embedding, then validate that configuration independently. Until that happens, this repository should be treated as a faithful source extraction and API bootstrap, not as a verified standalone build.

## Documentation

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — implemented architecture versus planned work;
- [`docs/INTEGRATION.md`](docs/INTEGRATION.md) — host-agnostic Vulkan integration contract;
- [`integrations/XENDROID.md`](integrations/XENDROID.md) — current XenDroid reference integration;
- [`docs/STATUS.md`](docs/STATUS.md) — validated, implemented, planned and experimental state;
- [`docs/LIMITATIONS.md`](docs/LIMITATIONS.md) — observed limitations;
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — next development stages;
- [`docs/LICENSING.md`](docs/LICENSING.md) — licensing/provenance status.

## Licensing status

The final standalone license is **pending provenance review**. The ZeroFG-specific files extracted from the XenDroid development history do not currently carry an explicit file-level license, and no blanket license is asserted in this bootstrap. XenDroid/Xenia implementation files and unrelated third-party components are not copied into the core repository.

Until a top-level license is deliberately selected after provenance review, do not infer reuse rights merely from public repository visibility. See [`docs/LICENSING.md`](docs/LICENSING.md).

## Research references

ZeroFG development has used other frame-generation systems, including GHFG/GameScopeV2, only as research and comparative references. No proprietary GHFG/GameScopeV2 code, extracted shader blob or proprietary implementation is included in this repository.
