/*
 * WiiGX2.c — IOKitLib wrapper for the Wii U GPU7 user client.
 *
 * Targets Mac OS X 10.4 / 10.5 PowerPC. Uses only pre-10.6 APIs.
 *
 * Copyright (c) 2026 John Davis. All rights reserved.
 */

#include "WiiGX2.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <libkern/OSByteOrder.h>
#include <IOKit/IOKitLib.h>

struct WiiGX2Device {
    io_service_t  service;
    io_connect_t  connect;

    /* Shared user fence page (read-only mapping). */
    vm_address_t  fenceAddr;
    vm_size_t     fenceSize;

    /* Per-process buffer bookkeeping — used only for unmap on close. */
    struct {
        int          live;
        vm_address_t addr;
        vm_size_t    size;
    } buffers[kWiiGX2UCMaxBuffers];
};

/* --------------------------------------------------------------------
 * Device lifecycle.
 * -------------------------------------------------------------------- */

WiiGX2Device *
wiigx2_open(kern_return_t *errOut)
{
    WiiGX2Device    *dev;
    CFMutableDictionaryRef matching;
    io_service_t     service;
    io_connect_t     connect;
    kern_return_t    kr;
    vm_address_t     fenceAddr = 0;
    vm_size_t        fenceSize = 0;

    if (errOut != NULL) {
        *errOut = kIOReturnSuccess;
    }

    matching = IOServiceMatching("WiiCafeFB");
    if (matching == NULL) {
        if (errOut != NULL) { *errOut = kIOReturnNoMemory; }
        return NULL;
    }

    /* IOServiceGetMatchingService consumes a reference to `matching`. */
    service = IOServiceGetMatchingService(kIOMasterPortDefault, matching);
    if (service == IO_OBJECT_NULL) {
        if (errOut != NULL) { *errOut = kIOReturnNotFound; }
        return NULL;
    }

    kr = IOServiceOpen(service, mach_task_self(), kWiiGX2UCType, &connect);
    if (kr != kIOReturnSuccess) {
        IOObjectRelease(service);
        if (errOut != NULL) { *errOut = kr; }
        return NULL;
    }

    /* Map the shared user-fence page read-only. */
    kr = IOConnectMapMemory(connect,
                            kWiiGX2UCMemoryUserFence,
                            mach_task_self(),
                            &fenceAddr, &fenceSize,
                            kIOMapAnywhere | kIOMapReadOnly);
    if (kr != kIOReturnSuccess) {
        IOServiceClose(connect);
        IOObjectRelease(service);
        if (errOut != NULL) { *errOut = kr; }
        return NULL;
    }

    dev = (WiiGX2Device *) calloc(1, sizeof (*dev));
    if (dev == NULL) {
        IOConnectUnmapMemory(connect, kWiiGX2UCMemoryUserFence,
                             mach_task_self(), fenceAddr);
        IOServiceClose(connect);
        IOObjectRelease(service);
        if (errOut != NULL) { *errOut = kIOReturnNoMemory; }
        return NULL;
    }

    dev->service   = service;
    dev->connect   = connect;
    dev->fenceAddr = fenceAddr;
    dev->fenceSize = fenceSize;
    return dev;
}

void
wiigx2_close(WiiGX2Device *dev)
{
    uint32_t i;

    if (dev == NULL) {
        return;
    }

    for (i = 0; i < kWiiGX2UCMaxBuffers; i++) {
        if (dev->buffers[i].live && dev->buffers[i].addr != 0) {
            IOConnectUnmapMemory(dev->connect,
                                 kWiiGX2UCMemoryBufferBase + i,
                                 mach_task_self(),
                                 dev->buffers[i].addr);
            /* Kernel-side FreeBuffer is best-effort on close. */
            (void) IOConnectMethodScalarIScalarO(dev->connect,
                                                 kWiiGX2UCSelectorFreeBuffer,
                                                 1, 0, (int) i);
            dev->buffers[i].live = 0;
            dev->buffers[i].addr = 0;
            dev->buffers[i].size = 0;
        }
    }

    if (dev->fenceAddr != 0) {
        IOConnectUnmapMemory(dev->connect,
                             kWiiGX2UCMemoryUserFence,
                             mach_task_self(),
                             dev->fenceAddr);
    }

    IOServiceClose(dev->connect);
    IOObjectRelease(dev->service);
    free(dev);
}

