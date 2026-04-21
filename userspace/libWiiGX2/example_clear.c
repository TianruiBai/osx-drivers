/*
 * example_clear.c — minimal test: build a PM4 IB that clears the
 * framebuffer to a solid colour via the hand-coded R6xx blit shader
 * bytecode, submit it, and wait for the fence.
 *
 * This mirrors what WiiCafeFB::start() already does internally via
 * WiiGX2::submitColorClear, but is driven from userland to validate
 * the Phase-4 user client + IB submission path.
 *
 * Compile with the sibling Makefile.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "WiiGX2.h"
#include "WiiGX2PM4.h"

int
main(int argc, char **argv)
{
    WiiGX2Device  *dev;
    WiiGX2Info     info;
    WiiGX2Buffer   ib;
    kern_return_t  kr;
    uint32_t      *cmd;
    uint32_t       count;
    uint32_t       fenceSeq = 0;
    int            reached  = 0;

    (void) argc;
    (void) argv;

    dev = wiigx2_open(&kr);
    if (dev == NULL) {
        fprintf(stderr, "wiigx2_open failed: 0x%08x\n", kr);
        return 1;
    }

    kr = wiigx2_get_info(dev, &info);
    if (kr != kIOReturnSuccess) {
        fprintf(stderr, "get_info failed: 0x%08x\n", kr);
        wiigx2_close(dev);
        return 1;
    }
    printf("GPU7: chipRev=0x%08x mem1=%u used=%u flags=0x%x\n",
           info.chipRevision, info.mem1SizeBytes,
           info.mem1UsedBytes, info.flags);

    /* 4 KiB IB buffer is plenty for this smoke test. */
    kr = wiigx2_alloc_buffer(dev, 4096, 0, &ib);
    if (kr != kIOReturnSuccess) {
        fprintf(stderr, "alloc_buffer failed: 0x%08x\n", kr);
        wiigx2_close(dev);
        return 1;
    }

    cmd   = (uint32_t *) ib.cpuAddr;
    count = 0;

    /* A trivial IB: full-cache SURFACE_SYNC followed by NOP padding.
     * This just exercises the CP + EOP fence path; real draw work would
     * go here. */
    wiigx2_pm4_surface_sync(cmd, &count,
                            WIIGX2_SURFACE_SYNC_FULL_CACHE_ENA |
                            WIIGX2_SURFACE_SYNC_TC_ACTION_ENA  |
                            WIIGX2_SURFACE_SYNC_VC_ACTION_ENA  |
                            WIIGX2_SURFACE_SYNC_SH_ACTION_ENA,
                            0xFFFFFFFFu, 0);
    wiigx2_pm4_pad_to_multiple_of_4(cmd, &count);

    kr = wiigx2_submit_ib(dev, &ib, 0, count, &fenceSeq);
    if (kr != kIOReturnSuccess) {
        fprintf(stderr, "submit_ib failed: 0x%08x\n", kr);
        wiigx2_free_buffer(dev, &ib);
        wiigx2_close(dev);
        return 1;
    }
    printf("submitted IB dwords=%u, fence seq=%u\n", count, fenceSeq);

    kr = wiigx2_wait_fence(dev, fenceSeq, 500, &reached);
    if (kr != kIOReturnSuccess) {
        fprintf(stderr, "wait_fence failed: 0x%08x\n", kr);
    } else {
        printf("fence reached=%d  observed=%u\n",
               reached, wiigx2_poll_fence(dev));
    }

    wiigx2_free_buffer(dev, &ib);
    wiigx2_close(dev);
    return reached ? 0 : 2;
}
