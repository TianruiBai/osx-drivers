# Wii U Graphics Driver Investigation

## Goal

Provide a usable Wii U graphics driver for Mac OS X with an end state that includes OpenGL support, not just a scanout framebuffer.

This note captures what is already present in the repo, what the OS expects from a graphics stack, what the Wii U GX2 hardware appears to provide, and what the recommended implementation path should be.

## Current Repo State

### What exists

- `WiiGraphics/src/Cafe/WiiCafeFB.cpp` implements a Wii U `IOFramebuffer` for `NTDOY,gx2` and now performs explicit D1 scanout programming during controller enable and mode changes.
- `WiiGraphics/src/Cafe/GX2Regs.hpp` now covers the D1 registers needed for the current scanout path, including CRTC control, blanking, graphics update/flip state, and high surface addresses.
- The current framebuffer path exposes one 1280x720 display mode with 32-bit, 16-bit, and 8-bit pixel depths.
- The current framebuffer path already supports hardware cursor upload/positioning plus CLUT and gamma LUT programming.
- `include/WiiCommandProcessor.hpp`, `include/WiiGXFifo.hpp`, and `include/WiiPixelEngine.hpp` already describe command-processor, FIFO, and pixel-engine register blocks.
- `WiiPlatform/src/Interrupts/WiiInterruptController.cpp` and `include/WiiProcessorInterface.hpp` already know about PI interrupt vectors for the command processor and pixel engine.

### What is missing

- No mode table beyond one hardcoded 1280x720 mode.
- No full timing-register programming for the display pipe yet.
- No vblank-safe synchronization around scanout reprogramming.
- No complete self-test path beyond the current debug first-light pattern.
- No GX2 command submission path.
- No accelerator service associated with the framebuffer.
- No user-space renderer bundle for OpenGL.
- No documented memory-management strategy for GPU-visible surfaces.

## Current Driver Capabilities

The current `WiiCafeFB` implementation is no longer just a placeholder. As it stands now, it can already provide the following capabilities:

- map the GX2 MMIO aperture and framebuffer memory exposed by the provider,
- expose the framebuffer through the system aperture expected by `IOFramebuffer`,
- advertise one display mode at 1280x720 and a nominal 60 Hz refresh,
- switch between 32-bit direct, 16-bit direct, and 8-bit indexed pixel formats,
- explicitly program D1 primary/secondary surface addresses, pitch, viewport start/end, graphics enable state, and endian swap mode,
- poll D1 graphics update state with bounded timeout logging after scanout programming,
- blank and unblank the display during scanout reconfiguration,
- fill a one-shot debug test pattern directly into the framebuffer for first-light validation when the framebuffer debug boot argument is enabled,
- upload and position a 32x32 hardware cursor,
- accept CLUT and gamma tables and load the hardware LUT,
- preserve a simple startup mode path compatible with the existing OS X graphics stack.

These are meaningful framebuffer capabilities, but they still stop well short of acceleration.

## Current Driver Limits

The current Cafe graphics driver still has major limitations that should frame the next development steps:

- only one display mode is exposed,
- display timing programming is still incomplete,
- there is no page-flip or fence abstraction,
- there is no interrupt-backed synchronization,
- there is no GPU command processor bring-up,
- there is no blit, copy, or render path,
- there is no OpenGL accelerator pairing or renderer bundle,
- runtime validation still depends on manual deploy and feedback from real Wii U hardware.

## Current Debug Bring-Up Aid

The current driver now includes a simple first-light aid for manual Wii U testing:

- with `-wiifbdbg` enabled, the first controller enable will log extra scanout state,
- the driver will write a visible test pattern into the mapped framebuffer once,
- the driver will also log the D1 graphics update state if scanout update completion times out.

This is not a complete self-test, but it is enough to separate "scanout path is dead" from "higher layers did not draw anything" during early hardware validation.

## Build And Validation Workflow

For this project, new Wii U graphics driver builds should be produced inside the Tiger QEMU guest documented in `doc/target_qemu_device.md`.

Practical workflow:

- compile the kext in the verified Mac OS X 10.4.11 QEMU environment,
- move artifacts with `scp` using legacy `ssh-rsa` compatibility options,
- deploy to the real Wii U manually,
- use manual test feedback from the Wii U to decide the next driver change.

This matters because the local host workflow does not currently provide a complete, reproducible build path for these Tiger/PPC kexts.

## Verified External Findings

### Wii U / GX2 hardware

