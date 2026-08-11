# Roadmap

This roadmap separates extraction work from algorithm development. It does not imply that later stages are already implemented.

## 1. Standalone extraction

**Current:** source/API extraction completed in this bootstrap.

Remaining work: establish a reproducible independent dependency/tooling path and prove the extracted source can be consumed outside XenDroid.

## 2. API cleanup

- validate the current `Create` / `Resize` / `Interpolate` contract with a second host;
- reduce any remaining assumptions inherited from the reference integration;
- formalize capabilities and format requirements without overfitting XenDroid.

## 3. Motion estimation

Evolve beyond the V1 8×8/±4 estimator. The current design direction includes preprocessing, a coarse-to-fine pyramid, confident motion propagation, residual search including a zero-motion candidate, and final-level subpixel refinement.

## 4. Synthesis quality

Improve warp quality, confidence use, temporal blending and edge behavior while preserving a safe low-confidence path.

## 5. Disocclusion handling

Add explicit detection/rejection/reconstruction strategies for newly visible regions instead of relying on simple blending.

## 6. Performance and overhead

Measure pass-level GPU cost, reduce bandwidth/temporary-resource overhead and establish predictable mobile budgets without trading correctness for headline FPS.

## 7. Pacing integration

Keep presentation scheduling host-owned, but define a cleaner timing contract so hosts can combine source cadence, synthetic-frame completion and display cadence without feedback loops.

## 8. Alternative presets

Develop the planned profiles:

- **Zero** — quality-oriented balanced target;
- **ReallyZero** — lower-cost/eco target.

These names are current design goals, not separate production-ready algorithms in the extracted core today.

## 9. Additional host integrations

Add at least one non-XenDroid Vulkan integration to prove that the core/API boundary is genuinely reusable.

## 10. Hardware validation

Validate correctness, format support, performance and quality across multiple mobile Vulkan implementations and, where useful, desktop Vulkan as a development/reference environment.
