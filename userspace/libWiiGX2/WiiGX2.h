/*
 * WiiGX2.h — userland public interface for the Wii U GPU7 kernel driver.
 *
 * Targets Mac OS X 10.4 "Tiger" and 10.5 "Leopard" on PowerPC. Uses only
 * pre-10.6 IOKitLib:
 *   - IOServiceMatching / IOServiceGetMatchingService
 *   - IOServiceOpen
 *   - IOConnectMethodScalarIScalarO (deprecated in 10.5 but still present
 *     in IOKit.framework through 10.7)
 *   - IOConnectMapMemory / IOConnectUnmapMemory
 * No blocks, no GCD, no IOConnectCallMethod.
 *
 * Copyright (c) 2026 John Davis. All rights reserved.
 */

#ifndef WIIGX2_USERLAND_H
#define WIIGX2_USERLAND_H

#include <stdint.h>
#include <stddef.h>
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------
 * Kernel protocol constants — MUST stay in sync with
 * WiiGraphics/src/Cafe/WiiGX2UserClient.hpp
 * -------------------------------------------------------------------- */

enum {
    kWiiGX2UCSelectorGetInfo     = 0,
    kWiiGX2UCSelectorAllocBuffer = 1,
    kWiiGX2UCSelectorFreeBuffer  = 2,
    kWiiGX2UCSelectorSubmitIB    = 3,
    kWiiGX2UCSelectorWaitFence   = 4,
    kWiiGX2UCSelectorReadFence   = 5
};

#define kWiiGX2UCMemoryUserFence   0u
#define kWiiGX2UCMemoryBufferBase  0x00010000u

/* Must match WiiGraphics/src/Cafe/WiiGX2UserClient.hpp. Type 0 is left to
 * IOFramebuffer / WindowServer, so the custom GX2 interface uses an
 * explicit non-zero open type. */
#define kWiiGX2UCType              0x47583255u

#define kWiiGX2UCMaxBuffers        32u
#define kWiiGX2UCMaxBufferBytes    (16u * 1024u * 1024u)

enum {
    kWiiGX2FlagCPRunning    = (1u << 0),
    kWiiGX2FlagIHReady      = (1u << 1),
    kWiiGX2FlagDMAReady     = (1u << 2),
    kWiiGX2FlagMEM1Ready    = (1u << 3),
    kWiiGX2FlagUserFenceReady = (1u << 4)
};

/* --------------------------------------------------------------------
 * Device handle.
 * -------------------------------------------------------------------- */

typedef struct WiiGX2Device WiiGX2Device;

typedef struct {
    uint32_t handle;
    uint32_t sizeBytes;         /* rounded up to page size */
    uint64_t gpuPhys;           /* physical address for PM4 INDIRECT_BUFFER */
    void    *cpuAddr;           /* user-mapped virtual address (R/W) */
    size_t   cpuMapSize;
} WiiGX2Buffer;

typedef struct {
    uint32_t chipRevision;
    uint32_t mem1SizeBytes;
    uint32_t mem1UsedBytes;
    uint32_t mem2LiveBytes;
    uint32_t flags;
} WiiGX2Info;

/* --------------------------------------------------------------------
 * Device lifecycle.
 * -------------------------------------------------------------------- */

/* Open the first WiiCafeFB in the IORegistry and negotiate a user client.
 * On failure returns NULL and (if errOut is non-NULL) fills it with a
 * kern_return_t from the underlying IOKit call. */
WiiGX2Device *wiigx2_open(kern_return_t *errOut);

/* Close the user client. Frees all outstanding buffers owned by the
 * process and releases the fence mapping. */
void wiigx2_close(WiiGX2Device *dev);

/* Query driver info. Safe to call at any time while the device is open. */
kern_return_t wiigx2_get_info(WiiGX2Device *dev, WiiGX2Info *outInfo);

/* --------------------------------------------------------------------
 * Buffer management. Buffers are physically contiguous, wired, and
 * identical to the backing used by the CP/IH/DMA rings.
 * -------------------------------------------------------------------- */

kern_return_t wiigx2_alloc_buffer(WiiGX2Device *dev,
                                  uint32_t sizeBytes,
                                  uint32_t usageHint,
                                  WiiGX2Buffer *outBuffer);

kern_return_t wiigx2_free_buffer(WiiGX2Device *dev,
                                 WiiGX2Buffer *buffer);

/* --------------------------------------------------------------------
 * Command submission and synchronisation.
 *
 * An IB is built in CPU memory inside a buffer allocated by
 * wiigx2_alloc_buffer; call wiigx2_submit_ib() with the dword offset
 * and count to enqueue it on the CP. The kernel emits:
 *   INDIRECT_BUFFER(ib, count) → SURFACE_SYNC(full) → EVENT_WRITE_EOP
 * and returns a monotonically increasing fence seq.
 *
 * The user fence page is mapped once per device open; wiigx2_poll_fence()
 * reads it without a kernel transition. wiigx2_wait_fence() blocks in the
 * kernel up to timeoutMs (capped kernel-side at 2000 ms).
 * -------------------------------------------------------------------- */

kern_return_t wiigx2_submit_ib(WiiGX2Device *dev,
                               const WiiGX2Buffer *buffer,
                               uint32_t dwordOffset,
                               uint32_t dwordCount,
                               uint32_t *outFenceSeq);

kern_return_t wiigx2_wait_fence(WiiGX2Device *dev,
                                uint32_t fenceSeq,
                                uint32_t timeoutMs,
                                int *outReached);

/* Lock-free read of the last completed fence value from the shared page. */
uint32_t wiigx2_poll_fence(const WiiGX2Device *dev);

/* Same as wiigx2_poll_fence but performs an IOKit call to cross-check. */
kern_return_t wiigx2_read_fence(WiiGX2Device *dev, uint32_t *outSeq);

/* Wrap-safe compare: returns non-zero if `a` is >= `b` on the monotonic
 * fence timeline. Useful for comparing polled values. */
int wiigx2_fence_reached(uint32_t observed, uint32_t target);

#ifdef __cplusplus
}
#endif

#endif /* WIIGX2_USERLAND_H */