- WiiUBrew documents GX2 at `0x0C200000`, 32-bit big-endian MMIO, as a Radeon R7xx-like GPU.
- WiiUBrew also documents that Cafe OS programs the D1 and D2 display blocks directly using register writes.
- The documented D1 register usage lines up with the existing `WiiCafeFB` code and with older AMD RV630 and RS780 display-engine documentation.
- GX2 is not exposed as a normal PCI device and there is no known usable AtomBIOS path.

### macOS graphics stack

- Apple IOKit documentation for the Graphics family states that `IOFramebuffer` is the base kernel object for scanout and display control.
- The same Apple documentation states that graphics acceleration is supplied by modules loaded in user address space, with hardware-specific code split between user space and a kernel driver.
- Apple OpenGL documentation confirms that the system can choose either hardware or software renderers. Applications can explicitly disable software fallback with the `NoRecovery` pixel-format attribute, which implies that fallback normally exists.
- Apple OpenGL documentation also exposes runtime checks for whether vertex and fragment processing are happening on the GPU or on the CPU.

### IOGraphics implementation hints

Even though the public Apple open-source `IOGraphics` repository is newer than Tiger, it still exposes the relevant service topology:

- `IOAccelFindAccelerator(framebuffer, ...)` exists in `IOGraphicsInterface.h`.
- `kIOFramebufferOpenGLIndexKey` exists in `IOGraphicsTypesPrivate.h`.
- `kIOAccelIndexKey` and `kIOAccelTypesKey` exist in `IOGraphicsInterfaceTypes.h`.
- `IOFramebufferPrivate.h` contains `assignGLIndex()` and `probeAccelerator()` hooks.
- `kIOMessageGraphicsProbeAccelerator` exists as a framebuffer-to-graphics-system message.

These points strongly indicate that a real OpenGL-capable solution needs more than a framebuffer. The OS expects a paired accelerator path that can be discovered from the framebuffer service.

## What OpenGL Support Means On This Project

There are two distinct milestones:

### 1. OpenGL availability

This is the point where OpenGL contexts can be created and the system can fall back to a software renderer if hardware acceleration is unavailable.

This milestone is useful because it proves:

- the display path is stable,
- the framebuffer is correctly integrated with Quartz and IOGraphics,
- the target machine can run OpenGL applications at all.

It is not the final goal.

### 2. OpenGL hardware acceleration

This is the final target implied by "usable graphics driver" for Wii U. It requires:

- a robust `IOFramebuffer`,
- a paired accelerator service discoverable by IOGraphics,
- GPU-visible surface and synchronization management,
- a user-space OpenGL renderer bundle that targets GX2,
- a real GX2 command submission path.

Without those pieces, the result is only a display driver plus whatever software OpenGL the OS can provide.

## Main Constraint: Do Not Start With Radeon Reuse As The Primary Plan

The temptation is to reuse an existing Radeon path because GX2 is R7xx-like. That helps as a register reference, but not as a direct driver strategy.

### Why Radeon reuse is not the primary plan

- GX2 is not on PCI.
- The usual AtomBIOS path is missing.
- Memory appears to be shared system memory, not dedicated VRAM in the normal desktop Radeon sense.
- The existing repo already leans toward direct-register control, not PCI emulation.
- The old PowerPC Mac OS X graphics stack is itself specialized and not a drop-in fit for Linux Radeon code.

### What can still be reused

- AMD display-engine documentation for D1 and D2 blocks.
- AMD R6xx and R7xx 3D register documentation as a semantic reference.
- Packet and ring-management ideas, if they map cleanly to GX2.

The practical conclusion is: use Radeon-family documentation as a hardware reference, not as the main software architecture.

## Recommended Architecture

### Layer 1: Stable GX2 framebuffer

Keep `WiiCafeFB` as the display-facing `IOFramebuffer`, but expand it into a real scanout driver.

Responsibilities:

- explicit scanout initialization,
- mode table management,
- framebuffer surface address and pitch programming,
- display timing and blanking programming,
- hardware cursor,
- gamma and LUT,
- vblank-related synchronization.

### Layer 2: GX2 accelerator service

Add a new paired accelerator service for GX2 instead of overloading `WiiCafeFB` with all rendering responsibilities.

Recommended new files:

- `WiiGraphics/src/Cafe/WiiGX2Accelerator.hpp`
- `WiiGraphics/src/Cafe/WiiGX2Accelerator.cpp`

Responsibilities:

- command queue management,
- GPU interrupt handling,
- fence and completion tracking,
- surface allocation bookkeeping,
- exposing the service and properties IOGraphics expects for accelerator discovery.

### Layer 3: User-space OpenGL renderer

Add a user-space renderer bundle that speaks to the accelerator service.

This is the layer that turns "framebuffer + GPU service" into actual OpenGL acceleration.

