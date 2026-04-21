//
//  WiiGX2.hpp
//  Wii U GX2 low-level register access
//
//  Copyright © 2025 John Davis. All rights reserved.
//

#ifndef WiiGX2_hpp
#define WiiGX2_hpp

#include <IOKit/IOService.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOLib.h>

#include "GX2Regs.hpp"
#include "WiiCommon.hpp"

typedef struct {
  UInt32 chipRevision;
  UInt32 hardwareResetControl;
  UInt32 hardwareClockGate;
  UInt32 clockControl;
  UInt32 gpuInterruptStatus;
  UInt32 gpuInterruptMask;
  UInt32 indirectGrbmStatus;
  UInt32 indirectCpStatus;

  UInt32 grbmStatus;
  UInt32 grbmStatus2;
  UInt32 grbmStatusSE0;
  UInt32 grbmStatusSE1;
  UInt32 grbmStatusSE2;
  UInt32 grbmStatusSE3;
  UInt32 srbmStatus;
  UInt32 srbmStatus2;
  UInt32 configMemSize;
  UInt32 bifFramebufferEnable;

  UInt32 cpStalledStatus3;
  UInt32 cpStalledStatus1;
  UInt32 cpStalledStatus2;
  UInt32 cpBusyStatus;
  UInt32 cpStatus;
  UInt32 cpMeControl;
  UInt32 cpQueueThresholds;
  UInt32 cpMEQThresholds;
  UInt32 cpRingBase;
  UInt32 cpRingControl;
  UInt32 cpRingWritePtr;
  UInt32 cpIntControl;
  UInt32 cpIntStatus;
  UInt32 ihRingBase;
  UInt32 ihRingControl;
  UInt32 ihRingReadPtr;
  UInt32 ihRingWritePtr;
  UInt32 ihControl;
  UInt32 hdpMemCoherencyFlushControl;
  UInt32 hdpRegCoherencyFlushControl;
} WiiGX2HardwareState;

//
// Represents low-level GX2 register access.
//
class WiiGX2 : public OSObject {
  OSDeclareDefaultStructors(WiiGX2);
  WiiDeclareLogFunctions("gx2");
  typedef OSObject super;

private:
  IOMemoryMap         *_mmioMap;
  volatile void       *_mmioBaseAddr;
  IOMemoryMap         *_bridgeMap;
  volatile void       *_bridgeBaseAddr;

  // CP ring buffer.
  IOBufferMemoryDescriptor *_ringDesc;
  volatile UInt32          *_ringPtr;
  IOPhysicalAddress        _ringPhys;
  UInt32                   _ringWptr;
  bool                     _cpRunning;

  // IH ring buffer.
  IOBufferMemoryDescriptor *_ihRingDesc;
  volatile UInt32          *_ihRingPtr;
  IOPhysicalAddress        _ihRingPhys;
  IOBufferMemoryDescriptor *_ihWptrDesc;
  volatile UInt32          *_ihWptrPtr;
  IOPhysicalAddress        _ihWptrPhys;
  UInt32                   _ihRptr;
  bool                     _ihReady;
  bool                     _ihInterruptsEnabled;

  // DMA ring buffer (R6xx async DMA engine).
  IOBufferMemoryDescriptor *_dmaRingDesc;
  volatile UInt32          *_dmaRingPtr;
  IOPhysicalAddress        _dmaRingPhys;
  IOBufferMemoryDescriptor *_dmaRPtrDesc;
  volatile UInt32          *_dmaRPtrPtr;
  IOPhysicalAddress        _dmaRPtrPhys;
  UInt32                   _dmaWptr;
  bool                     _dmaReady;