/* --------------------------------------------------------------------
 * Scalar method wrappers.
 * -------------------------------------------------------------------- */

kern_return_t
wiigx2_get_info(WiiGX2Device *dev, WiiGX2Info *outInfo)
{
    int out[5];
    kern_return_t kr;

    if (dev == NULL || outInfo == NULL) {
        return kIOReturnBadArgument;
    }

    memset(out, 0, sizeof (out));
    kr = IOConnectMethodScalarIScalarO(dev->connect,
                                       kWiiGX2UCSelectorGetInfo,
                                       0, 5,
                                       &out[0], &out[1], &out[2],
                                       &out[3], &out[4]);
    if (kr != kIOReturnSuccess) {
        return kr;
    }
    outInfo->chipRevision    = (uint32_t) out[0];
    outInfo->mem1SizeBytes   = (uint32_t) out[1];
    outInfo->mem1UsedBytes   = (uint32_t) out[2];
    outInfo->mem2LiveBytes   = (uint32_t) out[3];
    outInfo->flags           = (uint32_t) out[4];
    return kIOReturnSuccess;
}

kern_return_t
wiigx2_alloc_buffer(WiiGX2Device *dev, uint32_t sizeBytes,
                    uint32_t usageHint, WiiGX2Buffer *outBuffer)
{
    int out[3];
    uint32_t handle;
    uint64_t phys;
    vm_address_t addr = 0;
    vm_size_t    mapSize = 0;
    kern_return_t kr;

    if (dev == NULL || outBuffer == NULL) {
        return kIOReturnBadArgument;
    }
    if (sizeBytes == 0 || sizeBytes > kWiiGX2UCMaxBufferBytes) {
        return kIOReturnBadArgument;
    }

    memset(outBuffer, 0, sizeof (*outBuffer));
    memset(out, 0, sizeof (out));

    kr = IOConnectMethodScalarIScalarO(dev->connect,
                                       kWiiGX2UCSelectorAllocBuffer,
                                       2, 3,
                                       (int) sizeBytes, (int) usageHint,
                                       &out[0], &out[1], &out[2]);
    if (kr != kIOReturnSuccess) {
        return kr;
    }
    handle = (uint32_t) out[0];
    phys   = ((uint64_t) (uint32_t) out[1]) |
             (((uint64_t) (uint32_t) out[2]) << 32);

    if (handle >= kWiiGX2UCMaxBuffers) {
        (void) IOConnectMethodScalarIScalarO(dev->connect,
                                             kWiiGX2UCSelectorFreeBuffer,
                                             1, 0, (int) handle);
        return kIOReturnInternalError;
    }

    kr = IOConnectMapMemory(dev->connect,
                            kWiiGX2UCMemoryBufferBase + handle,
                            mach_task_self(),
                            &addr, &mapSize,
                            kIOMapAnywhere);
    if (kr != kIOReturnSuccess) {
        (void) IOConnectMethodScalarIScalarO(dev->connect,
                                             kWiiGX2UCSelectorFreeBuffer,
                                             1, 0, (int) handle);
        return kr;
    }

    dev->buffers[handle].live = 1;
    dev->buffers[handle].addr = addr;
    dev->buffers[handle].size = mapSize;

    outBuffer->handle     = handle;
    outBuffer->sizeBytes  = (uint32_t) mapSize;
    outBuffer->gpuPhys    = phys;
    outBuffer->cpuAddr    = (void *) addr;
    outBuffer->cpuMapSize = mapSize;
    return kIOReturnSuccess;
}

