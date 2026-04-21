# OpenGL 2.1 Capability Assessment — Wii U GPU7 / Latte

## Hardware Summary

| Property | Value |
|---|---|
| GPU codename | Latte / GPU7 |
| Architecture | AMD TeraScale (R700 class, Radeon HD 4000 series) |
| Shader model | Unified (VS/PS/GS share the same ALU clusters) |
| GX2 API level | OpenGL 3.3 equivalent (per Copetti / fail0verflow) |
| Target | OpenGL 2.1 (GLSL 1.20) — conservative, well within hardware |

Sources:
- Copetti Wii U architecture analysis (references fail0verflow 30C3 talk)
- Linux radeon driver r600.c / rv770.c / r600d.h
- AMD RV630 Register Reference Guide (local PDF)
- Wikipedia OpenGL version history

## OpenGL 2.1 Feature Requirements

OpenGL 2.1 was released July 2, 2006.  Key features beyond OpenGL 2.0:

| Feature | Status on GPU7 |
|---|---|
| GLSL 1.20 | Supported — R700 shader ISA covers GLSL 1.20+ |
| Pixel Buffer Objects (PBO) | Supported — CP DMA and async copy via DMA engine |
| sRGB Textures | Supported — `FORCE_DEGAMMA` bit in SQ_TEX_RESOURCE_WORD4 |
| Non-power-of-two textures | Supported — R600+ handles NPOT natively |
| Multiple render targets (MRT) | Supported — 8 CB colour buffers |
| Occlusion queries | Supported — EVENT_WRITE_EOP with ZPASS_DONE |
| Point sprites | Supported — VGT + SPI handle point sprite expansion |
| Two-sided stencil | Supported — DB_DEPTH_CONTROL has separate front/back |
| Separate blend per RT | Supported — CB_COLOR_CONTROL per-target blend |
| Vertex/Fragment shaders | Supported — SQ unified shader with VS/PS/GS/ES stages |

### Conclusion

GPU7 hardware exceeds OpenGL 2.1 requirements.  The bottleneck is entirely software — specifically, the absence of a command submission path, shader compiler, and state tracker.

## Mapping GL 2.1 to R700 Hardware Blocks

| GL concept | Hardware block | Key registers |
|---|---|---|
| Vertex fetch | VGT (Vertex Geometry Tessellator) | VGT_PRIMITIVE_TYPE, VGT_NUM_INSTANCES, VGT_DMA_BASE |
| Vertex shader | SQ (Sequencer) | SQ_PGM_START_VS, SQ_PGM_RESOURCES_VS, SQ_GPR_RESOURCE_MGMT1 |
| Primitive assembly | PA (Primitive Assembly) | PA_CL_ENHANCE, PA_SC_MODE_CNTL, PA_SC_AA_CONFIG |
| Rasterisation | SC (Scan Converter) | PA_SC_SCREEN_SCISSOR_TL, PA_SC_GENERIC_SCISSOR_TL |
| Fragment shader | SQ | SQ_PGM_START_PS, SQ_PGM_RESOURCES_PS, SPI_PS_IN_CONTROL_0 |
| Texturing | TA/TC | TA_CNTL_AUX, TC_CNTL, SQ_TEX_RESOURCE_WORD0..5 |
| Depth / stencil | DB (Depth Block) | DB_DEPTH_CONTROL, DB_DEPTH_INFO, DB_DEPTH_BASE |
| Colour output | CB (Colour Buffer) | CB_COLOR0_BASE, CB_COLOR0_INFO, CB_TARGET_MASK |
| Blending | CB | CB_COLOR_CONTROL, CB_COLOR0_INFO blend bits |
| Shader export | SX | SX_MISC, SX_MEMORY_EXPORT_BASE |
| Sync / fences | CP + SCRATCH | SCRATCH_REG0..7, EVENT_WRITE_EOP, WAIT_REG_MEM |
| GPU DMA / PBO | DMA engine | DMA_RB_CNTL, DMA_RB_BASE, DMA_PACKET_COPY |
| GPU interrupt | IH ring | IH_RB_CNTL, IH_RB_BASE, IH_CNTL |

