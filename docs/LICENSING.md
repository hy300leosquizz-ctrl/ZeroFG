# Licensing and Provenance Status

## Current status

A top-level `LICENSE` is intentionally **not** included in this bootstrap.

The ZeroFG-specific core, Vulkan helpers and GLSL shaders extracted from the development lineage do not carry an explicit file-level license notice in their current source form, and the source repository does not provide enough project-level evidence to assign a new standalone license safely without a deliberate provenance decision.

Public repository visibility does not itself grant reuse rights. Until licensing is resolved, do not infer permissions beyond those explicitly established by the relevant copyright holders.

## What was extracted

The standalone repository contains only the isolated ZeroFG API/implementation, its Vulkan helpers, its own GLSL shader sources, and generated C++ SPIR-V arrays corresponding to those shader sources.

The copied ZeroFG source blobs are preserved byte-for-byte from the current `XenDroid-ZeroFG-DEV/zerofg-v2-migration` state.

## What was not copied

- XenDroid/Xenia presenter or emulator implementation;
- SGSR code from the predecessor branch;
- Qualcomm shader/source material associated with SGSR;
- unrelated XenDroid frontend/build infrastructure;
- GHFG/GameScopeV2 proprietary code;
- extracted GHFG/GameScopeV2 shaders or binary blobs;
- raw runtime dumps, APKs, logs or laboratory artifacts.

Xenia source files inspected for the integration contain their own BSD notice; they remain in the XenDroid/Xenia codebase and are not part of this core extraction.

## Generated shader headers

`src/luma_spv.h`, `src/motion_spv.h` and `src/synth_spv.h` are compiler-generated C++ representations of the ZeroFG GLSL sources included under `shaders/`. They are retained because the current implementation consumes embedded arrays directly. Raw `.spv` build outputs were not copied.

A future standalone build should regenerate these headers reproducibly from the source shaders instead of treating generated arrays as the long-term authoring format.

## Before selecting a standalone license

1. confirm authorship/provenance of each ZeroFG core/helper/shader file;
2. verify that no copied fragment carries an upstream license obligation not currently visible at file level;
3. identify the intended copyright holder(s);
4. choose an open-source license compatible with all retained dependencies and provenance;
5. add any required file-level notices and the top-level license in one explicit licensing change.

Until that review is complete, the repository should not make a stronger legal claim than its evidence supports.
