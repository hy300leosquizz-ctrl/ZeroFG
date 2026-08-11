# AGENTS.md

This file defines the operating rules for coding agents working on the independent ZeroFG project.

## Project boundary

ZeroFG is an independent, platform-agnostic Vulkan frame-generation engine with a mobile-first focus. Keep the engine core under `include/`, `src/` and `shaders/` free of XenDroid-specific dependencies unless a dependency is demonstrably required by the generic Vulkan contract.

XenDroid is a host and reference integration, not part of the engine. Host-specific adapters, presenter behavior and integration notes belong under `integrations/` or in the host repository.

Never copy proprietary GHFG/GameScopeV2 code, extracted shaders, binary blobs or implementation details into ZeroFG. Those systems may be discussed only as research/comparison references.

## Source of truth

Treat the code in this repository and `docs/STATUS.md` as the current standalone state. Historical XenDroid runtime evidence may be used to explain what the extracted core has demonstrated in the reference POC, but do not convert host-specific evidence into standalone validation.

When architecture, validated behavior, limitations or roadmap materially change, update the relevant documents in the same work cycle.

## Implementation discipline

- Prefer small, reversible and testable changes.
- Avoid broad refactors during algorithm experiments.
- Preserve the public host/core boundary unless a change is explicitly justified.
- Keep Vulkan requirements explicit and mobile compatibility in mind.
- Do not add queue ownership, presentation, swapchain management or frame pacing to the core merely because XenDroid currently performs those tasks.
- Preserve safe failure behavior so hosts can fall back to presenting the real frame.
- Do not claim planned motion-estimation, synthesis or disocclusion features are implemented before the code exists.

## Builds and tests

Use Codex for builds, compilation, automated tests and iterative debugging. The standalone build system is currently pending; do not invent or claim a working build path until it has actually been implemented and validated.

Use precise validation language:

- **Inspected** — static source/documentation review only.
- **Compiled** — a relevant build or compilation succeeded.
- **Runtime validated** — behavior was observed on a target host/device with evidence.

A successful static inspection is not compilation, and compilation is not runtime validation.

## Licensing and provenance

The standalone license is pending provenance review. Do not add a top-level license, third-party code, copied host implementation or new license assertions without establishing provenance and compatibility first. Preserve required notices on any future imported code.

## Completion reports

For material changes report only what matters: files changed, architectural/behavioral effect, validation actually performed, remaining uncertainty and whether project documentation was updated.
