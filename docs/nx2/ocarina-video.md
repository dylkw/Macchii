# Ocarina video replacement experiment

The immediate acceptance target is to reach controllable gameplay after the opening video,
then sustain 30 fps at normal emulation speed. Video playback alone does not satisfy either
target. The original 60 fps gameplay goal remains a later milestone.

`EDEN_NX2_ZELDA_VIDEO=spatial` enables a deliberately approximate replacement for the captured
1080p neural video enhancement in title `0400486024DEC000`. Unset the variable to retain native
processing. This is an opt-in development experiment, not an unconditional emulation fix or a
guarantee of 60 fps. The experiment switch must not merge to nx2.

The captured chain has thirteen compute dispatches after AV1 YUV-to-RGBA conversion. Twelve
produce a same-size RGB10A2 image; the thirteenth contains another neural upscale stage and samples
a noise texture. The experiment copies decoded RGBA8 colour into the same-size RGB10A2 image at the
original enhancement output dispatch, then bilinearly scales that image into the final
2560x1440 RGBA8 image at the original upscale dispatch. It leaves decoding and subsequent game/UI rendering in place and
omits the neural network's intermediate feature-buffer and image writes. Sharpening, denoising,
upscale detail, dithering and fine colour values can differ from native processing.

Admission requires the captured title, dispatch shapes, native program hashes and source lengths,
QMD allocation layout, constant-buffer records, shared completion parent, and exact image format,
dimensions, mip/layer and component contracts. All thirteen command and constant records are checked
before the first copy. Missing or different records retain the original path before admission.
The existing QMD scheduler still executes each dispatch's release callbacks and dependent actions.

After admission, participating dispatches must match the saved work and QMD address/ordinal.
Future constants are patched by the SDK, so preflight constants are not immutable execution
bindings. The skipped neural stages' weights and parameters are intentionally omitted by this
approximation. Each endpoint's current constant-buffer handle is resolved at its actual dispatch:
pass 11 uses offsets 0x70/0x78, and pass 12 uses 0xa0/0xc8. Normalized pass-12 input must equal
pass-11 output, including its address. Copies occur at the original image-producing dispatches,
so a decoded source need not survive beyond its original consumer. Unrelated dispatches retain
native execution. Mutable QMD completion fields are excluded from the work comparison. A changed participating dispatch
throws and fails the experiment; there is no automatic recovery that runs a native tail with
missing intermediates. Logs distinguish admitted evaluations from completed evaluations.

These guards identify a captured workload. They do not prove a stable lifetime for every guest
resource or establish that intermediate buffers have no other consumers in every possible scene.
An evaluation-level recovery design and wider scene/version validation are required before
considering production use. RenderDoc replay was unavailable on the tested Mac; local sampled
frame dumps, the live server, GPU timestamp traces and a one-second CPU sample supplied evidence.

