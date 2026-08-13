# ZeroFG

> Open-source, platform-agnostic Vulkan frame generation engine focused on mobile implementations.

**Status: Experimental — Functional V1 Proof of Concept**

ZeroFG is a standalone Vulkan frame-generation engine. This repository preserves the first functional public implementation as a focused V1 source snapshot, independent of XenDroid.

The host supplies Vulkan resources and a command buffer. ZeroFG records interpolation work into that command buffer; queue submission, swapchain ownership, presentation, synchronization, and frame pacing remain host responsibilities. V1 targets a single synthetic midpoint (`phase = 0.5`) for use in a 2× integration path.

The implementation was validated at runtime through [XenDroid-ZeroFG](https://github.com/hy300leosquizz-ctrl/XenDroid-ZeroFG), the reference integration and functional proof of concept. This standalone snapshot does not yet have an independently validated build system.

## V1 pipeline

The V1 pipeline is intentionally small:

```text
previous + current
        ↓
       luma
        ↓
  block motion
        ↓
    confidence
        ↓
midpoint warp/blend
        ↓
 synthetic frame
```

It estimates limited block motion between the previous and current images, derives a confidence value, and synthesizes the midpoint by warping and blending samples. This is a functional experiment, not a claim of production-quality interpolation.

## Integration contract

ZeroFG owns the interpolation resources and commands needed by its engine path. The integrating host remains responsible for:

- providing compatible Vulkan images and image views;
- providing the command buffer in which ZeroFG records work;
- resource state and synchronization outside the engine contract;
- queue submission, swapchain management, presentation, and frame pacing.

The public API is in `include/zerofg/zerofg.h`. The implementation and embedded SPIR-V headers are in `src/`, while the corresponding GLSL and checkpoint SPIR-V files are preserved in `shaders/`.

## Known limitations

V1 is a functional proof of concept, not a usable gameplay product. Its known limitations include:

- severe ghosting and smearing;
- a limited motion-search range and simple block estimator;
- poor disocclusion handling;
- visible edge and motion artifacts;
- significant GPU overhead for the resulting quality;
- image quality unsuitable for normal gameplay.

The public build exists to demonstrate that the V1 frame-generation path produced real synthetic frames, not to offer a practical enhancement.

## Reference V1 POC build

The [ZeroFG V1 Functional POC — Experimental](https://github.com/hy300leosquizz-ctrl/XenDroid-ZeroFG/releases/tag/v1-poc) release contains the reference XenDroid integration APK. It is a demonstration artifact and is not intended for daily use or normal gameplay.

## Building

No standalone build is claimed or documented yet. The historical in-tree CMake target depended on the XenDroid/Xenia source tree and was deliberately not promoted here. The validated integration is maintained in [XenDroid-ZeroFG](https://github.com/hy300leosquizz-ctrl/XenDroid-ZeroFG).

## Development and credits

ZeroFG is developed through human–AI collaboration.

- **hy300leosquizz** ([`hy300leosquizz-ctrl`](https://github.com/hy300leosquizz-ctrl)) — creator and project maintainer; responsible for engineering direction, integration, device and runtime testing, and final technical decisions.
- **Zeromeia** — the project name for an AI development collaborator powered by ChatGPT by OpenAI. Zeromeia is used extensively across the development pipeline, including architecture, technical and runtime analysis, algorithm and experiment design, code-generation guidance, Codex task design, code review, debugging, log analysis, documentation, repository organization, and release preparation.

AI-generated analysis, designs, code suggestions, and documentation are treated as engineering inputs. Final project decisions, device testing, validation, and publication remain under the control of the human maintainer. The name Zeromeia describes the project's use of ChatGPT and does not imply sponsorship or endorsement by OpenAI, legal personhood, copyright ownership, or independent publication authority.

## Licensing

Unless otherwise noted, the ZeroFG-specific contents of this repository are licensed under the **Apache License, Version 2.0** (`Apache-2.0`). See [`LICENSE`](LICENSE).

The license is applied without modifying the published V1 engine snapshot: the 13 V1 source/shader files remain byte-for-byte identical to the historical checkpoint used for this standalone extraction. Any third-party material added in the future remains subject to its own license and must be identified separately.