## Shader Compilation Path

R700 uses a proprietary bytecode ISA (R600 ISA / TeraScale ISA).  Options:

1. **Port Mesa r600 shader compiler** — Mesa's `src/gallium/drivers/r600/` contains a complete TGSI → R600 ISA compiler.  GitLab access currently blocked by bot protection; source can be obtained from a Mesa tarball.
2. **Write a minimal GLSL 1.20 → R600 ISA translator** — only needs: vertex attribute fetch, ALU (scalar + vector), texture fetch, export instructions.
3. **Use pre-compiled shader binaries** — for initial bring-up, hand-assemble a clear shader and a passthrough VS/PS pair.

### Recommended approach

For bring-up:
- Hand-write a VS that passes position + colour through.
- Hand-write a PS that exports a constant colour (for clear) or interpolated colour.
- Upload via SQ_PGM_START_VS / SQ_PGM_START_PS after setting SQ_GPR_RESOURCE_MGMT.

For full OpenGL 2.1:
- Integrate Mesa's r600 gallium backend (or equivalent) as a userspace library.
- The kernel driver provides command submission, memory management, and synchronisation.

## Implementation Roadmap

### Phase 1: Command Processor (current)
- [x] CP register definitions (CP_ME_CNTL, CP_RB_*, CP_INT_*)
- [x] CP halt / soft-reset / resume sequence
- [x] PM4 packet format macros
- [x] Ring buffer memory allocation (IOBufferMemoryDescriptor)
- [x] ME_INITIALIZE packet submission
- [x] Fence via SCRATCH_REG + SET_CONFIG_REG
- [x] GPU default state initialization (SQ/SPI/VGT/PA/CB — RV710 profile)
- [x] Pre-compiled R6xx blit shader bytecode (VS + PS)

### Phase 2: GPU Memory and Sync
- [x] Reusable GPU-visible physically contiguous buffer allocation
- [x] CP EOP writeback fence into shared memory
- [x] MEM2-specific allocation policy / heap management (live/peak byte accounting + 4 MiB soft driver budget, warning log on breach)
- [x] MEM1 (EDRAM) access for render targets (32 MiB aperture at phys 0x00000000 wired via `IODeviceMemory::withRange`, bump allocator with 256-byte default alignment exposed through `WiiGX2::allocMEM1` / `resetMEM1Heap`)
- [x] HDP flush around CPU ↔ GPU data transfers
- [x] IH ring allocation / programming (interrupt delivery still masked)
- [x] IH ring consumption and acknowledgement (`WiiGX2::consumeIHRing()` drains 4-DW entries, byte-swaps from MC-LE, advances `IH_RB_RPTR`; live GPU7 hook into `LatteInterruptController` now installed via `callPlatformFunction("LatteRegisterDirectIRQ", ...)` against vector 43 from `WiiCafeFB::start()`)
- [x] DMA engine ring setup for async copies (`WiiGX2::setupDMARing()` programs RB_CNTL with BE swap bits, IB enable, DMA_MODE=1 per rv770; `dmaConstFill()` emits a CONSTANT_FILL packet and polls RPTR writeback as a functional test)

### Phase 3: Clear and Blit
- [x] Minimal SQ/SPI/VGT/PA/CB/DB state programming (full context-reg program in `WiiGX2::submitColorClear` covering PA scissor/viewport/VTE/clip, CB target mask + format + size + base, DB disabled, SPI VS_OUT_CONFIG/PS_INPUT_CNTL, SQ PGM start + resources for VS/PS)
- [x] Hand-coded clear shader (reuses the R6xx blit VS/PS bytecode from `r600_blit_shaders.c`; PS exports interpolated VB colour so the clear colour is encoded per-vertex)
- [x] Submit draw via CP: set state, set shaders, draw full-screen triangle (`WiiGX2::submitColorClear` now mirrors the Radeon blit path: window-space oversized triangle VB, `PA_SC_WINDOW_OFFSET=0`, `PA_CL_VTE_CNTL=VTX_XY_FMT`, SURFACE_SYNC → VGT config → PA/CB/DB context regs → SPI/SQ shader binding → SET_RESOURCE VB descriptor → DRAW_INDEX_AUTO → post-draw CB flush → EVENT_WRITE_EOP fence, then polls a shared-memory fence page for ≤100 ms)
- [x] Verify result via existing WiiCafeFB scanout (driver runs a single 1280×720 BGRA clear against `getDeviceMemoryWithIndex(1)` at `start()` time as the Phase-3 smoke gate)