kern_return_t
wiigx2_free_buffer(WiiGX2Device *dev, WiiGX2Buffer *buffer)
{
    uint32_t handle;
    kern_return_t kr;

    if (dev == NULL || buffer == NULL) {
        return kIOReturnBadArgument;
    }
    handle = buffer->handle;
    if (handle >= kWiiGX2UCMaxBuffers) {
        return kIOReturnBadArgument;
    }
    if (!dev->buffers[handle].live) {
        return kIOReturnNotFound;
    }

    IOConnectUnmapMemory(dev->connect,
                         kWiiGX2UCMemoryBufferBase + handle,
                         mach_task_self(),
                         dev->buffers[handle].addr);
    dev->buffers[handle].live = 0;
    dev->buffers[handle].addr = 0;
    dev->buffers[handle].size = 0;

    kr = IOConnectMethodScalarIScalarO(dev->connect,
                                       kWiiGX2UCSelectorFreeBuffer,
                                       1, 0, (int) handle);

    memset(buffer, 0, sizeof (*buffer));
    return kr;
}

kern_return_t
wiigx2_submit_ib(WiiGX2Device *dev, const WiiGX2Buffer *buffer,
                 uint32_t dwordOffset, uint32_t dwordCount,
                 uint32_t *outFenceSeq)
{
    int out = 0;
    kern_return_t kr;

    if (dev == NULL || buffer == NULL || outFenceSeq == NULL) {
        return kIOReturnBadArgument;
    }
    *outFenceSeq = 0;

    if (buffer->handle >= kWiiGX2UCMaxBuffers) {
        return kIOReturnBadArgument;
    }

    kr = IOConnectMethodScalarIScalarO(dev->connect,
                                       kWiiGX2UCSelectorSubmitIB,
                                       3, 1,
                                       (int) buffer->handle,
                                       (int) dwordOffset,
                                       (int) dwordCount,
                                       &out);
    if (kr == kIOReturnSuccess) {
        *outFenceSeq = (uint32_t) out;
    }
    return kr;
}

kern_return_t
wiigx2_wait_fence(WiiGX2Device *dev, uint32_t fenceSeq,
                  uint32_t timeoutMs, int *outReached)
{
    int out = 0;
    kern_return_t kr;

    if (dev == NULL) {
        return kIOReturnBadArgument;
    }
    kr = IOConnectMethodScalarIScalarO(dev->connect,
                                       kWiiGX2UCSelectorWaitFence,
                                       2, 1,
                                       (int) fenceSeq,
                                       (int) timeoutMs,
                                       &out);
    if (outReached != NULL) {
        *outReached = (kr == kIOReturnSuccess) ? (out != 0) : 0;
    }
    return kr;
}

uint32_t
wiigx2_poll_fence(const WiiGX2Device *dev)
{
    const volatile uint32_t *p;

    if (dev == NULL || dev->fenceAddr == 0) {
        return 0;
    }
    p = (const volatile uint32_t *) dev->fenceAddr;
    /* Kernel writes the fence via EVENT_WRITE_EOP which lands in memory
     * byte-swapped relative to the PPC host (same as CP writeback — see
     * OSSwapLittleToHostInt32 in WiiGX2.cpp). */
    return OSSwapLittleToHostInt32(p[0]);
}

kern_return_t
wiigx2_read_fence(WiiGX2Device *dev, uint32_t *outSeq)
{
    int out = 0;
    kern_return_t kr;

    if (dev == NULL || outSeq == NULL) {
        return kIOReturnBadArgument;
    }
    kr = IOConnectMethodScalarIScalarO(dev->connect,
                                       kWiiGX2UCSelectorReadFence,
                                       0, 1, &out);
    if (kr == kIOReturnSuccess) {
        *outSeq = (uint32_t) out;
    }
    return kr;
}

int
wiigx2_fence_reached(uint32_t observed, uint32_t target)
{
    return ((int32_t) (observed - target)) >= 0;
}
