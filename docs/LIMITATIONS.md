# Current Limitations

This list records limitations observed in the present ZeroFG/XenDroid proof of concept or directly imposed by the current extracted implementation. It intentionally avoids speculative defects.

## Image quality

- Camera motion can produce strong ghosting and smearing.
- The 8×8 block estimator with a ±4-pixel search range is too limited for larger or more complex motion.
- Block-level vectors are spatially coarse and can fail around edges and independently moving objects.
- Disocclusion is not explicitly reconstructed; newly exposed regions can artifact.
- Confidence is derived only from best-versus-second-best SAD cost and is not a complete reliability model.
- The synthesis fallback is a plain previous/current blend, so uncertain motion can still appear blurred.

## Performance

Reference XenDroid measurements for the current generation path observed synthetic GPU work around 8.3–8.9 ms in the measured workloads, with roughly 7.8–8.2 ms attributable to the synthetic path beyond the real presentation command buffer. These values are workload/device evidence, not a universal benchmark.

Earlier V1 testing also showed significant cost when source content was already near 60 FPS. No standalone performance claim is made.

## Pacing

Pacing and scheduling are currently host responsibilities and remain under active development in the XenDroid integration. A prior adaptive controller demonstrated that self-imposed waits can contaminate source-rate estimation; a correction exists in the host history but was still awaiting equivalent device retest at the extraction checkpoint.

The core itself does not schedule presentation times.

## API and portability

- Only midpoint interpolation (`phase == 0.5`) is supported by the current public path.
- The output path uses `vkCmdBlitImage`, adding format-feature and queue-capability requirements.
- Input/output images must already be valid host resources with explicit non-undefined layouts.
- The host must prevent premature frame-context reuse.
- No standalone build/toolchain has yet been validated.
- The current embedded SPIR-V headers should eventually be generated reproducibly from the GLSL sources by the standalone build.

## Validation scope

The functional proof of concept is runtime validated through XenDroid, not through an independent ZeroFG sample host. This repository has only static extraction/organization validation so far.
