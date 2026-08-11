# ZeroFG Architecture

## Scope

This document describes the architecture actually present in the extracted ZeroFG core. Planned work is separated explicitly from implemented behavior.

## Public boundary

The public API is `include/zerofg/zerofg.h`.

The host supplies a `VulkanContext` containing the Vulkan instance, physical device, logical device, loader entry point and optional allocation callbacks. Frames are described through lightweight `Image` records containing image/view handles, current layout, format, extent and usage flags.

`CreateInfo` also selects the number of independent frame-resource contexts. The current default is three and the implementation accepts 1–8.

### Lifecycle

1. **`Interpolator::Create`** validates the Vulkan context and creates the requested independent internal contexts.
2. **Internal initialization** occurs inside `Create`; it loads required Vulkan entry points and creates samplers, descriptor layouts/pools/sets, shader modules and compute pipelines. There is no separate public `Initialize` method in the current API.
3. **`Resize`** allocates/recreates per-context working images for the requested extent and records the input/output formats. It is intended to stay outside the hot presentation path.
4. **`Interpolate`** records one midpoint interpolation into a host-provided command buffer.
5. Destruction releases ZeroFG-owned Vulkan resources. Queue and presentation lifetime remain host-owned.

## Implemented frame pipeline

The current algorithm is the reconstructed functional V1-quality path.

### 1. Luma extraction

Both previous and current color frames are sampled and converted to `R8_UNORM` luma using Rec.709-style weights. Each frame context owns separate previous/current luma images.

### 2. Motion estimation

Motion is estimated on 8×8 blocks. The current shader searches integer offsets in a ±4-pixel radius and evaluates SAD using a 4×4 sample grid inside each block. The best and second-best costs produce a simple confidence term.

The motion surface is `R32G32B32A32_SFLOAT` at block resolution and stores motion XY, best cost and confidence.

### 3. Synthesis

The synthesis pass reads previous/current color, the block motion field and `phase`. It warps the two source positions toward the requested temporal point, blends them, and mixes that result with a non-warped interpolation according to confidence.

The current public path accepts only `phase == 0.5` (within a small tolerance), corresponding to one synthetic midpoint frame.

### 4. Output

Synthesis writes a full-resolution `R16G16B16A16_SFLOAT` internal image. The implementation then blits that image to the host-provided output image and restores the host-described layouts.

Because the current output conversion uses `vkCmdBlitImage`, the supplied command buffer/queue path must support the required graphics/transfer operation as documented by the public API.

## Resource ownership

### ZeroFG owns

- luma working images;
- block-motion image;
- synthetic working image;
- associated device memory and image views;
- samplers;
- descriptor layouts, pools and sets;
- shader modules and compute pipelines;
- independent copies of those resources for each frame context.

### Host owns

- previous and current color frames;
- synthetic destination image;
- Vulkan instance/device/physical device;
- command-buffer allocation and recording scope;
- queue submission and completion tracking;
- swapchain and presentation;
- frame history selection/retention;
- pacing and scheduling.

ZeroFG does not currently own a persistent full-color previous-frame history. The reference XenDroid integration maintains a private completed previous frame on the host side and supplies it to the engine.

## Synchronization

ZeroFG records barriers needed for its internal sequence and temporarily transitions host images from the layouts supplied in `Image`. Inputs and output are restored to those described layouts before `Interpolate` returns.

The host remains responsible for external synchronization, submission order and resource lifetime. A `frame_context_index` must not be reused until the GPU submission containing the prior call using that index has completed.

## Failure and fallback

The core reports `kInvalidArgument`, `kUnsupported`, `kOutOfMemory` or `kVulkanError` where appropriate. The host should treat failure as a reason to skip the synthetic frame and preserve the real-frame path.

There is also a shader-level quality fallback: low-confidence motion causes synthesis to favor a plain previous/current blend instead of the warped result. This is distinct from the host-level failure fallback.

## Planned architecture — not implemented

The canonical development plan includes stronger preprocessing, a coarse-to-fine motion pyramid, confident propagation, residual search, subpixel refinement, bidirectional reasoning and improved disocclusion handling. These are roadmap items, not capabilities of the extracted core today.

Likewise, `Zero` and `ReallyZero` are planned quality/cost profiles; the standalone core does not yet expose distinct production-ready preset behaviors.