Responsibilities:

- context creation,
- command encoding for GX2,
- state translation from OpenGL to GX2 packets/register programming,
- shader and resource handling appropriate to the subset of OpenGL supported.

This bundle is the final mile for OpenGL hardware acceleration.

## Recommended Implementation Order

## Phase 0: Investigation hardening

Before writing the accelerator, verify exact Tiger-era interfaces from the target SDK and runtime because Apple's currently published `IOGraphics` open source is newer than 10.4.

What to verify on the target OS and SDK:

- exact private headers available in Tiger,
- exact accelerator class names and properties used by Tiger,
- whether Tiger's OpenGL stack already exposes a usable software renderer on this hardware bring-up path,
- how renderer bundles are discovered on 10.4.

This phase should not block framebuffer work.

## Phase 1: Make `WiiCafeFB` a real GX2 scanout driver

Status: in progress.

Already completed:

- added the D1 register coverage needed for the current scanout implementation,
- updated `enableController()` to perform explicit display programming,
- updated `setDisplayMode()` to reprogram scanout state rather than only changing format bits,
- made the current driver explicitly program framebuffer address, pitch, viewport bounds, blanking, and CRTC enable state,
- added a bounded scanout update-state poll and debug register logging,
- added a debug-gated first-light framebuffer test pattern path.

Recommended first edits:

- Add an internal display-mode table instead of the current single hardcoded mode.
- Program at minimum:
  - primary surface address,
  - pitch,
  - active window,
  - start and end coordinates,
  - scanout enable,
  - endianness,
  - timing and blanking registers.
- Add a deterministic test-pattern path so the first visual validation does not depend on higher layers.

Deliverable:

- stable text and GUI output at one known-good mode on real hardware,
- no assumption that the bootloader or prior firmware left the display configured.

## Phase 1A: Scanout hardening backlog

This is the next practical enhancement slice for the current driver.

Recommended next tasks:

- extend the current test-pattern path into a more complete framebuffer self-test,
- validate whether the current `D1GRPH_UPDATE` polling is sufficient on real hardware or needs different sequencing,
- add structured debug logging or register-dump support for failed bring-up,
- confirm whether D1 timing registers must be explicitly programmed on cold boot and, if so, add that path,
- evolve the one-mode implementation into a small internal mode table,
- validate cursor, CLUT, and gamma behavior after repeated mode changes,
- add a simple framebuffer self-test path that can be used before any accelerator work starts.

## Phase 2: Add interrupt-backed display synchronization

Use existing platform hooks rather than inventing a separate path.

Files to extend:

- `WiiPlatform/src/Interrupts/WiiInterruptController.cpp`
- `include/WiiProcessorInterface.hpp`
- possibly a GX2-specific helper in `WiiGraphics/src/Cafe/`

Goals:

- route command-processor and pixel-engine related interrupts,
- obtain a usable vblank or completion signal,
- support future page flips and fence completion.

Deliverable:

- stable synchronization primitive for both display and future rendering submission.

## Phase 3: Create the accelerator skeleton

Add the new GX2 accelerator service and wire it to the framebuffer.

The first version does not need full 3D rendering. It should focus on service discovery and surfaces.

Goals:

- publish an accelerator service that IOGraphics can associate with the framebuffer,
- expose accelerator index/type properties as required by the target OS,
- provide surface objects backed by GPU-visible memory,
- implement fence and wait primitives even if the first version only uses simple blits or copies.

Deliverable:

- `IOAccelFindAccelerator()`-style discovery works on the target system,
- there is a concrete kernel object for renderer bundles to talk to.

## Phase 4: Bring up command submission

This is the inflection point where the project becomes a real GPU driver.

Use these repo headers as the seed:

- `include/WiiCommandProcessor.hpp`
- `include/WiiGXFifo.hpp`
- `include/WiiPixelEngine.hpp`

Goals:

- initialize the command processor cleanly,
- allocate and manage a ring or FIFO buffer,
- encode and submit a minimal packet stream,
- confirm completion through interrupt or polling fallback,
- prove one hardware-executed operation end to end.

The first hardware-executed operation should be simple and observable, for example:

- clear a render target,
- copy from one surface to another,
- perform a minimal triangle draw to a surface.

Deliverable:

- one verified GPU-executed command stream with synchronization.

## Phase 5: User-space renderer bundle

Only after the accelerator can manage surfaces and submission should the project start the real OpenGL renderer work.

Goals:

- create a renderer bundle that binds to the accelerator service,
- expose a minimal OpenGL-capable renderer path,
- start with a narrow supported feature set rather than broad API coverage.

