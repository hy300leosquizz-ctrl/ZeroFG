# Host Integration

ZeroFG is designed to record frame-generation work into resources supplied by an existing Vulkan host. This document describes the current generic contract without depending conceptually on XenDroid.

## Host prerequisites

The host provides:

- a valid `VkInstance`, `VkPhysicalDevice` and `VkDevice`;
- the loader-compatible `vkGetInstanceProcAddr` entry point used by the same Vulkan stack as the host;
- optional `VkAllocationCallbacks` when required;
- a command buffer suitable for ZeroFG's compute work and current blit-based output path;
- previous and current input images;
- a distinct synthetic output image;
- synchronization that keeps all supplied resources alive and unavailable for unsafe reuse until GPU completion.

The current implementation requires Vulkan image operations/formats supported by the selected physical device. It does not currently advertise a broad portable capability profile beyond the checks performed by `Resize` and `Interpolate`.

## Create

Construct `CreateInfo`, fill `create_info.vulkan`, select `frame_context_count` and call:

```cpp
auto fg = zerofg::Interpolator::Create(create_info, &status);
```

The implementation initializes Vulkan dispatch, descriptors and pipelines inside `Create`. Failure returns `nullptr` plus an optional status.

Three frame contexts are the current reference configuration. Each is an independent set of internal GPU resources.

## Resize

Call `Resize(width, height, input_format, output_format)` after creation and whenever relevant extent or formats change.

The current output path accepts these formats:

- `VK_FORMAT_R8G8B8A8_UNORM` / `SRGB`;
- `VK_FORMAT_B8G8R8A8_UNORM` / `SRGB`;
- `VK_FORMAT_A2B10G10R10_UNORM_PACK32`;
- `VK_FORMAT_A2R10G10B10_UNORM_PACK32`;
- `VK_FORMAT_R16G16B16A16_SFLOAT`.

The device must support blitting from the internal `R16G16B16A16_SFLOAT` synthetic image to the selected output format. Unsupported combinations return `kUnsupported`.

Keep `Resize` outside the hot frame path when possible because it reallocates working resources for every frame context.

## Per-frame inputs

For `previous`, `current` and `output`, populate:

- `VkImage`;
- `VkImageView`;
- current `VkImageLayout`;
- `VkFormat`;
- width/height;
- image usage flags.

The two inputs must have `VK_IMAGE_USAGE_SAMPLED_BIT`. The output must be distinct from both inputs and have `VK_IMAGE_USAGE_TRANSFER_DST_BIT`. `VK_IMAGE_LAYOUT_UNDEFINED` is rejected for supplied host images.

The current MVP requires the input/output dimensions and formats to match those last passed to `Resize`.

## Interpolate

Record one synthetic midpoint frame with:

```cpp
status = fg->Interpolate(command_buffer,
                         frame_context_index,
                         previous,
                         current,
                         0.5f,
                         output);
```

Only `phase == 0.5` is currently implemented. Other temporal phases return `kUnsupported`.

ZeroFG records luma, motion, synthesis, internal barriers and the final blit into the command buffer. The output contains the synthetic frame after the host submits the command buffer and the GPU completes it.

## Frame-context synchronization

A frame-context index selects a private set of ZeroFG working resources. Do not reuse an index until the prior GPU submission that used it has completed. The host should map this onto its existing frames-in-flight or completion-timeline mechanism.

## Host responsibilities

The host remains responsible for:

- retaining/choosing the previous completed frame;
- selecting the current frame;
- allocating/owning the destination image;
- command-buffer begin/end;
- queue submission and completion synchronization;
- swapchain acquisition/presentation;
- ordering synthetic versus real frames;
- pacing, backpressure and target cadence;
- handling resize/surface changes;
- preserving the real frame when ZeroFG cannot run.

These responsibilities intentionally remain outside the engine so ZeroFG can be reused by different Vulkan hosts.

## Failure handling

Treat any non-`kSuccess` result as a skipped synthetic frame unless the host has a stronger recovery policy. A robust integration should always retain a safe path to present the real current frame.

Do not convert a ZeroFG failure into a broken presentation path merely to preserve frame-generation cadence.

## Adapter recommendation

Hosts with complex presenter types should use a thin adapter that translates native image/device structures into `zerofg::VulkanContext` and `zerofg::Image`. Keep queue/presenter/pacing logic in the host rather than growing host-specific dependencies in the core.

See `integrations/XENDROID.md` for the current reference pattern.
