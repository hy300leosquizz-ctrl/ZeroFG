# GPU priority for ZeroFG's device on Adreno (KGSL)

When a game saturates the GPU, ZeroFG's work (generation, post-processing,
the copy to the screen) waits behind the game's frames and completes in
clumps. On Adreno the kernel driver, KGSL, keeps one queue per priority level
and the GPU firmware preempts a lower level for a higher one; any process may
ask for a priority when it creates a GPU context. Turnip's KGSL backend
creates every context without one (`kgsl_submitqueue_new` in
`tu_knl_kgsl.cc`), so the game and ZeroFG share the default level 8 and
`VK_KHR_global_priority` has no effect.

`kgsl-context-priority.patch` (against XenDroid's copy of libadrenotools, plus
the two XenDroid files that use it) adds `ADRENOTOOLS_DRIVER_CONTEXT_PRIORITY`:

- a hook library, `libkgsl_priority_hook.so`, is loaded global into the
  driver's linker namespace, as adrenotools' `fopen` redirect is, so the
  driver's `ioctl` calls go through it;
- while `adrenotools_set_context_priority(p)` holds a value, the hook writes it
  into the priority field of `IOCTL_KGSL_DRAWCTXT_CREATE`; every other ioctl
  passes through untouched;
- `adrenotools_context_priority_raised()` counts the contexts created that
  way, so the host can log that it worked.

XenDroid-ZeroFG sets priority 4 (the second of four levels: above the game,
below the top level) around the `vkCreateDevice` of ZeroFG's device and resets
it right after. The same `vkCreateDevice` also asks for
`VK_QUEUE_GLOBAL_PRIORITY_HIGH` through `VK_KHR_global_priority` (or the EXT)
when the driver offers it, and retries without it if the driver refuses: that
covers drivers that honour the standard request, and the hook covers Turnip,
which does not. The patch carries both changes to `vulkan_device.cc`. Measured on an Adreno 840 (Snapdragon 8 Elite Gen 5) in Batman:
Arkham City with the GPU 95-99% busy: the copy to the screen went from 40-170
ms to about 1 ms, presents stopped blocking, and every generated frame reached
the screen, with the game at about 23 fps instead of 24-29 in the earlier,
thermally different runs.

It only applies to drivers loaded through adrenotools (custom drivers). The
patch keeps libadrenotools' BSD 2-Clause license.