### Phase 4: OpenGL 2.1 State Tracker
- [x] IOUserClient for userspace command submission (`WiiGX2UserClient`, 10.4/10.5 PowerPC-safe dispatch via `getTargetAndMethodForIndex` + `clientMemoryForType`; selectors: GetInfo, AllocBuffer, FreeBuffer, SubmitIB, WaitFence, ReadFence; shared user fence page mapped read-only into userspace)
- [x] Userspace client library (`userspace/libWiiGX2/`): C89 wrapper around `IOConnectMethodScalarIScalarO` + `IOConnectMapMemory`, header-only PM4 Type-3 packet builder (`WiiGX2PM4.h`), header-only R600/R700 ISA encoder covering CF (CF_VTX / CF_ALU / CF_EXPORT), OP2 ALU, and VTX fetch with `BUF_SWAP_32BIT` for PowerPC (`WiiR600ISA.h`), plus a pass-through VS builder and a working `example_clear.c` smoke test
- [ ] Userspace Mesa r600 gallium state tracker (or equivalent)
- [ ] GLSL 1.20 shader compilation
- [ ] Texture upload and sampling
- [ ] Blending and depth test state

### Phase 5: Validation
- [ ] glxgears or equivalent test
- [ ] glmark2 basic tests
- [ ] sRGB correctness check
- [ ] Multi-RT rendering test

## Key Differences from Desktop R700

| Aspect | Desktop R700 | GPU7 / Latte |
|---|---|---|
| Bus | PCIe | On-die (Latte SoC) |
| Memory | GDDR3/5 via MC | MEM1 (32MB EDRAM) + MEM2 (2GB DDR3) |
| Register access | BAR0 MMIO | Direct MMIO + Latte bridge indirect |
| Endianness | Little-endian host | Big-endian (PowerPC) — requires BUF_SWAP_32BIT |
| BIOS tables | ATOMBIOS used for posting, connector routing, clocks, and power tables | Wii U firmware-owned; not required for raw CP / IH / DMA experimentation |
| Microcode | Linux loads PFP / ME / RLC / SMC blobs separately from ATOMBIOS | Status unknown — may be pre-loaded by boot firmware, but it is a separate dependency from ATOMBIOS |
| Display output | TMDS / DisplayPort / HDMI | Fixed HDMI via Latte display controller |
| Power management | Full DVFS | Minimal — no need for complex PM on embedded SoC |

## ATOMBIOS / Firmware Notes

- Linux `r600_init` / `rv770_init` expect ATOMBIOS because a desktop Radeon driver must post the card, parse connector topology, discover clock / voltage tables, and run board-specific scripts.
- The CP, IH, DMA, and PM4 submission paths are separate from that BIOS-table logic. On Wii U, they can be brought up independently if boot firmware has already posted the GPU and established a stable memory/display configuration.
- Linux also loads `pfp`, `me`, `rlc`, and `smc` firmware blobs separately from ATOMBIOS. Those firmware blobs remain a distinct dependency to track even if ATOMBIOS itself is absent.

## References

- Linux radeon r600.c / rv770.c — CP init sequence
- Linux r600d.h — register offsets and bitfields
- AMD RV630 Register Reference Guide (42589_rv630_rrg_1.01o.pdf)
- Copetti Wii U architecture: https://www.copetti.org/writings/consoles/wiiu/
- Wikipedia OpenGL: https://en.wikipedia.org/wiki/OpenGL
- Mesa r600 gallium: https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/gallium/drivers/r600