  // MEM1 (EDRAM) bump heap. Physical range is fixed by the SoC: 32 MiB at
  // phys 0x00000000. Intended for render targets and hot bandwidth surfaces
  // (depth buffer, small colour targets). Allocations are monotonic — we
  // rely on a per-frame or driver-teardown reset rather than a free-list.
  IODeviceMemory           *_mem1Memory;
  IOMemoryMap              *_mem1Map;
  volatile UInt8           *_mem1Virt;
  IOPhysicalAddress        _mem1PhysBase;
  UInt32                   _mem1Size;
  UInt32                   _mem1Offset;
  IOLock                   *_mem1Lock;
  bool                     _mem1Ready;

  // MEM2 (DDR3) accounting. Every `allocateGPUBuffer()` hit lands in the
  // system contiguous pool, which is effectively MEM2 under kernel control.
  // We track the high-water mark and the current live byte count so we can
  // enforce a soft budget and produce meaningful diagnostics.
  UInt32                   _mem2LiveBytes;
  UInt32                   _mem2PeakBytes;

  bool allocateGPUBuffer(IOBufferMemoryDescriptor **bufferDesc,
    volatile UInt32 **bufferPtr, IOPhysicalAddress *bufferPhys,
    IOByteCount size, const char *label);
  void freeGPUBuffer(IOBufferMemoryDescriptor **bufferDesc,
    volatile UInt32 **bufferPtr, IOPhysicalAddress *bufferPhys);

  // Internal ring management.
  bool allocateRingBuffer(void);
  void freeRingBuffer(void);
  bool allocateIHRing(void);
  void freeIHRing(void);
  bool allocateDMARing(void);
  void freeDMARing(void);
  bool writeMemoryFenceViaCP(IOPhysicalAddress gpuAddress, UInt32 value);
  bool testCPWritebackBuffer(void);
  void ringWrite(UInt32 value);
  void ringCommit(void);

  inline UInt32 readBridgeReg32(UInt32 offset) const {
    if (_bridgeBaseAddr == NULL) {
      return 0;
    }
    return OSReadBigInt32(_bridgeBaseAddr, offset);
  }

  inline void writeBridgeReg32(UInt32 offset, UInt32 data) const {
    if (_bridgeBaseAddr == NULL) {
      return;
    }
    OSWriteBigInt32(_bridgeBaseAddr, offset, data);
  }

public:
  bool init(IOService *provider);
  void free(void);

  inline UInt32 readReg32(UInt32 offset) const {
    if (_mmioBaseAddr == NULL) {
      return 0;
    }
    return OSReadBigInt32(_mmioBaseAddr, offset);
  }

  inline void writeReg32(UInt32 offset, UInt32 data) {
    if (_mmioBaseAddr == NULL) {
      return;
    }
    OSWriteBigInt32(_mmioBaseAddr, offset, data);
  }

  IOPhysicalAddress getRegBasePhysAddr(void) const;
  IOByteCount getRegLength(void) const;
  IOPhysicalAddress getBridgePhysAddr(void) const;
  IOByteCount getBridgeLength(void) const;
  bool hasIndirectRegs(void) const;
  UInt32 getChipRevision(void) const;
  UInt32 getChipRevisionMajor(void) const;
  UInt32 getChipRevisionMinor(void) const;
  bool readGPURegIndirect32(UInt32 regOffset, UInt32 *value) const;
  bool writeGPURegIndirect32(UInt32 regOffset, UInt32 value);
  bool readIndirectReg32(UInt32 regSpace, UInt32 tileId, UInt32 regOffset, UInt32 *value) const;
  bool writeIndirectReg32(UInt32 regSpace, UInt32 tileId, UInt32 regOffset, UInt32 value);
  bool getHardwareState(WiiGX2HardwareState *state) const;
  void flushHdp(void);
  void logHardwareState(void) const;

  // Command Processor bring-up.
  void cpHalt(void);
  void cpSoftReset(void);
  bool cpResume(IOPhysicalAddress ringBase, UInt32 ringSizeLog2);
  void grbmSoftReset(UInt32 mask);
  bool waitForGRBMIdle(UInt32 timeoutUS);

