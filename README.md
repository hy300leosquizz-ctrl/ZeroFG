# ZeroFG

> A Vulkan frame generation engine for mobile GPUs. **Version 1.0.**

ZeroFG takes two frames a game rendered and creates the frame halfway between
them. Shown in between, the generated frames double the frame rate: 60 fps
becomes 120, 30 becomes 60. ZeroFG only records GPU work into a command buffer
you give it, so it can live inside any Vulkan host: an emulator, a game, a
compositor.

ZeroFG 1.0 is the engine of [XenDroid-ZeroFG](https://github.com/hy300leosquizz-ctrl/XenDroid-ZeroFG),
where it runs in play on Android at 120 frames per second.

## Two modes

| Mode | What it is | GPU time per frame, 1280 x 720 |
| --- | --- | --- |
| `Mode::kZero` | The full engine: the best image | Adreno 840: 2.2 ms · Adreno 650: 7.2 ms |
| `Mode::kReallyZero` | The same engine with half the fine passes, for weaker GPUs | Adreno 840: 1.7 ms · Adreno 650: 5.4 ms |

The times are laboratory measurements of the generation alone, on a fixed
workload. ReallyZero stays within 0.2 dB of Zero on our measurement streams.

## How it works

For every pair of frames A and B, ZeroFG:

1. **models the exposure** between them (gain, bias, correlation), so fades and
   flashes are not mistaken for motion;
2. **estimates motion on a 20-pixel cell grid**, coarse to fine, and tests a
   few global camera hypotheses;
3. **verifies every cell at full resolution** with four propagation passes
   (two in ReallyZero), seeded by the previous pair's motion as a temporal
   prior;
4. **guards continuity**: a vector that would tear the scene apart, or that the
   evidence does not support, falls back to a safe blend;
5. **resolves the midpoint frame**, warping both sources by their trust and
   rebuilding the final colour with a sharp kernel.

Static HUDs, text and overlays stay where they are instead of smearing with the
motion behind them.

## What ZeroFG does not do

ZeroFG owns its GPU resources and the commands it records. It does not own a
queue, a swapchain, presentation, frame pacing or any synchronization with your
renderer. Deciding when to generate, how to wait for the inputs and when to
show what is the host's job. [INTEGRATION.md](INTEGRATION.md) explains how.

## Requirements

- A Vulkan 1.3 device with `synchronization2` and
  `shaderStorageImageExtendedFormats` enabled.
- `R8_UNORM` storage images, `A2B10G10R10_UNORM_PACK32` storage images that can
  be blitted from, and an input format that can be sampled with linear filtering.
- A picture size that the 64-cell grid divides exactly (1280 x 720 and
  1920 x 1080 both work). Other sizes are refused, not run in a weaker mode.

Optional features the device really has enabled make it faster (half-precision
colour, 64-wide subgroups, cubic filtering). Every backend produces the same
logical result.

## Building

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This builds the static library `zerofg` (alias `zerofg::zerofg`). It needs
Python 3, plus `glslangValidator` and `spirv-opt` from the Vulkan SDK (set
`VULKAN_SDK`, or put both on `PATH`). The Vulkan headers come from
`find_package(Vulkan)`, or from `-DZEROFG_VULKAN_HEADERS_DIR=<dir>`. With the
Android NDK toolchain, the NDK's own headers are found. The library never links
a Vulkan loader: every entry point comes from the `vkGetInstanceProcAddr` you
pass in.

In your project:

```cmake
add_subdirectory(third_party/ZeroFG)
target_link_libraries(my_host PRIVATE zerofg::zerofg)
```

Built and checked on Windows (clang, llvm-mingw) and Android (NDK r29,
arm64-v8a).

## From V1 to 1.0

V1 (August 2026) was the first functional engine: block motion, a confidence
value and a midpoint blend. It produced real frames and strong ghosting. It
remains in this repository's history. 1.0 replaces it entirely: a new motion
estimator verified at full resolution, a temporal prior, an exposure model, a
continuity guard and a new resolve, measured against ground truth in a
dedicated laboratory and in play.

## Development and credits

ZeroFG is developed through human–AI collaboration.

- **hy300leosquizz** ([`hy300leosquizz-ctrl`](https://github.com/hy300leosquizz-ctrl)) — creator and project maintainer; responsible for engineering direction, integration, device and runtime testing, and final technical decisions.
- **Zeromeia** — the project name for an AI development collaborator powered by ChatGPT by OpenAI, used across the pipeline: architecture, runtime and log analysis, experiment design, code review, documentation and release preparation.
- **Zé Raio** — the project name for an AI development collaborator powered by Claude by Anthropic, used for implementation, the measurement laboratory, log analysis, documentation and release preparation.

AI-generated analysis, designs, code and documentation are treated as engineering inputs. Final project decisions, device testing, validation and publication remain under the control of the human maintainer. The names Zeromeia and Zé Raio describe the project's use of ChatGPT and Claude and do not imply sponsorship or endorsement by OpenAI or Anthropic.

## License

Licensed under the **Apache License, Version 2.0** (`Apache-2.0`). See
[`LICENSE`](LICENSE).
