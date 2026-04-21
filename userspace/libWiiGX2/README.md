# libWiiGX2 — Userland client for the Wii U GPU7 kernel driver

This library is the userspace-side companion to `WiiGraphics.kext`'s
`WiiGX2UserClient`.  It targets **Mac OS X 10.4 "Tiger"** on PowerPC as
the baseline (10.5 "Leopard" is supported via `MACOSX_VERSION=10.5`).

The API deliberately stays inside the pre-10.6 IOKit surface:

- Service discovery via `IOServiceMatching` / `IOServiceGetMatchingService`
- Scalar method calls via `IOConnectMethodScalarIScalarO`
- Memory sharing via `IOConnectMapMemory` / `IOConnectUnmapMemory`

No blocks, no Grand Central Dispatch, no `IOConnectCallMethod`.

## Files

| File             | Purpose |
|---|---|
| `WiiGX2.h`       | Public C API + selector / type constants |
| `WiiGX2.c`       | IOKitLib wrapper (`wiigx2_open`, `wiigx2_submit_ib`, …) |
| `WiiGX2PM4.h`    | Header-only PM4 Type-3 packet builder |
| `WiiR600ISA.h`   | Minimal R600 / R700 shader ISA encoder (CF / ALU / VTX / EXPORT) |
| `example_clear.c`| Smoke test: submits a SURFACE_SYNC-only IB and waits on the fence |

## API summary

```c
WiiGX2Device *d = wiigx2_open(NULL);
WiiGX2Buffer  b;
wiigx2_alloc_buffer(d, 64 * 1024, 0, &b);

/* Build a PM4 IB into b.cpuAddr using WiiGX2PM4.h helpers */
uint32_t *cmd = (uint32_t *) b.cpuAddr, count = 0;
wiigx2_pm4_surface_sync(cmd, &count, ..., 0xFFFFFFFFu, 0);
wiigx2_pm4_pad_to_multiple_of_4(cmd, &count);

uint32_t seq;
wiigx2_submit_ib(d, &b, 0, count, &seq);

int reached;
wiigx2_wait_fence(d, seq, 500, &reached);

wiigx2_free_buffer(d, &b);
wiigx2_close(d);
```

## Design notes

- Every IB submission goes through the single kernel CP ring. The kernel
  serialises emission with an `IOLock`, so multiple user clients can
  coexist — each one just sees its own fence values advancing
  monotonically on a shared counter.
- Buffers are physically contiguous, wired, page-granular, and identical
  to the backing used by the CP/IH/DMA rings in the kernel (see
  `WiiGX2::allocateGPUBuffer`). This lets userland use the same
  buffer pointers for vertex data, constant buffers, IBs, and render
  targets.
- The fence page is mapped **read-only** into user space, so user code
  can poll `wiigx2_poll_fence()` without a syscall but cannot corrupt
  the GPU's writeback.
- The PM4 and R600 ISA builders are header-only. They emit host-endian
  dwords; the GPU's MC / fetch-shader swap bits handle the actual byte
  order.

## Next layers

Mesa's r600 gallium backend (`src/gallium/drivers/r600/`) is the
reference for a full GL state tracker. For 10.4/10.5 the practical path
is a **static build** of Mesa with only the r600 backend enabled, linked
against this library as the winsys. Alternatively a hand-rolled
GLSL 1.20 → R600 ISA translator can be built on top of `WiiR600ISA.h`
if shipping Mesa proves impractical on PowerPC Tiger.
