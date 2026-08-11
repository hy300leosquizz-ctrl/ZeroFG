# Project Status

**Project status: Experimental — Functional Proof of Concept**

This repository is a static extraction of the ZeroFG engine from the current XenDroid development lineage. Standalone compilation/runtime validation has not yet been performed.

## Validated in the XenDroid reference POC

The existing XenDroid integration has runtime evidence that:

- ZeroFG produces actual synthetic intermediate frames on target-device workloads;
- the 2× integration can present `synth → real` pairs;
- the reconstructed core has operated with three independent frame contexts;
- recorded validation rounds produced matching synthetic/real counts without the slot-reuse mechanism becoming the observed bottleneck;
- the current V1-quality algorithm exhibits substantial smear/ghosting and nontrivial GPU cost.

These are **reference-host runtime results**. They are not standalone-repository runtime validation.

## Implemented in the extracted core

- `Interpolator::Create`, `Resize` and `Interpolate`;
- host-provided Vulkan loader/device contract;
- 1–8 independent internal frame contexts, default 3;
- per-frame luma extraction;
- 8×8 SAD block motion with ±4-pixel integer search;
- best/second-best confidence estimate;
- midpoint warp/blend synthesis;
- confidence-weighted non-warped synthesis fallback;
- internal image allocation, descriptors, compute pipelines and barriers;
- synthetic `RGBA16F` working image and blit to host output;
- argument, usage, layout, extent, format and capability checks;
- explicit status/failure reporting.

## Implemented in XenDroid but not part of the core

- private host-owned previous-frame history;
- activation/gating in the Vulkan presenter;
- synthetic/real presentation order;
- queue submission and GPU completion tracking;
- pacing, backpressure and timing telemetry;
- Android/frontend configuration.

## Implemented but not fully validated

The latest XenDroid development history includes a host-side pacing-source estimate fix (`959b5b23`) that was published in the canonical record but had not yet completed the same build/device retest at the time of extraction. It is not engine-core code and is not copied here.

The standalone source organization in this repository is statically inspected only. No independent build system is currently claimed.

## Planned

- reproducible standalone build and shader-generation pipeline;
- normal standalone Vulkan-Headers dependency;
- public API cleanup after first external-host integration;
- stronger coarse-to-fine motion estimation;
- subpixel refinement and better confidence/rejection;
- explicit disocclusion handling;
- synthesis-quality improvements;
- lower GPU overhead;
- clearer pacing interoperability contract;
- `Zero` and `ReallyZero` cost/quality presets;
- additional Vulkan host integrations and broader hardware validation.

## Experimental

Motion-estimator V2 design and pacing-controller work remain experimental until separately implemented and validated. Research comparisons with GHFG/GameScopeV2 are not part of the ZeroFG implementation.

## Extraction provenance

The code snapshot was taken from `hy300leosquizz-ctrl/XenDroid-ZeroFG-DEV`, branch `zerofg-v2-migration`. The extracted core files are preserved byte-for-byte from that source state; raw build `.spv` outputs were not carried over, while the generated embedded SPIR-V headers required by the current C++ implementation were retained alongside their GLSL sources.
