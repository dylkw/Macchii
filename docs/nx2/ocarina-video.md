# Ocarina video replacement experiment

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

The earlier MoltenVK 1.4.1 GUI and fixed-window CLI tests separately lost their Vulkan device
near the first scene load (roughly 212–214 seconds). The allocation diagnostic alone does not
establish that device loss's cause. Tests must distinguish the video improvement, this validation
failure, and successful gameplay rendering.
