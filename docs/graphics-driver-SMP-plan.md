# Wii U Graphics And SMP Bring-up Plan

## Project intent

This repository is building the platform support needed to run Mac OS X on Wii and Wii U hardware.
The two highest-risk items right now are:

- Espresso SMP bring-up on Wii U
- GX2 graphics acceleration on Wii U

These must be treated as separate tracks. SMP is mainly a CPU/platform problem. Graphics is effectively a new GPU stack.

## Current workspace status

### What already exists for SMP

- WiiPlatform already registers CPUs with XNU and creates an IOCPUInterruptController.
- LatteInterruptController already handles the 64 Latte interrupt vectors.
- WiiInterruptController already exposes the Processor Interface vectors.
- The missing work is concentrated in actual secondary-core bring-up and IPI support.

What is still missing in the current tree:

- WiiCPU currently hardcodes `_numCPUs = 1`.
- `initCPU()` only handles the boot CPU path.
- `startCPU()`, `quiesceCPU()`, and `haltCPU()` are still stubs.
- `processor_info.start_paddr` is currently hardcoded to `0x0100` for all CPUs.
- WiiInterruptController still assumes CPU0-only PI handling on Cafe.

### What already exists for graphics

- WiiCafeFB and WiiFlipperFB are framebuffer drivers, not acceleration drivers.
- WiiCafeFB already handles:
	- MMIO mapping
	- basic display mode programming
	- gamma and CLUT loading
	- hardware cursor programming
- GX2Regs.hpp currently covers display-controller, LUT, and cursor registers.

What is not present anywhere in the tree yet:

- GPU reset and clock/power sequencing
- command processor or ring buffer setup
- GPU interrupt and fence handling
- memory manager for GPU-visible buffers
- MMU or GART or VM programming
- shader upload or compilation path
- kernel user client for acceleration
- userspace OpenGL renderer or translation layer

Bottom line: the current graphics code is a scanout foundation only.

## SMP track

### External facts worth trusting

- Linux-WiiU reports that cores 1 and 2 do not boot from the EXI bootstub in Wii U mode.
- Their successful bring-up used code written at `0x08100100`, which maps into the Wii U boot image area used by the secondary cores.
- Linux-WiiU also found that CAR and BCR initialization is required for reliable cross-core cache coherency.
- Linux-WiiU had to rework IPI delivery. Older public bit descriptions for SCR and ICI routing were not reliable enough to follow blindly.

### Practical implementation plan

1. Enumerate all CPU nubs from `/cpus` and stop hardcoding a single CPU.
2. Track boot CPU versus secondary CPUs from the device tree and register all three with XNU.
3. Add a secondary-core trampoline placed at the correct high-vector target used in Wii U mode.
4. In that trampoline:
	 - set up minimal BAT and MMU state
	 - initialize CAR and BCR with validated values
	 - flush and invalidate caches correctly
	 - publish a simple handshake state in explicitly flushed memory
	 - jump to XNU's secondary start address
5. Implement `WiiCPU::startCPU()` so it:
	 - writes trampoline and startup arguments
	 - flushes dcache and icache for the trampoline region
	 - releases or wakes the target core using the verified Espresso mechanism
	 - waits for the secondary handshake with timeout and debug logging
6. Implement real `initCPU(false)` handling for secondaries.
7. Implement real IPI send and acknowledge.
8. Extend interrupt handling so per-core PI or Latte sources are not treated as CPU0-only.
9. Keep SMP behind a boot-arg until the machine is stable.

### Expected first SMP milestone

- All three CPUs register with XNU.
- Secondary CPUs reach `initCPU(false)` and transition to running.
- Cross-call or IPI traffic works reliably under scheduler load.

## Graphics track

### Do not frame this as "just a better framebuffer"

- A standard Mac OS X OpenGL path requires more than IOFramebuffer.
- The current tree has scanout support only.
- Hardware-accelerated graphics will require both:
	- a kernel-side GPU command, memory, and interrupt path
	- a userspace rendering stack that talks to it

### Architecture caveat

Public sources are not fully consistent on whether GPU7 or GX2 is best thought of as late R700 or early Evergreen-class hardware.

The safe planning assumption is:

- it is Radeon-like enough that R6xx and R7xx documentation is useful
- the exact feature level still needs to be proven from real register access and packet submission