  // GPU command processor lifecycle.
  bool startCP(void);
  void stopCP(void);
  bool isCPRunning(void) const { return _cpRunning; }

  // Interrupt handler ring bring-up.
  bool setupInterruptRing(bool enableInterrupts = false);
  void teardownInterruptRing(void);
  bool isInterruptRingReady(void) const { return _ihReady; }
  void setInterruptRingEnabled(bool enableInterrupts);

  // One IH ring entry (4 DWORDs, little-endian in ring memory because the
  // MC swap mode is set to 32-bit). word0 is the CP_INT_SRC_ID, word2 is
  // source-specific data; word1/word3 are reserved on R6xx/R7xx.
  struct InterruptEntry {
    UInt32 sourceID;
    UInt32 reserved1;
    UInt32 sourceData;
    UInt32 reserved3;
  };

  // Drain pending IH ring entries. If 'outEntries' is non-NULL, up to
  // 'maxEntries' are copied there. Returns the total number of entries
  // consumed from the ring. Advances IH_RB_RPTR so the ring cannot overflow
  // regardless of whether the caller snapshots the entries.
  UInt32 consumeIHRing(InterruptEntry *outEntries, UInt32 maxEntries);

  // Async DMA engine bring-up.
  bool setupDMARing(void);
  void teardownDMARing(void);
  bool isDMARingReady(void) const { return _dmaReady; }
  // Submit a simple CONSTANT_FILL (dword fill) into the DMA ring. Used as a
  // functional test. Blocks for up to ~100ms waiting for the RPTR writeback
  // to reach the post-packet WPTR.
  bool dmaConstFill(IOPhysicalAddress dstGpuAddr, UInt32 dwordValue,
                    UInt32 numDWords);

  // MEM1 (on-die EDRAM) heap management. Bump allocator on the 32 MiB
  // EDRAM aperture at physical address 0. The heap is created on demand;
  // failure of `setupMEM1Heap()` just means render targets fall back to
  // MEM2. `allocMEM1()` returns a physical + virtual pair that the GPU can
  // consume directly (via CB_COLOR*_BASE or DB_DEPTH_BASE shifted by 8).
  // `resetMEM1Heap()` discards all outstanding allocations — callers must
  // ensure the GPU is idle first.
  bool setupMEM1Heap(void);
  void teardownMEM1Heap(void);
  bool isMEM1HeapReady(void) const { return _mem1Ready; }
  bool allocMEM1(UInt32 size, UInt32 align,
                 IOPhysicalAddress *outPhys, volatile void **outVirt);
  void resetMEM1Heap(void);
  UInt32 getMEM1Used(void) const { return _mem1Offset; }
  UInt32 getMEM1Size(void) const { return _mem1Size; }

  // MEM2 (DDR3) budget / accounting hooks.
  UInt32 getMEM2LiveBytes(void) const { return _mem2LiveBytes; }
  UInt32 getMEM2PeakBytes(void) const { return _mem2PeakBytes; }

  // Command submission and sync.
  bool submitMEInitialize(void);
  bool writeScratchViaCP(UInt32 scratchIndex, UInt32 value);
  bool testCPFence(void);

  // GPU state initialization.
  bool initGPUDefaultState(void);

  // --------------------------------------------------------------------
  // Phase 3: minimal draw path — hand-coded colour-clear via the CP.
  // --------------------------------------------------------------------
  // Uploads the R6xx blit VS + PS blobs into a contiguous GPU buffer and
  // retains that staging buffer for the lifetime of the driver. Safe to
  // call multiple times — subsequent calls are no-ops. Returns false only
  // on allocation failure.
  bool setupClearPipeline(void);
  void teardownClearPipeline(void);
  bool isClearPipelineReady(void) const { return _clearReady; }