Related upstream work: [Eglantine PR #614](https://github.com/Djihads80/Eglantine/pull/614)
fixes image selection for a gameplay upscale stand-in. Its diff does not implement this same-size
video enhancement replacement. Existing Fire Emblem/Star Fox contracts also do not match it.

The contract tests cover incompatible surface descriptors and the distinction between QMD work
and completion metadata. They do not validate visual equivalence or the resource-lifetime
assumptions above. Performance measurements must use the same binary and settings, no concurrent
compilation, and tracing disabled for both sides; GPU traces can change timing.

## Metal uniform-buffer diagnostic

An SDK MoltenVK 1.4.2 run with `MTL_DEBUG_LAYER=1` stopped in the video at 114 seconds:
`c0[0]` declared 65536 bytes, but its streamed buffer offset left only 47360 bytes in the
134217728-byte backing allocation. The MoltenVK streaming path now reserves at least 64 KiB,
zeros its tail and returns only the requested writable span. Descriptor ranges remain unchanged.
This fixes that physical allocation boundary; cached and null-buffer paths are not covered.
The additional ring occupancy and zeroing require fresh performance validation.

A later scene test exposed the same declaration-size requirement on cached buffers: a 64 KiB
allocation bound at offset 19200 left 46336 bytes for a 65536-byte declaration. MoltenVK cache
buffers now allocate and clear an additional 64 KiB beyond their logical guest extent. Buffer
cache ranges and copies retain their original sizes. The fallback null buffer also has uniform,
storage and texel usages, a nonzero descriptor range, and a 64 KiB allocation on MoltenVK.

The earlier MoltenVK 1.4.1 GUI and fixed-window CLI tests separately lost their Vulkan device
near the first scene load (roughly 212–214 seconds). The allocation diagnostic alone does not
establish that device loss's cause. Tests must distinguish the video improvement, this validation
failure, and successful gameplay rendering.

## Additional macOS validation fixes

The captured gameplay fragment program needs 17 samplers. MoltenVK push descriptors lower to
Metal's 16-slot direct sampler interface. Following the workaround described in
[Eden issue #495](https://github.com/eden-emulator/Issue-Reports/issues/495), affected pipeline
layouts use ordinary sets; other pipelines retain push descriptors. The tested SDK driver
advertises 16 ordinary samplers but 500000 update-after-bind samplers, so the affected layout
and its sampler bindings use update-after-bind, with separately flagged descriptor pool banks.
The queried sampled-image feature is preserved only on MoltenVK. Descriptor-buffer selection is
excluded for that layout. Guest descriptor-set lifetime/reuse ordering remains unchanged.

A fresh MoltenVK driver cache was necessary after changing layouts: the existing cache continued
returning the previously generated direct-binding shader. A new MoltenVK driver-cache filename
keeps the guest shader/environment cache and the old driver cache intact.

Khronos validation additionally identified missing `VK_KHR_portability_subset` enablement and
an attachmentless framebuffer requesting 32768x32768 against device limits of 16384x16384.
The advertised portability extension and its queried features are now enabled. Framebuffer
extent selection starts from the device's maximum extent, then retains the existing attachment
and render-area clamps. These are validation corrections; reaching gameplay and measuring its
performance remain separate acceptance checks.

The screenshot path now keeps its capture frame as a renderer member, rather than destroying
the image immediately after readback. `DrawToFrame` creates and resizes that frame, retaining
its transfer-source usage and existing idle wait on resize. This avoided a Metal validation
assertion on a temporary 640x360 BGRA8 capture target. Captures at 640x360 and 800x450 passed
without capture-specific Vulkan or Metal validation errors. This diagnostic correction does
not establish the lifetime of unrelated gameplay resources.

The first scene also uses task/mesh programs. The tested release drivers do not expose the
required features, so those pipelines are skipped. A separate local driver experiment uses
[MoltenVK PR #2777](https://github.com/KhronosGroup/MoltenVK/pull/2777), pinned at
`9525fe13719f02de71bbfc70233d9677d8e8adae`, with the existing task path exposed only by a local
test switch. Upstream advertises taskless meshes; enabling the task feature is not conformance
evidence. The image-view lifetime correction comes from
[MoltenVK PR #2887](https://github.com/KhronosGroup/MoltenVK/pull/2887), pinned at
`f2fa686edc71c93c44ca1d91b38354d9ddc5a919`. These driver changes are isolated under the test
build directory and are not installed system-wide.

That driver reached further into scene rendering, where validation caught the cached uniform
boundary above and a null 1D view bound to 2D/3D shader declarations. Sampled null descriptors
now use separate 1D, 2D/cube and 3D backing images, cleared to zero and transitioned to GENERAL.
Scene rendering and gameplay performance still require validation after these corrections.

## Scene synchronization follow-up

The macOS candidate incorporates the scheduler-owner and back-idle synchronization changes from
[Eglantine PR #639](https://github.com/Djihads80/Eglantine/pull/639), commit
`df92fc6c95461d2fc07ab6357301e5a212e0e03f`, through the upstream merge. It also ports
[PR #654](https://github.com/Djihads80/Eglantine/pull/654), commit
`72ca8e7244fdbff81d8902a2b81e5b908e56815e`: forced fence waits on the back thread submit deferred
work first and disable deferral until the wait returns. These address specific scheduler races
and a deferred-submission deadlock; they are not yet evidence that the macOS scene stall is fixed.
The opt-in GPU workload trace now includes direct task/mesh draws as well as ordinary draws.

An allocation-stack investigation also identified substantial ARM64 CPU recompiler metadata:
each emitted block's small relocation and fastmem-patch maps used full hash-table allocations.
Those per-block maps now use sorted flat maps. Block metadata is moved into its cache, and
emission returns only the entry pointer; linking also avoids copying relocation lists.
The existing ordinary ARM64 instruction, memory-operation and invalidation tests passed
438 assertions in 85 cases. An explicitly selected hidden random-instruction test (`A64: rand2`)
aborted; its baseline behavior has not been established. At the same intro point, live malloc
allocations fell from about 5.8 GiB to 2.9 GiB, and process footprint from 12.2 GiB to 9.3 GiB.
The first-scene sample fell from 17.9 GiB to 14.0 GiB. Intro playback remained near 60 fps, but
the scene still stalled at 0–0.2 fps; the memory reduction does not establish gameplay progress.

The scene test also coincided with a WindowServer watchdog termination on November 5 at
21:39:22 local time. Its report records 40 seconds without a successful check-in. The watchdog
stackshot identifies the test's already-terminated `eden-cli` process blocked in IOGPU/AGX kernel
cleanup. Earlier live samples show Metal command-buffer allocation waiting for a queue slot and
the emulator's GPU back thread waiting for a timeline value. These establish a host graphics
hang during the failed run, but do not identify the offending command or prove memory pressure
as its cause. Killing the process did not promptly release the driver resources.

Further gameplay launches were suspended while investigating that failure. An isolated MoltenVK
experiment can leave its queue-wide residency set disabled, selecting the driver's existing
per-encoder resource tracking. This follows the lead in
[MoltenVK issue #2890](https://github.com/KhronosGroup/MoltenVK/issues/2890); the reported workload
and memory thresholds differ from this test, so it remains an unproven candidate. With Metal
validation, small standalone transfer and descriptor-backed compute checks passed in both modes:
eight timeline submissions, 256 KiB per submission, full readback comparison and clean teardown.
These checks establish basic operation only, not Zelda correctness or driver stability under
the game's workload. The candidate is workspace-local and has not been installed system-wide.

The subsequent normal-speed scene test with residency disabled still stalled: logged frame rate
fell to 1.5 fps at 202 seconds, then 0 fps. The worker/back-thread sample again showed Metal
command-buffer allocation and timeline waits. Disabling residency alone is therefore rejected
as the scene-transition fix. That bounded run exited normally; no new WindowServer report was
observed afterward.

Half-second taps and five-second holds of Plus, X and B during the movie left it playing with
no visible skip prompt in the captured states. No intro-skip shortcut is confirmed. Earlier menu
inputs reduced time to the failing transition by roughly 25 seconds. The test harness now checks
the live presented-frame count for stalls, because buffered console FPS output delayed its first
log-based watchdog. Controllable gameplay and the 30 fps milestone remain unachieved.