Because of that:

- OpenGL 4.0 should not be the initial target.
- A realistic first exposed goal is "draw and page-flip reliably".
- After that, a practical target is OpenGL 1.x or 2.x class functionality.
- A higher feature ceiling should only be claimed after the hardware model is confirmed.

### Practical implementation plan

1. Finish GPU identification and reset coverage.
2. Extend register definitions beyond scanout and cursor.
3. Use `LT_GPUINDADDR` and `LT_GPUINDDATA` to probe the indirect GPU register space safely.
4. Fingerprint major blocks such as CP, GRBM, SQ, SPI, CB, DB, DMA, and memory-management blocks.
5. Build a minimal kernel GX2 bring-up layer with:
	 - GPU reset and clock ungating
	 - interrupt source discovery
	 - MEM1 and MEM2 buffer allocation suitable for GPU access
	 - cache flush and invalidate helpers for shared CPU and GPU buffers
6. Prove command submission before any OpenGL work.
7. After command submission works, add interrupt and fence infrastructure.
8. Only then design the Mac OS X acceleration interface.

### Minimum viable graphics milestone

The first real milestone should be:

- initialize the command processor
- allocate a ring buffer
- submit one tiny packet stream
- wait on a fence or interrupt
- clear a render target or blit a test pattern
- copy or present that result through the existing WiiCafeFB scanout path

Only after that should shader upload and triangle rendering start.

### Why this order matters

If packet submission cannot clear a surface reliably, then trying to target OpenGL or Mac applications is premature.
The right early demo is:

1. clear a surface
2. draw a triangle
3. draw a textured triangle
4. swap or page-flip reliably

Minecraft and Blender are long-term validation targets, not bring-up targets.

## Major risks

- The repository currently has no userspace graphics components, so a "standard OpenGL interface" is not a near-term kernel-only task.
- Porting an existing Apple Radeon stack may be difficult because the target OS versions are old and PowerPC support matters.
- If the GPU is closer to R700 than Evergreen, OpenGL 4.0 is not an evidence-based target.
- GPU debugging without stable logging and a conservative boot configuration will be slow.

## Recommended order of attack

1. Finish SMP first, but keep it behind a boot-arg or build option.
2. Keep the default bring-up configuration single-core until SMP is stable.
3. In parallel, use the existing WiiCafeFB path to keep display output and logging available while building the GPU submission path.
4. Do not start on OpenGL until a native GX2 packet stream can clear a surface and signal completion.

## Immediate code tasks in this tree

- `WiiPlatform/src/PE/WiiCPU.cpp`
	- remove `_numCPUs = 1`
	- implement `startCPU()`
	- implement secondary `initCPU(false)`
	- implement `haltCPU()` and `quiesceCPU()`
- `WiiPlatform/src/Interrupts/WiiInterruptController.cpp`
	- stop assuming CPU0-only PI routing on Cafe
	- add per-core handling where required
- `WiiGraphics/src/Cafe/GX2Regs.hpp`
	- extend register coverage beyond scanout, cursor, and LUT programming
- `WiiGraphics`
	- add a new low-level GX2 core module instead of growing WiiCafeFB into a 3D driver

## References

- SMP: https://www.wiiulinux.net/wii-u-news/smp-on-the-wii-u/
- General hardware: https://wiiubrew.org/wiki/Hardware
- Espresso registers: https://wiiubrew.org/wiki/Hardware/Espresso
- Processor interface: https://wiiubrew.org/wiki/Hardware/Processor_interface
- Latte IRQs: https://wiiubrew.org/wiki/Hardware/Latte_IRQs
- Latte registers: https://wiiubrew.org/wiki/Hardware/Latte_registers
- Wii U architecture overview: https://www.copetti.org/writings/consoles/wiiu/
- AMD references already worth mining locally:
	- Radeon R6xx or R7xx acceleration guide
	- Radeon R6xx or R7xx 3D register reference guide
	- R700 family ISA material

## Bottom line

- SMP is close enough to attack immediately. The missing work is concrete and localized.
- Graphics acceleration is not close. The current code is a framebuffer foundation, not a Radeon driver.
- The right graphics milestone is "submit packets and render one test surface", not "OpenGL 4.0".