  // Submit a single full-viewport colour clear draw to `cbPhys` (must be
  // a linear 8-bit-per-channel RGBA/BGRA surface). Blocks until the EOP
  // fence is observed back in shared memory (≤100 ms). This is the Phase
  // 3 functional gate — validates CP packet stream + state + shader +
  // VS fetch + PS colour export + CB write path.
  //
  // `formatCompSwap` selects R6xx CB comp-swap:
  //   0 = standard (R,G,B,A),
  //   1 = alternate (A,R,G,B — matches BGRA host).
  bool submitColorClear(IOPhysicalAddress cbPhys,
                        UInt32 widthPixels,
                        UInt32 heightPixels,
                        UInt32 formatCompSwap,
                        const float clearRGBA[4]);

  // --------------------------------------------------------------------
  // Phase 4: userspace IB submission path.
  // --------------------------------------------------------------------
  // A single shared "user fence" page is allocated at driver start and
  // reused across every user-client submission. Each IB completion bumps
  // `_userFenceSeq`, and the CP writes that value into the first DWORD of
  // the shared page via EVENT_WRITE_EOP. Userspace maps the page read-
  // only (via WiiGX2UserClient::clientMemoryForType) and polls it.
  //
  // `submitIndirectBuffer` enqueues:
  //   INDIRECT_BUFFER(ibPhys, dwordCount)  -- executes the user IB
  //   SURFACE_SYNC(TC|VC|SH|CB|DB|full)    -- flush caches
  //   EVENT_WRITE_EOP(fencePhys, seq)      -- signal completion
  // and returns the assigned sequence number.
  bool setupUserFence(void);
  void teardownUserFence(void);
  bool isUserFenceReady(void) const { return _userFenceReady; }
  IOPhysicalAddress getUserFencePhys(void) const { return _userFencePhys; }
  IOBufferMemoryDescriptor *getUserFenceDescriptor(void) const {
    return _userFenceDesc;
  }
  UInt32 readUserFence(void) const;
  bool submitIndirectBuffer(IOPhysicalAddress ibPhys,
                            UInt32 dwordCount,
                            UInt32 *outFenceSeq);
  bool waitUserFence(UInt32 seq, UInt32 timeoutUS);

private:
  // Clear-pipeline staging buffers.
  IOBufferMemoryDescriptor *_clearVSDesc;
  volatile UInt32          *_clearVSPtr;
  IOPhysicalAddress        _clearVSPhys;
  IOBufferMemoryDescriptor *_clearPSDesc;
  volatile UInt32          *_clearPSPtr;
  IOPhysicalAddress        _clearPSPhys;
  IOBufferMemoryDescriptor *_clearVBDesc;
  volatile UInt32          *_clearVBPtr;
  IOPhysicalAddress        _clearVBPhys;
  IOBufferMemoryDescriptor *_clearFenceDesc;
  volatile UInt32          *_clearFencePtr;
  IOPhysicalAddress        _clearFencePhys;
  UInt32                   _clearFenceSeq;
  bool                     _clearReady;

  // Shared user-client fence page.
  IOBufferMemoryDescriptor *_userFenceDesc;
  volatile UInt32          *_userFencePtr;
  IOPhysicalAddress        _userFencePhys;
  UInt32                   _userFenceSeq;
  bool                     _userFenceReady;
  IOLock                   *_userSubmitLock;

  // PM4 helpers used by the draw path.
  void pm4SetContextReg(UInt32 reg, UInt32 value);
  void pm4SetContextRegs(UInt32 reg, const UInt32 *values, UInt32 count);
  void pm4SetConfigReg(UInt32 reg, UInt32 value);
  void pm4SurfaceSync(UInt32 cpCoherCntl, UInt32 size, UInt32 baseShifted);
  void pm4SetVTXResource(UInt32 slot, IOPhysicalAddress base, UInt32 sizeBytes,
                         UInt32 stride);
  void pm4WaitEopFence(IOPhysicalAddress fencePhys, UInt32 seq);
};

#endif