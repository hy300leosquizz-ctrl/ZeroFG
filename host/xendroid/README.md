# XenDroid: an example host

XenDroid (Xbox 360, Android) was the first host of the
[ZeroFG Independent Presenter](../presenter/README.md). This folder keeps what
XenDroid adds around it, as an example of a complete integration in a
Xenia-based emulator:
[XenDroid-ZeroFG](https://github.com/hy300leosquizz-ctrl/XenDroid-ZeroFG).

| File | What it does |
| --- | --- |
| `vulkan_presenter_zerofg_device_context.inc` | Creating ZeroFG's own Vulkan device and its output pipelines |
| `vulkan_presenter_zerofg_source_adapter.inc` | Publishing each finished game frame to the presenter |
| `zerofg_xenia_adapter.h/.cc` | The thin wrapper XenDroid uses around `zerofg::Interpolator` |
| `adrenotools/kgsl-context-priority.patch` | GPU priority for ZeroFG's device on Adreno: the change to XenDroid's libadrenotools (a hook that sets the KGSL priority of the contexts a custom driver creates) and to `vulkan_device.cc`/`vulkan_instance.cc` that uses it ([README](adrenotools/README.md)) |
| `xendroid_glue/guest-pacing.patch` | Guest pacing, in XenDroid's GPU code (command processor, vblank limiter, PM4 wait): a vblank probe learns the address a game waits on for its flip, a frame already late for it gets its vblank at once (the vblank it replaces is skipped, so the game never speeds up), and the game frame rate cap holds swaps at least 1/limit apart. Both only while the presenter is live |
| `xendroid_glue/vulkan_presenter_zerofg_glue.cc` | The callbacks inside XenDroid's own presenter that record a Generation, run the normal output pipeline on a frame (the Post) and hand the surface over |

The rest of the integration lives inside XenDroid's own presenter
(`vulkan_presenter.cc`, a Xenia file). Its ZeroFG functions are copied in
`xendroid_glue/` as an excerpt: `ProcessZeroFGGeneration` and
`PollZeroFGGeneration` record and watch a generation, `ProcessZeroFGPost` runs
the normal output pipeline on a frame, and the surface functions hand the
screen over.

## License

Xenia's BSD 3-Clause license ([LICENSE-Xenia](LICENSE-Xenia)) for
`vulkan_presenter_zerofg_device_context.inc`, adapted from Xenia's presenter,
and `xendroid_glue/vulkan_presenter_zerofg_glue.cc`, an excerpt of it. The
adrenotools patch changes libadrenotools, which keeps its BSD 2-Clause license;
`xendroid_glue/guest-pacing.patch` changes Xenia files, which keep theirs. The
rest is Apache-2.0, like the rest of this repository.