Practical recommendation:

- target an early fixed-function OpenGL subset first,
- delay advanced extensions, shaders, and aggressive optimization until the kernel path is stable.

Deliverable:

- a real hardware-backed OpenGL context for a constrained subset of applications.

## Suggested Near-Term Milestones

### Milestone A: Useful display driver

Definition:

- native GX2 scanout setup is explicit and reliable,
- 1280x720 works from cold boot,
- cursor and gamma still work,
- no dependency on firmware-initialized state.

### Milestone B: OpenGL software validation

Definition:

- OpenGL applications can create contexts on the target OS,
- the system uses its fallback renderer when hardware acceleration is absent,
- this confirms that the framebuffer integration is good enough to move to accelerator work.

Note:

This is a validation milestone, not the final goal.

### Milestone C: Accelerator discovery

Definition:

- IOGraphics can associate an accelerator with the GX2 framebuffer,
- a user client can create surfaces and synchronize with the device.

### Milestone D: First hardware OpenGL path

Definition:

- a simple OpenGL application renders through GX2,
- the GPU, not the CPU fallback path, performs the draw.

## Concrete Next Changes In This Repo

The best next implementation slice is:

1. Add a deterministic test-pattern and framebuffer self-check path to `WiiCafeFB`.
2. Harden the current scanout programming with update-state validation and better debug instrumentation.
3. Audit and extend D1 timing and blanking programming for cold-boot robustness.
4. Add a small internal mode table instead of a single hardcoded mode.
5. Add interrupt plumbing for display or command completion.
6. After that, create a new GX2 accelerator service instead of continuing to grow `WiiCafeFB` into a monolith.

This ordering is important because a broken scanout path will contaminate every later OpenGL and accelerator debugging session.

## Recommended Enhancement Roadmap

To keep the graphics driver moving forward without mixing too many risk areas together, the update plan should be staged like this:

### Stage A: Reliable framebuffer

- make scanout deterministic across cold boot and repeated mode sets,
- add test-pattern and self-test support,
- verify cursor, LUT, and gamma behavior after each reconfiguration,
- validate behavior on real Wii U hardware through manual deploy feedback.

### Stage B: Observable synchronization

- add interrupt-backed or polling-backed synchronization that is robust enough for page-flip style updates,
- define the fence/completion model the future accelerator will use,
- ensure the framebuffer can safely become the display half of a paired accelerated stack.

### Stage C: Minimal accelerator kernel path

- create the GX2 accelerator service,
- expose accelerator discovery properties to IOGraphics,
- add surface bookkeeping and synchronization primitives,
- prove one hardware operation end to end.

### Stage D: OpenGL bring-up

- validate software-renderer fallback first,
- then add the user-space GX2 renderer path,
- finally expand feature coverage from a narrow initial subset.

## Recommendation

The project should treat OpenGL support as a two-stage goal:

- short-term: validate the OS integration with a strong framebuffer and software-renderer fallback,
- final: build a native GX2 accelerator plus a user-space OpenGL renderer bundle.

Do not frame success as "OpenGL exists." Frame success as "OpenGL is hardware-backed on GX2."

The current repo is closest to that result if it continues with a native GX2 path rather than a Radeon port.

## References Used For This Investigation

- Apple IOKit Fundamentals, Graphics family reference:
  - https://developer.apple.com/library/archive/documentation/DeviceDrivers/Conceptual/IOKitFundamentals/Families_Ref/Families_Ref.html
- Apple OpenGL Programming Guide for Mac, renderer and pixel-format behavior:
  - https://developer.apple.com/library/archive/documentation/GraphicsImaging/Conceptual/OpenGL-MacProgGuide/opengl_pixelformats/opengl_pixelformats.html
  - https://developer.apple.com/library/archive/documentation/GraphicsImaging/Conceptual/OpenGL-MacProgGuide/opengl_contexts/opengl_contexts.html
- Apple open-source IOGraphics headers and interfaces:
  - https://github.com/apple-oss-distributions/IOGraphics
- WiiUBrew hardware references:
  - https://wiiubrew.org/wiki/Hardware
  - https://wiiubrew.org/wiki/Hardware/GX2
  - https://wiiubrew.org/wiki/Hardware/Processor_interface
  - https://wiiubrew.org/wiki/Hardware/Espresso
  - https://wiiubrew.org/w/index.php?title=Hardware/Latte_registers&action=raw
  - https://wiiubrew.org/w/index.php?title=Hardware/USB_Host_Controller&action=raw
- Linux Wii U GX2 discussion:
  - https://gitlab.com/linux-wiiu/linux-wiiu/-/work_items/19