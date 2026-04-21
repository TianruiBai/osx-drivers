//
//  WiiGX2.cpp
//  Wii U GX2 low-level register access
//
//  Copyright © 2025 John Davis. All rights reserved.
//

#include "WiiGX2.hpp"
#include "WiiEspresso.hpp"

OSDefineMetaClassAndStructors(WiiGX2, super);

// ========================================================================
// Pre-compiled R6xx/R7xx blit shaders from the Linux radeon driver.
//
// These are hand-assembled shader programs for clear and copy operations.
// Reference: drivers/gpu/drm/radeon/r600_blit_shaders.c (MIT licensed).
//
// VS: pass-through vertex shader (fetches position, exports it).
// PS: pass-through pixel shader (exports interpolated colour).
//
// The BIG_ENDIAN variant uses BUF_SWAP_32BIT in the vertex fetch word.
// GPU7 / Latte is big-endian (PowerPC host), so we use the same bytecode
// layout the Linux radeon driver selects under __BIG_ENDIAN.
// ========================================================================

static const UInt32 kR6xxBlitVS[] = {
  0x00000004,
  0x81000000,
  0x0000203C,
  0x94000B08,
  0x00004000,
  0x14200B1A,
  0x00000000,
  0x00000000,
  0x3C000000,
  0x68CD1000,
  0x000A0000,   // BUF_SWAP_32BIT (big-endian)
  0x00000000,
};

static const UInt32 kR6xxBlitPS[] = {
  0x00000002,
  0x80800000,
  0x00000000,
  0x94200688,
  0x00000010,
  0x000D1000,
  0xB0800000,
  0x00000000,
};

static const UInt32 kR6xxBlitVSSize = sizeof(kR6xxBlitVS) / sizeof(UInt32);
static const UInt32 kR6xxBlitPSSize = sizeof(kR6xxBlitPS) / sizeof(UInt32);

bool WiiGX2::init(IOService *provider) {
  if (!super::init()) {
    return false;
  }

  WiiCheckDebugArgs();

  _mmioMap        = NULL;
  _mmioBaseAddr   = NULL;
  _bridgeMap      = NULL;
  _bridgeBaseAddr = NULL;

  _ringDesc    = NULL;
  _ringPtr     = NULL;
  _ringPhys    = 0;
  _ringWptr    = 0;
  _cpRunning   = false;

  _ihRingDesc           = NULL;
  _ihRingPtr            = NULL;
  _ihRingPhys           = 0;
  _ihWptrDesc           = NULL;
  _ihWptrPtr            = NULL;
  _ihWptrPhys           = 0;
  _ihRptr               = 0;
  _ihReady              = false;
  _ihInterruptsEnabled  = false;

  _dmaRingDesc  = NULL;
  _dmaRingPtr   = NULL;
  _dmaRingPhys  = 0;
  _dmaRPtrDesc  = NULL;
  _dmaRPtrPtr   = NULL;
  _dmaRPtrPhys  = 0;
  _dmaWptr      = 0;
  _dmaReady     = false;

  _mem1Memory    = NULL;
  _mem1Map       = NULL;
  _mem1Virt      = NULL;
  _mem1PhysBase  = kWiiGX2MEM1PhysBase;
  _mem1Size      = 0;
  _mem1Offset    = 0;
  _mem1Lock      = NULL;
  _mem1Ready     = false;

  _mem2LiveBytes = 0;
  _mem2PeakBytes = 0;

  _clearVSDesc    = NULL;
  _clearVSPtr     = NULL;
  _clearVSPhys    = 0;
  _clearPSDesc    = NULL;
  _clearPSPtr     = NULL;
  _clearPSPhys    = 0;
  _clearVBDesc    = NULL;
  _clearVBPtr     = NULL;
  _clearVBPhys    = 0;
  _clearFenceDesc = NULL;
  _clearFencePtr  = NULL;
  _clearFencePhys = 0;
  _clearFenceSeq  = 0;
  _clearReady     = false;

  _userFenceDesc  = NULL;
  _userFencePtr   = NULL;
  _userFencePhys  = 0;
  _userFenceSeq   = 0;
  _userFenceReady = false;
  _userSubmitLock = NULL;

  if (provider == NULL) {
    WIISYSLOG("No provider supplied for GX2 access");
    return false;
  }

  WiiSetDebugLocation(provider->getLocation());

  _mmioMap = provider->mapDeviceMemoryWithIndex(kWiiGX2RegisterMemoryIndex);
  if (_mmioMap == NULL) {
    WIISYSLOG("Failed to map GX2 MMIO window");
    return false;
  }
  _mmioBaseAddr = (volatile void *)_mmioMap->getVirtualAddress();

  _bridgeMap = provider->mapDeviceMemoryWithIndex(kWiiGX2BridgeMemoryIndex);
  if (_bridgeMap != NULL) {
    _bridgeBaseAddr = (volatile void *)_bridgeMap->getVirtualAddress();

    if ((readBridgeReg32(kWiiLatteRegChipRevId) & kWiiLatteChipRevMagicMask) != kWiiLatteChipRevMagicCAFE) {
      WIISYSLOG("Device memory index %u does not look like the Latte bridge window", kWiiGX2BridgeMemoryIndex);
      OSSafeReleaseNULL(_bridgeMap);
      _bridgeBaseAddr = NULL;
    }
  }

  return true;
}

void WiiGX2::free(void) {
  stopCP();
  teardownDMARing();
  teardownInterruptRing();
  teardownClearPipeline();
  teardownUserFence();
  teardownMEM1Heap();

  OSSafeReleaseNULL(_bridgeMap);
  _bridgeBaseAddr = NULL;

  OSSafeReleaseNULL(_mmioMap);
  _mmioBaseAddr = NULL;

  super::free();
}

IOPhysicalAddress WiiGX2::getRegBasePhysAddr(void) const {
  if (_mmioMap == NULL) {
    return 0;
  }

  return _mmioMap->getPhysicalAddress();
}

IOByteCount WiiGX2::getRegLength(void) const {
  if (_mmioMap == NULL) {
    return 0;
  }

  return _mmioMap->getLength();
}

IOPhysicalAddress WiiGX2::getBridgePhysAddr(void) const {
  if (_bridgeMap == NULL) {
    return 0;
  }

  return _bridgeMap->getPhysicalAddress();
}

IOByteCount WiiGX2::getBridgeLength(void) const {
  if (_bridgeMap == NULL) {
    return 0;
  }

  return _bridgeMap->getLength();
}

bool WiiGX2::hasIndirectRegs(void) const {
  return _bridgeBaseAddr != NULL;
}

UInt32 WiiGX2::getChipRevision(void) const {
  if (!hasIndirectRegs()) {
    return 0;
  }

  return readBridgeReg32(kWiiLatteRegChipRevId);
}

UInt32 WiiGX2::getChipRevisionMajor(void) const {
  UInt32 chipRevision = getChipRevision();
  return (chipRevision & kWiiLatteChipRevVersionHighMask) >> kWiiLatteChipRevVersionHighShift;
}

UInt32 WiiGX2::getChipRevisionMinor(void) const {
  return getChipRevision() & kWiiLatteChipRevVersionLowMask;
}

bool WiiGX2::readGPURegIndirect32(UInt32 regOffset, UInt32 *value) const {
  return readIndirectReg32(kWiiGX2IndirectRegSpaceGpu, kWiiGX2IndirectTileGpu, regOffset, value);
}

bool WiiGX2::writeGPURegIndirect32(UInt32 regOffset, UInt32 value) {
  return writeIndirectReg32(kWiiGX2IndirectRegSpaceGpu, kWiiGX2IndirectTileGpu, regOffset, value);
}

bool WiiGX2::readIndirectReg32(UInt32 regSpace, UInt32 tileId, UInt32 regOffset, UInt32 *value) const {
  if (!hasIndirectRegs() || value == NULL) {
    return false;
  }

  writeBridgeReg32(kWiiLatteRegGPUIndirectAddr,
    wiiGX2EncodeIndirectAddr(regSpace, tileId, regOffset));
  *value = readBridgeReg32(kWiiLatteRegGPUIndirectData);
  return true;
}

bool WiiGX2::writeIndirectReg32(UInt32 regSpace, UInt32 tileId, UInt32 regOffset, UInt32 value) {
  if (!hasIndirectRegs()) {
    return false;
  }

  writeBridgeReg32(kWiiLatteRegGPUIndirectAddr,
    wiiGX2EncodeIndirectAddr(regSpace, tileId, regOffset));
  writeBridgeReg32(kWiiLatteRegGPUIndirectData, value);
  return true;
}

bool WiiGX2::getHardwareState(WiiGX2HardwareState *state) const {
  UInt32 core;
  UInt32 interruptBase;

  if (state == NULL) {
    return false;
  }

  bzero(state, sizeof (*state));

  state->grbmStatus                   = readReg32(kWiiGX2RegGRBMStatus);
  state->grbmStatus2                  = readReg32(kWiiGX2RegGRBMStatus2);
  state->grbmStatusSE0                = readReg32(kWiiGX2RegGRBMStatusSE0);
  state->grbmStatusSE1                = readReg32(kWiiGX2RegGRBMStatusSE1);
  state->grbmStatusSE2                = readReg32(kWiiGX2RegGRBMStatusSE2);
  state->grbmStatusSE3                = readReg32(kWiiGX2RegGRBMStatusSE3);
  state->srbmStatus                   = readReg32(kWiiGX2RegSRBMStatus);
  state->srbmStatus2                  = readReg32(kWiiGX2RegSRBMStatus2);
  state->configMemSize                = readReg32(kWiiGX2RegConfigMemSize);
  state->bifFramebufferEnable         = readReg32(kWiiGX2RegBifFramebufferEnable);

  state->cpStalledStatus3             = readReg32(kWiiGX2RegCPStalledStatus3);
  state->cpStalledStatus1             = readReg32(kWiiGX2RegCPStalledStatus1);
  state->cpStalledStatus2             = readReg32(kWiiGX2RegCPStalledStatus2);
  state->cpBusyStatus                 = readReg32(kWiiGX2RegCPBusyStatus);
  state->cpStatus                     = readReg32(kWiiGX2RegCPStatus);
  state->cpMeControl                  = readReg32(kWiiGX2RegCPMeControl);
  state->cpQueueThresholds            = readReg32(kWiiGX2RegCPQueueThresholds);
  state->cpMEQThresholds              = readReg32(kWiiGX2RegCPMEQThresholds);
  state->cpRingBase                   = readReg32(kWiiGX2RegCPRingBase);
  state->cpRingControl                = readReg32(kWiiGX2RegCPRingControl);
  state->cpRingWritePtr               = readReg32(kWiiGX2RegCPRingWritePtr);
  state->cpIntControl                 = readReg32(kWiiGX2RegCPIntControl);
  state->cpIntStatus                  = readReg32(kWiiGX2RegCPIntStatus);
  state->ihRingBase                   = readReg32(kWiiGX2RegIHRBBase);
  state->ihRingControl                = readReg32(kWiiGX2RegIHRBCntl);
  state->ihRingReadPtr                = readReg32(kWiiGX2RegIHRBRPtr);
  state->ihRingWritePtr               = readReg32(kWiiGX2RegIHRBWPtr);
  state->ihControl                    = readReg32(kWiiGX2RegIHCntl);
  state->hdpMemCoherencyFlushControl  = readReg32(kWiiGX2RegHdpMemCoherencyFlushControl);
  state->hdpRegCoherencyFlushControl  = readReg32(kWiiGX2RegHdpRegCoherencyFlushControl);

  if (!hasIndirectRegs()) {
    return true;
  }

  state->chipRevision         = readBridgeReg32(kWiiLatteRegChipRevId);
  state->hardwareResetControl = readBridgeReg32(kWiiLatteRegHardwareResetControl);
  state->hardwareClockGate    = readBridgeReg32(kWiiLatteRegHardwareClockGate);
  state->clockControl         = readBridgeReg32(kWiiLatteRegClockControl);

  core = mfEspressoSPR(kEspressoSPR_PIR);
  if (core < 3) {
    interruptBase = kWiiLatteRegPPCInterruptBase + (core * kWiiLatteRegPPCInterruptStride);
    state->gpuInterruptStatus = readBridgeReg32(interruptBase + kWiiLatteRegPPCInterruptCause1);
    state->gpuInterruptMask   = readBridgeReg32(interruptBase + kWiiLatteRegPPCInterruptMask1);
  }

  (void) readGPURegIndirect32(kWiiGX2RegGRBMStatus, &state->indirectGrbmStatus);
  (void) readGPURegIndirect32(kWiiGX2RegCPStatus, &state->indirectCpStatus);

  return true;
}

void WiiGX2::flushHdp(void) {
  writeReg32(kWiiGX2RegHdpMemCoherencyFlushControl, 1);
  (void) readReg32(kWiiGX2RegHdpMemCoherencyFlushControl);
  writeReg32(kWiiGX2RegHdpRegCoherencyFlushControl, 1);
  (void) readReg32(kWiiGX2RegHdpRegCoherencyFlushControl);
}

void WiiGX2::logHardwareState(void) const {
  WiiGX2HardwareState state;

  WIIDBGLOG("GX2 MMIO mapped to %p (physical 0x%X), length: 0x%X", _mmioBaseAddr,
    getRegBasePhysAddr(), getRegLength());

  if (!getHardwareState(&state)) {
    WIISYSLOG("Failed to snapshot GX2 state");
    return;
  }

  WIIDBGLOG("Display control: 0x%08X, enable: 0x%08X, memsize: 0x%08X, bif_fb_en: 0x%08X",
    readReg32(kWiiGX2RegD1GrphControl), readReg32(kWiiGX2RegD1GrphEnable),
    state.configMemSize, state.bifFramebufferEnable);
  WIISYSLOG("GRBM: status=0x%08X status2=0x%08X se0=0x%08X se1=0x%08X se2=0x%08X se3=0x%08X",
    state.grbmStatus, state.grbmStatus2, state.grbmStatusSE0, state.grbmStatusSE1,
    state.grbmStatusSE2, state.grbmStatusSE3);
  WIIDBGLOG("SRBM: status=0x%08X status2=0x%08X", state.srbmStatus, state.srbmStatus2);
  WIISYSLOG("CP: stat=0x%08X busy=0x%08X stalled1=0x%08X stalled2=0x%08X stalled3=0x%08X me=0x%08X",
    state.cpStatus, state.cpBusyStatus, state.cpStalledStatus1, state.cpStalledStatus2,
    state.cpStalledStatus3, state.cpMeControl);
  WIIDBGLOG("CP ring/int: rb_base=0x%08X rb_cntl=0x%08X rb_wptr=0x%08X int_cntl=0x%08X int_stat=0x%08X",
    state.cpRingBase, state.cpRingControl, state.cpRingWritePtr, state.cpIntControl, state.cpIntStatus);
  WIIDBGLOG("IH: rb_base=0x%08X rb_cntl=0x%08X rptr=0x%08X wptr=0x%08X cntl=0x%08X",
    state.ihRingBase, state.ihRingControl, state.ihRingReadPtr, state.ihRingWritePtr, state.ihControl);
  WIIDBGLOG("HDP flush: mem=0x%08X reg=0x%08X", state.hdpMemCoherencyFlushControl,
    state.hdpRegCoherencyFlushControl);

  if (!hasIndirectRegs()) {
    WIIDBGLOG("No Latte bridge mapping exposed, indirect GX2 probing unavailable");
    return;
  }

  WIIDBGLOG("Latte bridge mapped to %p (physical 0x%X), length: 0x%X", _bridgeBaseAddr,
    getBridgePhysAddr(), getBridgeLength());
  WIISYSLOG("Latte chip revision: 0x%08X (major 0x%02X minor 0x%02X)", state.chipRevision,
    getChipRevisionMajor(), getChipRevisionMinor());
  WIIDBGLOG("Latte gfx gate/reset: clkgate=0x%08X rstctrl=0x%08X clkctrl=0x%08X",
    state.hardwareClockGate, state.hardwareResetControl, state.clockControl);
  WIIDBGLOG("Indirect GPU probe: grbm=0x%08X cp_stat=0x%08X", state.indirectGrbmStatus,
    state.indirectCpStatus);
  WIIDBGLOG("GPU IRQ state: status=0x%08X mask=0x%08X gpu7=%u",
    state.gpuInterruptStatus, state.gpuInterruptMask,
    ((state.gpuInterruptStatus & state.gpuInterruptMask & kWiiLatteIRQ2GPU7GC) != 0));
}

// ========================================================================
// Command Processor bring-up helpers.
//
// These follow the R600/RV770 CP initialization sequence from the
// Linux radeon driver (drivers/gpu/drm/radeon/r600.c, rv770.c).
//
// Wii U GPU7 is an R700-class part (TeraScale / Radeon HD 4000 series).
// ========================================================================

void WiiGX2::cpHalt(void) {
  // Halt both the PFP (Pre-Fetch Parser) and the ME (Micro Engine).
  writeReg32(kWiiGX2RegCPMeControl,
    kWiiGX2RegCPMeControlPFPHalt | kWiiGX2RegCPMeControlMEHalt);
  (void) readReg32(kWiiGX2RegCPMeControl);
  WIIDBGLOG("CP halted (ME+PFP)");
}

void WiiGX2::grbmSoftReset(UInt32 mask) {
  // Read GRBM_STATUS for a fence, apply soft-reset, wait, then clear.
  (void) readReg32(kWiiGX2RegGRBMStatus);

  writeReg32(kWiiGX2RegGRBMSoftReset, mask);
  (void) readReg32(kWiiGX2RegGRBMSoftReset);

  // The Linux driver uses udelay(50) after assert + clear.
  IODelay(50);

  writeReg32(kWiiGX2RegGRBMSoftReset, 0);
  (void) readReg32(kWiiGX2RegGRBMSoftReset);

  IODelay(50);
  WIIDBGLOG("GRBM soft-reset 0x%08X applied", mask);
}

void WiiGX2::cpSoftReset(void) {
  cpHalt();
  IODelay(100);
  grbmSoftReset(kWiiGX2GRBMSoftResetCP);
}

bool WiiGX2::waitForGRBMIdle(UInt32 timeoutUS) {
  const UInt32 pollInterval = 10;
  UInt32 elapsed = 0;

  while (elapsed < timeoutUS) {
    UInt32 status = readReg32(kWiiGX2RegGRBMStatus);
    if ((status & kWiiGX2RegGRBMStatusGUIActive) == 0 &&
        (status & kWiiGX2RegGRBMStatusCPBusy) == 0) {
      return true;
    }
    IODelay(pollInterval);
    elapsed += pollInterval;
  }

  WIISYSLOG("GRBM idle wait timed out after %u us (status 0x%08X)",
    timeoutUS, readReg32(kWiiGX2RegGRBMStatus));
  return false;
}

bool WiiGX2::cpResume(IOPhysicalAddress ringBase, UInt32 ringSizeLog2) {
  UInt32 rbCntl;

  if (ringSizeLog2 < 8 || ringSizeLog2 > 23) {
    WIISYSLOG("Invalid ring size log2 %u (must be 8..23)", ringSizeLog2);
    return false;
  }

  // 1. Halt the CP (ME + PFP). On R6xx/R7xx a GRBM soft-reset of the CP
  //    sub-block clears the ME and PFP micro-code that was loaded by the
  //    boot firmware (IOSU/Cafe OS). Because this driver does not ship
  //    CP micro-code, re-asserting SOFT_RESET_CP here leaves the CP
  //    unable to execute PM4 packets (observed in the field: every
  //    testCPFence() write to SCRATCH_REG0 stalls at 0 and times out).
  //
  //    The Linux radeon driver calls r600_load_microcode() *after* the
  //    soft-reset, which is the piece we are missing. Until we can
  //    either reuse the Cafe-OS-loaded micro-code dump or supply our own
  //    firmware blob, we preserve boot-firmware state: only halt ME+PFP
  //    so we can safely re-program the ring registers, then un-halt.
  //
  //    Passing `-wiigx2reset` on the XNU command line forces the old
  //    soft-reset path for diagnostic purposes (and will not currently
  //    pass the CP fence test).
  if (checkKernelArgument("-wiigx2reset")) {
    WIIDBGLOG("CP: forcing GRBM soft-reset (-wiigx2reset)");
    cpSoftReset();
  } else {
    cpHalt();
    IODelay(100);
  }

  // 2. Configure ring buffer control.
  //    BUF_SWAP_32BIT is required for big-endian (PowerPC) hosts.
  rbCntl = (ringSizeLog2 << kWiiGX2RegCPRingControlBufferSizeShift) &
            kWiiGX2RegCPRingControlBufferSizeMask;
  rbCntl |= (2 << kWiiGX2RegCPRingControlBlockSizeShift) &
              kWiiGX2RegCPRingControlBlockSizeMask;
  rbCntl |= kWiiGX2RegCPRingControlBufferSwap32;
  rbCntl |= kWiiGX2RegCPRingControlReadPtrWriteEnable;
  writeReg32(kWiiGX2RegCPRingControl, rbCntl);

  // 3. Clear read and write pointers.
  writeReg32(kWiiGX2RegCPRingReadPtrWrite, 0);
  writeReg32(kWiiGX2RegCPRingWritePtr, 0);

  // 4. Set the ring buffer base address (256-byte aligned GPU address).
  writeReg32(kWiiGX2RegCPRingBase, (UInt32)(ringBase >> 8));

  // 5. Fence: read back the base to ensure the write has landed.
  (void) readReg32(kWiiGX2RegCPRingBase);

  // 6. The caller has already allocated and zeroed the ring; PM4 packets are
  //    submitted once the CP is un-halted.
  WIISYSLOG("CP ring programmed: base=0x%08X cntl=0x%08X sizeLog2=%u (%u DW)",
    (UInt32)(ringBase >> 8), rbCntl, ringSizeLog2, (1u << ringSizeLog2));

  // 7. Remove RB_NO_UPDATE so the CP can advance RPTR.
  rbCntl &= ~kWiiGX2RegCPRingControlNoUpdate;
  writeReg32(kWiiGX2RegCPRingControl, rbCntl);

  // 8. Un-halt the ME and PFP.
  writeReg32(kWiiGX2RegCPMeControl, 0);
  (void) readReg32(kWiiGX2RegCPMeControl);

  WIISYSLOG("CP resumed, ME+PFP un-halted");

  // 9. Wait briefly for the CP to become idle after initialization.
  if (!waitForGRBMIdle(15000)) {
    WIISYSLOG("CP did not reach idle after resume");
    return false;
  }

  WIIDBGLOG("CP idle after resume, initialization complete");
  return true;
}

// ========================================================================
// Ring buffer management.
// ========================================================================

bool WiiGX2::allocateGPUBuffer(IOBufferMemoryDescriptor **bufferDesc,
  volatile UInt32 **bufferPtr, IOPhysicalAddress *bufferPhys,
  IOByteCount size, const char *label) {
  IOByteCount length = 0;
  const char *bufferLabel = (label != NULL) ? label : "GPU";

  if (bufferDesc == NULL || bufferPtr == NULL || bufferPhys == NULL || size == 0) {
    return false;
  }

  *bufferDesc = IOBufferMemoryDescriptor::withOptions(
    kIOMemoryPhysicallyContiguous, size, PAGE_SIZE);
  if (*bufferDesc == NULL) {
    WIISYSLOG("Failed to allocate %u-byte %s buffer", (UInt32)size, bufferLabel);
    return false;
  }

  *bufferPtr = (volatile UInt32 *)(*bufferDesc)->getBytesNoCopy();
  *bufferPhys = (*bufferDesc)->getPhysicalSegment(0, &length);
  if (*bufferPtr == NULL || *bufferPhys == 0 || length < size) {
    WIISYSLOG("%s buffer mapping invalid (ptr=%p phys=0x%08X len=%u)",
      bufferLabel, *bufferPtr, (UInt32)*bufferPhys, (UInt32)length);
    OSSafeReleaseNULL(*bufferDesc);
    *bufferPtr = NULL;
    *bufferPhys = 0;
    return false;
  }

  bzero((void *)*bufferPtr, size);
  OSSynchronizeIO();

  // MEM2 accounting — IOBufferMemoryDescriptor with
  // kIOMemoryPhysicallyContiguous currently falls in the DDR3 system heap
  // (MEM2) under IOKit. Track high-water mark + live bytes so we can warn
  // when the driver-internal budget creeps up.
  _mem2LiveBytes += (UInt32) size;
  if (_mem2LiveBytes > _mem2PeakBytes) {
    _mem2PeakBytes = _mem2LiveBytes;
  }
  if (_mem2LiveBytes > kWiiGX2MEM2BudgetBytes) {
    WIISYSLOG("MEM2 driver budget exceeded (live=%u, peak=%u, cap=%u)",
      _mem2LiveBytes, _mem2PeakBytes, (UInt32) kWiiGX2MEM2BudgetBytes);
  }

  WIIDBGLOG("%s buffer: virt=%p phys=0x%08X size=%u (MEM2 live=%u)",
    bufferLabel, *bufferPtr, (UInt32)*bufferPhys, (UInt32)size,
    _mem2LiveBytes);
  return true;
}

void WiiGX2::freeGPUBuffer(IOBufferMemoryDescriptor **bufferDesc,
  volatile UInt32 **bufferPtr, IOPhysicalAddress *bufferPhys) {
  if (bufferDesc == NULL || bufferPtr == NULL || bufferPhys == NULL) {
    return;
  }

  if (*bufferDesc != NULL) {
    UInt32 size = (UInt32) (*bufferDesc)->getLength();
    if (size <= _mem2LiveBytes) {
      _mem2LiveBytes -= size;
    } else {
      _mem2LiveBytes = 0;
    }
  }

  OSSafeReleaseNULL(*bufferDesc);
  *bufferPtr = NULL;
  *bufferPhys = 0;
}

bool WiiGX2::allocateRingBuffer(void) {
  if (!allocateGPUBuffer(&_ringDesc, &_ringPtr, &_ringPhys,
      kWiiGX2CPRingBufferBytes, "CP ring")) {
    return false;
  }

  _ringWptr = 0;
  return true;
}

void WiiGX2::freeRingBuffer(void) {
  freeGPUBuffer(&_ringDesc, &_ringPtr, &_ringPhys);
  _ringWptr = 0;
}

bool WiiGX2::allocateIHRing(void) {
  if (!allocateGPUBuffer(&_ihRingDesc, &_ihRingPtr, &_ihRingPhys,
      kWiiGX2IHRingBufferBytes, "IH ring")) {
    return false;
  }

  if (!allocateGPUBuffer(&_ihWptrDesc, &_ihWptrPtr, &_ihWptrPhys,
      kWiiGX2IHWPtrWritebackBytes, "IH writeback")) {
    freeIHRing();
    return false;
  }

  _ihRptr = 0;
  return true;
}

void WiiGX2::freeIHRing(void) {
  freeGPUBuffer(&_ihWptrDesc, &_ihWptrPtr, &_ihWptrPhys);
  freeGPUBuffer(&_ihRingDesc, &_ihRingPtr, &_ihRingPhys);
  _ihRptr = 0;
}

void WiiGX2::setInterruptRingEnabled(bool enableInterrupts) {
  UInt32 interruptCntl;
  UInt32 ihCntl;
  UInt32 ihRbCntl;

  if (!_ihReady) {
    return;
  }

  interruptCntl = readReg32(kWiiGX2RegInterruptCntl);
  ihCntl        = readReg32(kWiiGX2RegIHCntl);
  ihRbCntl      = readReg32(kWiiGX2RegIHRBCntl);

  if (enableInterrupts) {
    interruptCntl |= kWiiGX2InterruptCntlGenIHIntEn;
    ihCntl |= kWiiGX2IHCntlEnableIntr;
    ihRbCntl |= kWiiGX2IHRBEnable;
  } else {
    interruptCntl &= ~kWiiGX2InterruptCntlGenIHIntEn;
    ihCntl &= ~kWiiGX2IHCntlEnableIntr;
    ihRbCntl &= ~kWiiGX2IHRBEnable;
  }

  writeReg32(kWiiGX2RegInterruptCntl, interruptCntl);
  writeReg32(kWiiGX2RegIHCntl, ihCntl);
  writeReg32(kWiiGX2RegIHRBCntl, ihRbCntl);
  (void) readReg32(kWiiGX2RegIHRBCntl);

  _ihInterruptsEnabled = enableInterrupts;
}

void WiiGX2::ringWrite(UInt32 value) {
  if (_ringPtr == NULL) {
    return;
  }
  _ringPtr[_ringWptr] = value;
  _ringWptr = (_ringWptr + 1) & kWiiGX2CPRingBufferDWordMask;
}

void WiiGX2::ringCommit(void) {
  // Ensure CPU writes to the ring buffer are visible to the GPU.
  OSSynchronizeIO();
  flushHdp();

  // Update the hardware write pointer.
  writeReg32(kWiiGX2RegCPRingWritePtr, _ringWptr);
  (void)readReg32(kWiiGX2RegCPRingWritePtr);
}

bool WiiGX2::setupInterruptRing(bool enableInterrupts) {
  UInt32 ihRbCntl;
  UInt32 ihCntl;

  if (_ihReady) {
    setInterruptRingEnabled(enableInterrupts);
    return true;
  }

  if (!allocateIHRing()) {
    return false;
  }

  writeReg32(kWiiGX2RegInterruptCntl,
    readReg32(kWiiGX2RegInterruptCntl) & ~kWiiGX2InterruptCntlGenIHIntEn);
  writeReg32(kWiiGX2RegIHRBCntl, 0);
  writeReg32(kWiiGX2RegIHCntl, 0);
  writeReg32(kWiiGX2RegIHRBRPtr, 0);
  writeReg32(kWiiGX2RegIHRBWPtr, 0);

  writeReg32(kWiiGX2RegIHRBBase, (UInt32)(_ihRingPhys >> 8));
  writeReg32(kWiiGX2RegIHRBWPtrAddrLo, (UInt32)((UInt64)_ihWptrPhys & 0xFFFFFFFFULL));
  writeReg32(kWiiGX2RegIHRBWPtrAddrHi, (UInt32)(((UInt64)_ihWptrPhys >> 32) & 0xFFFFFFFFULL));

  ihRbCntl = WiiGX2IHRBCntlSize(kWiiGX2IHRingBufSizeLog2DWords) |
             kWiiGX2IHWPtrWritebackEnable |
             kWiiGX2IHWPtrOverflowEnable |
             kWiiGX2IHWPtrOverflowClear;
  writeReg32(kWiiGX2RegIHRBCntl, ihRbCntl);

  ihCntl = WiiGX2IHCntlMCSwap(kWiiGX2IHCntlMCSwap32Bit) |
           WiiGX2IHCntlMCWrReqCredit(0x10) |
           WiiGX2IHCntlMCWrCleanCnt(0x10);
  writeReg32(kWiiGX2RegIHCntl, ihCntl);
  (void) readReg32(kWiiGX2RegIHCntl);

  _ihRptr = 0;
  _ihReady = true;
  _ihInterruptsEnabled = false;

  setInterruptRingEnabled(enableInterrupts);

  WIIDBGLOG("IH ring %s: base=0x%08X size=%u wptr_wb=0x%08X",
    enableInterrupts ? "enabled" : "staged",
    (UInt32)(_ihRingPhys >> 8), kWiiGX2IHRingBufferBytes, (UInt32)_ihWptrPhys);
  return true;
}

void WiiGX2::teardownInterruptRing(void) {
  if (!_ihReady && _ihRingDesc == NULL && _ihWptrDesc == NULL) {
    return;
  }

  if (_ihReady) {
    setInterruptRingEnabled(false);
  }

  writeReg32(kWiiGX2RegIHRBRPtr, 0);
  writeReg32(kWiiGX2RegIHRBWPtr, 0);
  writeReg32(kWiiGX2RegIHRBCntl, 0);
  writeReg32(kWiiGX2RegIHCntl, 0);
  writeReg32(kWiiGX2RegIHRBWPtrAddrLo, 0);
  writeReg32(kWiiGX2RegIHRBWPtrAddrHi, 0);
  writeReg32(kWiiGX2RegIHRBBase, 0);
  (void) readReg32(kWiiGX2RegIHRBCntl);

  _ihReady = false;
  _ihInterruptsEnabled = false;
  freeIHRing();

  WIIDBGLOG("IH ring torn down");
}

//
// Drain pending IH ring entries.
//
// R6xx/R7xx writes 4-DWORD entries into the IH ring. Because
// IH_CNTL.MC_SWAP is programmed to 32-bit, entries sitting in DDR3 are
// little-endian as far as the PPC is concerned, so we must byte-swap on
// read (matches the writeback fence path in testCPWritebackBuffer).
//
// Concurrency note: this routine is reentrancy-safe against the GPU
// writer because we only read up to the writeback WPTR snapshot captured
// once per call, and we advance IH_RB_RPTR *after* draining. It is NOT
// safe to call simultaneously from multiple host CPUs on the same
// WiiGX2 instance; the caller must serialize via whatever lock owns the
// GPU7 interrupt path on Espresso (LatteInterruptController's per-vector
// queue currently provides that guarantee).
//
UInt32 WiiGX2::consumeIHRing(InterruptEntry *outEntries, UInt32 maxEntries) {
  if (!_ihReady || _ihRingPtr == NULL || _ihWptrPtr == NULL) {
    return 0;
  }

  // The GPU writes the ring bytes via MC and the writeback WPTR via a
  // separate MC coherent channel; make sure we observe both using the
  // CPU-side HDP flush before snapshotting.
  flushHdp();
  OSSynchronizeIO();

  // Writeback pointer is stored by the MC as a byte offset into the ring,
  // little-endian.
  UInt32 wptrBytes = OSSwapLittleToHostInt32(_ihWptrPtr[0]) & kWiiGX2IHRingPtrMask;

  // _ihRptr is our shadow; the hardware IH_RB_RPTR register is a byte
  // offset in the same space. They must agree modulo the ring size at
  // rest.
  UInt32 rptrBytes = _ihRptr & kWiiGX2IHRingPtrMask;

  if (rptrBytes == wptrBytes) {
    return 0;
  }

  UInt32 consumed = 0;
  while (rptrBytes != wptrBytes) {
    UInt32 dwordIndex = rptrBytes >> 2;

    InterruptEntry entry;
    entry.sourceID   = OSSwapLittleToHostInt32(_ihRingPtr[dwordIndex + 0]);
    entry.reserved1  = OSSwapLittleToHostInt32(_ihRingPtr[dwordIndex + 1]);
    entry.sourceData = OSSwapLittleToHostInt32(_ihRingPtr[dwordIndex + 2]);
    entry.reserved3  = OSSwapLittleToHostInt32(_ihRingPtr[dwordIndex + 3]);

    if (outEntries != NULL && consumed < maxEntries) {
      outEntries[consumed] = entry;
    }
    consumed++;

    rptrBytes = (rptrBytes + kWiiGX2IHEntryBytes) & kWiiGX2IHRingPtrMask;
  }

  _ihRptr = rptrBytes;

  // Publish the new read pointer to the GPU so the ring slots are
  // reclaimed and the overflow bit can clear.
  writeReg32(kWiiGX2RegIHRBRPtr, rptrBytes);
  (void) readReg32(kWiiGX2RegIHRBRPtr);

  return consumed;
}

// ========================================================================
// Async DMA engine bring-up.
//
// R6xx/R7xx has one async DMA engine at 0xD000. Programming follows
// Linux drivers/gpu/drm/radeon/r600_dma.c:r600_dma_resume():
//   1. Clear DMA_SEM_INCOMPLETE / DMA_SEM_WAIT_FAIL timers.
//   2. RB_CNTL = (log2(ring_dwords) << 1) | BE swap bits.
//   3. Zero RPTR / WPTR.
//   4. Program RPTR writeback address (low32 to LO, high8 to HI mask).
//   5. RB_CNTL |= RPTR_WRITEBACK_ENABLE.
//   6. RB_BASE = ring_phys >> 8.
//   7. IB_CNTL = IB_ENABLE (+ IB_SWAP on PPC).
//   8. DMA_CNTL &= ~CTXEMPTY_INT_ENABLE (we also mask TRAP until IH hook).
//   9. RV770+: DMA_MODE = 1.
//  10. RB_CNTL |= RB_ENABLE.
// Endianness on PPC: must set RB_SWAP, RPTR_WRITEBACK_SWAP, IB_SWAP, and
// DATA/FENCE swap in DMA_CNTL — else the DMA FIFO treats the DWORDs we
// write as little-endian.
// ========================================================================

bool WiiGX2::allocateDMARing(void) {
  if (!allocateGPUBuffer(&_dmaRingDesc, &_dmaRingPtr, &_dmaRingPhys,
      kWiiGX2DMARingBufferBytes, "DMA ring")) {
    return false;
  }

  if (!allocateGPUBuffer(&_dmaRPtrDesc, &_dmaRPtrPtr, &_dmaRPtrPhys,
      kWiiGX2DMARPtrWritebackBytes, "DMA RPTR writeback")) {
    freeDMARing();
    return false;
  }

  _dmaWptr = 0;
  return true;
}

void WiiGX2::freeDMARing(void) {
  freeGPUBuffer(&_dmaRPtrDesc, &_dmaRPtrPtr, &_dmaRPtrPhys);
  freeGPUBuffer(&_dmaRingDesc, &_dmaRingPtr, &_dmaRingPhys);
  _dmaWptr = 0;
}

bool WiiGX2::setupDMARing(void) {
  UInt32 rbCntl;
  UInt32 ibCntl;
  UInt32 dmaCntl;
  UInt32 rbBufSizeLog2;

  if (_dmaReady) {
    return true;
  }

  if (!allocateDMARing()) {
    return false;
  }

  // Zero semaphore timers — Linux drops these before touching the ring.
  writeReg32(kWiiGX2RegDMASemIncompleteTimerCntl, 0);
  writeReg32(kWiiGX2RegDMASemWaitFailTimerCntl, 0);

  // RB_CNTL size field is log2(ring_dwords). Our ring is 64KB = 16384 DWs ⇒ 14.
  rbBufSizeLog2 = 14;
  rbCntl = WiiGX2DMARBCntlSize(rbBufSizeLog2) |
           kWiiGX2DMARBSwapEnable |
           kWiiGX2DMARPtrWritebackSwapEnable;
  writeReg32(kWiiGX2RegDMARBCntl, rbCntl);

  writeReg32(kWiiGX2RegDMARBRPtr, 0);
  writeReg32(kWiiGX2RegDMARBWPtr, 0);

  // Per r600d.h: RPTR_ADDR_HI takes the upper 32 bits masked to 0xFF (so the
  // GPU can only reach 40-bit phys). On PPC the writeback dword itself is
  // little-endian thanks to RPTR_WRITEBACK_SWAP_ENABLE.
  writeReg32(kWiiGX2RegDMARBRPtrAddrHi,
    (UInt32)(((UInt64)_dmaRPtrPhys >> 32) & 0xFFU));
  writeReg32(kWiiGX2RegDMARBRPtrAddrLo,
    (UInt32)((UInt64)_dmaRPtrPhys & 0xFFFFFFFCU));

  rbCntl |= kWiiGX2DMARPtrWritebackEnable;
  writeReg32(kWiiGX2RegDMARBCntl, rbCntl);

  writeReg32(kWiiGX2RegDMARBBase, (UInt32)(_dmaRingPhys >> 8));

  ibCntl = kWiiGX2DMAIBEnable | kWiiGX2DMAIBSwapEnable;
  writeReg32(kWiiGX2RegDMAIBCntl, ibCntl);

  // Mask all DMA-originated interrupts for now (trap, sem, ctx-empty). The
  // IH ring consumer will enable TRAP once the GPU7 vector is live.
  dmaCntl = readReg32(kWiiGX2RegDMACntl);
  dmaCntl &= ~(kWiiGX2DMACntlTrapEnable |
               kWiiGX2DMACntlSemIncompleteIntEnable |
               kWiiGX2DMACntlSemWaitIntEnable |
               kWiiGX2DMACntlCtxEmptyIntEnable);
  // Required for BE payloads: tell the DMA engine to byte-swap data and
  // fence dwords it writes back to memory.
  dmaCntl |= kWiiGX2DMACntlDataSwapEnable | kWiiGX2DMACntlFenceSwapEnable;
  writeReg32(kWiiGX2RegDMACntl, dmaCntl);

  // RV770+ (which includes RV710/GPU7) needs DMA_MODE = 1 per rv770 path.
  writeReg32(kWiiGX2RegDMAMode, 1);

  // Finally, enable the ring.
  writeReg32(kWiiGX2RegDMARBCntl, rbCntl | kWiiGX2DMARBEnable);
  (void) readReg32(kWiiGX2RegDMARBCntl);

  _dmaReady = true;
  _dmaWptr = 0;

  WIIDBGLOG("DMA ring up: base=0x%08X size=%u wb=0x%08X",
    (UInt32)(_dmaRingPhys >> 8),
    kWiiGX2DMARingBufferBytes, (UInt32)_dmaRPtrPhys);
  return true;
}

void WiiGX2::teardownDMARing(void) {
  if (!_dmaReady && _dmaRingDesc == NULL && _dmaRPtrDesc == NULL) {
    return;
  }

  if (_dmaReady) {
    UInt32 rbCntl = readReg32(kWiiGX2RegDMARBCntl);
    rbCntl &= ~kWiiGX2DMARBEnable;
    writeReg32(kWiiGX2RegDMARBCntl, rbCntl);

    writeReg32(kWiiGX2RegDMAIBCntl, 0);
    writeReg32(kWiiGX2RegDMARBRPtr, 0);
    writeReg32(kWiiGX2RegDMARBWPtr, 0);
    writeReg32(kWiiGX2RegDMARBRPtrAddrHi, 0);
    writeReg32(kWiiGX2RegDMARBRPtrAddrLo, 0);
    writeReg32(kWiiGX2RegDMARBBase, 0);
    (void) readReg32(kWiiGX2RegDMARBCntl);
  }

  _dmaReady = false;
  freeDMARing();
  WIIDBGLOG("DMA ring torn down");
}

//
// Submit a CONSTANT_FILL packet. This is the DMA-engine equivalent of
// memset-dword and is the simplest functional test we can run before
// wiring up COPY packets for PBOs. Blocks up to ~100ms for completion.
//
bool WiiGX2::dmaConstFill(IOPhysicalAddress dstGpuAddr, UInt32 dwordValue,
                          UInt32 numDWords) {
  UInt32 writeDwords;
  UInt32 slot;
  UInt32 timeoutMs;
  UInt32 rptrDwords;
  UInt32 postWptrDwords;

  if (!_dmaReady || _dmaRingPtr == NULL || numDWords == 0) {
    return false;
  }

  // R6xx CONSTANT_FILL layout (big-endian dword stream, with DATA_SWAP in
  // DMA_CNTL handling the payload swap for us):
  //   DW0: header  (type=0xD, count = #dwords to fill)
  //   DW1: dst addr low
  //   DW2: dst addr high (top bits of 40-bit phys)
  //   DW3: fill value
  // The count field is up to 0xFFFFF; we keep num small for the test.
  if (numDWords > 0xFFFFU) {
    numDWords = 0xFFFFU;
  }
  writeDwords = 4;

  // Ensure we have enough room from the current WPTR to the end of the ring.
  // If not, emit NOPs to wrap. The R600 ring is strictly monotonic per
  // packet — packets must not straddle the wrap.
  if (_dmaWptr + writeDwords > kWiiGX2DMARingBufferDWords) {
    UInt32 pad = kWiiGX2DMARingBufferDWords - _dmaWptr;
    for (UInt32 i = 0; i < pad; i++) {
      _dmaRingPtr[_dmaWptr] = WiiGX2DMAPacket(kWiiGX2DMAPacketNOP, 0, 0, 0);
      _dmaWptr = (_dmaWptr + 1) & kWiiGX2DMARingBufferDWordMask;
    }
  }

  slot = _dmaWptr;
  _dmaRingPtr[slot + 0] = WiiGX2DMAPacket(kWiiGX2DMAPacketConstFill, 0,
                                          (numDWords & 0xFFFF), 0);
  _dmaRingPtr[slot + 1] = (UInt32)((UInt64)dstGpuAddr & 0xFFFFFFFFU);
  _dmaRingPtr[slot + 2] = (UInt32)(((UInt64)dstGpuAddr >> 32) & 0xFFU);
  _dmaRingPtr[slot + 3] = dwordValue;

  _dmaWptr = (_dmaWptr + writeDwords) & kWiiGX2DMARingBufferDWordMask;
  postWptrDwords = _dmaWptr;

  // Publish to GPU: CPU-side flush + HDP flush + WPTR register.
  OSSynchronizeIO();
  flushHdp();
  // DMA WPTR is in DWORDs shifted left by 2 (byte offset) on R6xx.
  writeReg32(kWiiGX2RegDMARBWPtr, postWptrDwords << 2);
  (void) readReg32(kWiiGX2RegDMARBWPtr);

  // Poll the RPTR writeback — it is in bytes, little-endian because of the
  // writeback-swap bit we set in RB_CNTL.
  for (timeoutMs = 0; timeoutMs < 100; timeoutMs++) {
    flushHdp();
    OSSynchronizeIO();
    rptrDwords = (OSSwapLittleToHostInt32(_dmaRPtrPtr[0]) >> 2) &
                 kWiiGX2DMARingBufferDWordMask;
    if (rptrDwords == postWptrDwords) {
      return true;
    }
    IODelay(1000);
  }

  WIISYSLOG("DMA constant-fill timeout: wptr=%u rptr=%u status=0x%08X",
    postWptrDwords, rptrDwords, readReg32(kWiiGX2RegDMAStatusReg));
  return false;
}

// ========================================================================
// MEM1 (EDRAM) bump heap.
//
// The Wii U exposes 32 MiB of on-die EDRAM as MEM1 at physical address 0.
// For graphics it is the ideal home for bandwidth-critical render targets
// (typically depth + small colour buffers); colour/depth bases feed through
// CB_COLOR*_BASE / DB_DEPTH_BASE which accept any MC-reachable physical
// address shifted right by 8.
//
// We implement a simple bump allocator on top of an `IODeviceMemory` range
// created for the MEM1 aperture. That keeps GX2 independent of the device
// tree; the CPU mapping is only for small setup writes (pattern fill,
// header poke) — real traffic flows CPU→MC via DMA/CP.
// ========================================================================

bool WiiGX2::setupMEM1Heap(void) {
  if (_mem1Ready) {
    return true;
  }

  if (_mem1Lock == NULL) {
    _mem1Lock = IOLockAlloc();
    if (_mem1Lock == NULL) {
      WIISYSLOG("Failed to allocate MEM1 heap lock");
      return false;
    }
  }

  // Claim the 32 MiB EDRAM aperture. IODeviceMemory::withRange() just
  // reserves a physical range descriptor; it does not paper over whatever
  // owner currently claims it at the platform-expert level, so this call
  // can never be the sole barrier — it only allows us to build an
  // IOMemoryMap from it if the kernel allows. Failure here is non-fatal.
  _mem1Memory = IODeviceMemory::withRange(kWiiGX2MEM1PhysBase,
                                          kWiiGX2MEM1Size);
  if (_mem1Memory == NULL) {
    WIISYSLOG("IODeviceMemory::withRange failed for MEM1");
    return false;
  }

  _mem1Map = _mem1Memory->map();
  if (_mem1Map == NULL) {
    WIISYSLOG("Failed to map MEM1 aperture");
    OSSafeReleaseNULL(_mem1Memory);
    return false;
  }

  _mem1Virt     = (volatile UInt8 *) _mem1Map->getVirtualAddress();
  _mem1PhysBase = kWiiGX2MEM1PhysBase;
  _mem1Size     = kWiiGX2MEM1Size;
  _mem1Offset   = 0;
  _mem1Ready    = true;

  WIIDBGLOG("MEM1 heap up: phys=0x%08X size=%u virt=%p",
    (UInt32) _mem1PhysBase, _mem1Size, _mem1Virt);
  return true;
}

void WiiGX2::teardownMEM1Heap(void) {
  _mem1Ready = false;
  _mem1Virt  = NULL;
  _mem1Offset = 0;
  _mem1Size   = 0;

  OSSafeReleaseNULL(_mem1Map);
  OSSafeReleaseNULL(_mem1Memory);

  if (_mem1Lock != NULL) {
    IOLockFree(_mem1Lock);
    _mem1Lock = NULL;
  }
}

bool WiiGX2::allocMEM1(UInt32 size, UInt32 align,
                       IOPhysicalAddress *outPhys, volatile void **outVirt) {
  UInt32 base;
  UInt32 end;

  if (!_mem1Ready || size == 0) {
    return false;
  }
  if (align == 0) {
    align = 256; // R6xx surface bases are typically 256-byte aligned.
  }

  IOLockLock(_mem1Lock);
  base = (_mem1Offset + (align - 1)) & ~(align - 1);
  end  = base + size;
  if (end < base || end > _mem1Size) {
    IOLockUnlock(_mem1Lock);
    WIISYSLOG("MEM1 alloc of %u@%u would overflow (offset=%u size=%u)",
      size, align, _mem1Offset, _mem1Size);
    return false;
  }
  _mem1Offset = end;
  IOLockUnlock(_mem1Lock);

  if (outPhys != NULL) {
    *outPhys = _mem1PhysBase + base;
  }
  if (outVirt != NULL) {
    *outVirt = (volatile void *) (_mem1Virt + base);
  }
  WIIDBGLOG("MEM1 alloc %u@%u → phys=0x%08X (offset=%u, %u left)",
    size, align, (UInt32)(_mem1PhysBase + base), _mem1Offset,
    _mem1Size - _mem1Offset);
  return true;
}

void WiiGX2::resetMEM1Heap(void) {
  if (!_mem1Ready) {
    return;
  }
  IOLockLock(_mem1Lock);
  _mem1Offset = 0;
  IOLockUnlock(_mem1Lock);
  WIIDBGLOG("MEM1 heap reset");
}

// ========================================================================
// GPU command processor lifecycle.
// ========================================================================

bool WiiGX2::startCP(void) {
  if (_cpRunning) {
    WIIDBGLOG("CP already running");
    return true;
  }

  // 1. Allocate ring buffer.
  if (!allocateRingBuffer()) {
    return false;
  }

  // 2. Configure and resume the CP with our ring.
  if (!cpResume(_ringPhys, kWiiGX2CPRingBufSizeLog2QW)) {
    WIISYSLOG("cpResume failed");
    freeRingBuffer();
    return false;
  }

  _cpRunning = true;

  // 3. Submit ME_INITIALIZE packet.
  if (!submitMEInitialize()) {
    WIISYSLOG("ME_INITIALIZE failed");
    stopCP();
    return false;
  }

  // 4. Verify the CP is processing packets via a fence test.
  if (!testCPFence()) {
    WIISYSLOG("CP fence test failed - CP may not be processing packets");
    stopCP();
    return false;
  }

  if (testCPWritebackBuffer()) {
    WIIDBGLOG("CP shared-memory writeback path verified");
  } else {
    WIISYSLOG("CP shared-memory writeback test failed - continuing with scratch-only sync");
  }

  WIISYSLOG("CP started and verified via scratch-register fence");
  return true;
}

void WiiGX2::stopCP(void) {
  teardownInterruptRing();

  if (_cpRunning) {
    cpHalt();
    _cpRunning = false;
    WIIDBGLOG("CP stopped");
  }
  freeRingBuffer();
}

// ========================================================================
// Command submission and sync.
// ========================================================================

bool WiiGX2::submitMEInitialize(void) {
  if (!_cpRunning || _ringPtr == NULL) {
    return false;
  }

  //
  // ME_INITIALIZE: PACKET3(0x44, 5) — 6 body DWORDs.
  //
  // This boots the CP micro-engine.  Format follows the Linux radeon
  // driver r600_cp_start() / r600_cp_resume().
  //
  ringWrite(WiiGX2PM4Packet3(kWiiGX2PM4OpMEInitialize, 5));
  ringWrite(0x00000001);                                      // DW1: device select
  ringWrite(0x00000000);                                      // DW2: reserved
  ringWrite(kWiiGX2R700MaxHWContexts - 1);                    // DW3: max_hw_contexts - 1
  ringWrite(1u << kWiiGX2PM4MEInitDeviceIDShift);             // DW4: device_id
  ringWrite(0x00000000);                                      // DW5: reserved
  ringWrite(0x00000000);                                      // DW6: reserved

  ringCommit();

  // Wait for the CP to process the packet.
  if (!waitForGRBMIdle(50000)) {
    WIISYSLOG("CP did not go idle after ME_INITIALIZE (GRBM 0x%08X)",
      readReg32(kWiiGX2RegGRBMStatus));
    return false;
  }

  WIISYSLOG("ME_INITIALIZE submitted (max_hw_contexts=%u)", kWiiGX2R700MaxHWContexts);
  return true;
}

bool WiiGX2::writeScratchViaCP(UInt32 scratchIndex, UInt32 value) {
  UInt32 scratchReg;

  if (!_cpRunning || _ringPtr == NULL || scratchIndex > 7) {
    return false;
  }

  scratchReg = kWiiGX2RegScratchReg0 + (scratchIndex * 4);

  //
  // SET_CONFIG_REG: write a single config-space register via the CP.
  //
  // Packet body: (register_address - CONFIG_REG_BASE) >> 2, then data.
  //
  ringWrite(WiiGX2PM4Packet3(kWiiGX2PM4OpSetConfigReg, 1));
  ringWrite((scratchReg - kWiiGX2PM4SetConfigRegBase) >> 2);
  ringWrite(value);

  ringCommit();
  return true;
}

bool WiiGX2::writeMemoryFenceViaCP(IOPhysicalAddress gpuAddress, UInt32 value) {
  UInt32 cpCoherCntl;

  if (!_cpRunning || _ringPtr == NULL || gpuAddress == 0) {
    return false;
  }

  cpCoherCntl = kWiiGX2PM4SurfaceSyncTCActionEna |
                kWiiGX2PM4SurfaceSyncVCActionEna |
                kWiiGX2PM4SurfaceSyncSHActionEna |
                kWiiGX2PM4SurfaceSyncFullCacheEna;

  ringWrite(WiiGX2PM4Packet3(kWiiGX2PM4OpSurfaceSync, 3));
  ringWrite(cpCoherCntl);
  ringWrite(0xFFFFFFFF);
  ringWrite(0x00000000);
  ringWrite(10);

  ringWrite(WiiGX2PM4Packet3(kWiiGX2PM4OpEventWriteEOP, 4));
  ringWrite(WiiGX2PM4EventType(kWiiGX2PM4EventCacheFlushAndInvTS) |
            WiiGX2PM4EventIndex(kWiiGX2PM4EventIndexTS));
  ringWrite((UInt32)((UInt64)gpuAddress & 0xFFFFFFFCULL));
  ringWrite((UInt32)(((UInt64)gpuAddress >> 32) & 0xFFULL) |
            WiiGX2PM4EventWriteEOPDataSel(kWiiGX2PM4EventWriteEOPDataSelLow32) |
            WiiGX2PM4EventWriteEOPIntSel(kWiiGX2PM4EventWriteEOPIntSelNone));
  ringWrite(value);
  ringWrite(0x00000000);

  ringCommit();
  return true;
}

bool WiiGX2::testCPFence(void) {
  const UInt32 kTestValue   = 0xDEADBEEF;
  const UInt32 kTimeoutUS   = 100000;   // 100 ms
  const UInt32 kPollInterval = 10;
  UInt32 elapsed = 0;

  // Clear SCRATCH_REG0 via MMIO.
  writeReg32(kWiiGX2RegScratchReg0, 0);
  (void)readReg32(kWiiGX2RegScratchReg0);

  // Enable scratch register writeback.
  writeReg32(kWiiGX2RegScratchUMsk, 0xFF);

  // Write the test value via CP → SET_CONFIG_REG → SCRATCH_REG0.
  if (!writeScratchViaCP(0, kTestValue)) {
    return false;
  }

  // Poll SCRATCH_REG0 via MMIO for the expected value.
  while (elapsed < kTimeoutUS) {
    UInt32 val = readReg32(kWiiGX2RegScratchReg0);
    if (val == kTestValue) {
      WIISYSLOG("CP fence test passed: SCRATCH_REG0=0x%08X after %u us",
        val, elapsed);
      return true;
    }
    IODelay(kPollInterval);
    elapsed += kPollInterval;
  }

  WIISYSLOG("CP fence test FAILED: SCRATCH_REG0=0x%08X (expected 0x%08X) after %u us",
    readReg32(kWiiGX2RegScratchReg0), kTestValue, kTimeoutUS);
  return false;
}

bool WiiGX2::testCPWritebackBuffer(void) {
  const UInt32 kTestValue = 0x4D454D32;
  const UInt32 kTimeoutUS = 100000;
  const UInt32 kPollInterval = 10;
  IOBufferMemoryDescriptor *writebackDesc = NULL;
  volatile UInt32 *writebackPtr = NULL;
  IOPhysicalAddress writebackPhys = 0;
  UInt32 elapsed = 0;
  UInt32 observed = 0;
  bool success = false;

  if (!allocateGPUBuffer(&writebackDesc, &writebackPtr, &writebackPhys,
      PAGE_SIZE, "CP writeback")) {
    return false;
  }

  writebackPtr[0] = 0;
  OSSynchronizeIO();
  flushHdp();

  if (!writeMemoryFenceViaCP(writebackPhys, kTestValue)) {
    WIISYSLOG("Failed to emit CP shared-memory writeback fence");
    goto finish;
  }

  while (elapsed < kTimeoutUS) {
    UInt32 rawValue;

    OSSynchronizeIO();
    rawValue = writebackPtr[0];
    observed = OSSwapLittleToHostInt32(rawValue);
    if (observed == kTestValue) {
      WIIDBGLOG("CP writeback test passed: phys=0x%08X value=0x%08X after %u us",
        (UInt32)writebackPhys, observed, elapsed);
      success = true;
      break;
    }

    IODelay(kPollInterval);
    elapsed += kPollInterval;
  }

  if (!success) {
    WIISYSLOG("CP writeback test FAILED: phys=0x%08X value=0x%08X (expected 0x%08X) after %u us",
      (UInt32)writebackPhys, observed, kTestValue, kTimeoutUS);
  }

finish:
  freeGPUBuffer(&writebackDesc, &writebackPtr, &writebackPhys);
  return success;
}

// ========================================================================
// GPU default state initialization.
//
// Follows rv770_gpu_init() from the Linux radeon driver, adapted for
// RV710-class GPU7 / Latte:
//   max_pipes=2, max_simds=2, max_backends=1
//   max_gprs=256, max_threads=192, max_stack_entries=256
//   max_hw_contexts=4, sq_num_cf_insts=1
// ========================================================================

bool WiiGX2::initGPUDefaultState(void) {
  UInt32 sq_config;
  UInt32 shaderPipeConfig;
  UInt32 inactivePipes;
  UInt32 activePipes;
  UInt32 numQDPipes;
  UInt32 gb_tiling;

  // === GRBM read timeout ===
  writeReg32(kWiiGX2RegGRBMCntl, 0xFF);

  // === Tiling: preserve boot firmware config ===
  gb_tiling = readReg32(kWiiGX2RegGBTilingConfig);
  writeReg32(kWiiGX2RegDCPTilingConfig, gb_tiling & 0xFFFF);
  WIIDBGLOG("GB_TILING_CONFIG: 0x%08X (preserved from boot firmware)", gb_tiling);

  // === SPI: detect active pipe count ===
  shaderPipeConfig = readReg32(kWiiGX2RegCCGCShaderPipeConfig);
  inactivePipes = (shaderPipeConfig >> 8) & 0xFF;
  activePipes = 0;
  for (UInt32 i = 0, mask = 1; i < 8; i++, mask <<= 1) {
    if (!(inactivePipes & mask)) {
      activePipes++;
    }
  }
  if (activePipes == 0) {
    activePipes = 2;   // Fallback for RV710-class
  }
  WIIDBGLOG("Shader pipe config: 0x%08X, active pipes: %u", shaderPipeConfig, activePipes);

  // Single active pipe: disable interpolation on pipe 1.
  writeReg32(kWiiGX2RegSPIConfigCntl,
    (activePipes == 1) ? kWiiGX2SPIConfigCntlDisableInterp1 : 0);

  // === VGT dealloc / vertex reuse ===
  numQDPipes = activePipes;
  writeReg32(kWiiGX2RegVGTOutDeallocCntl, (numQDPipes * 4) & 0x7F);
  writeReg32(kWiiGX2RegVGTVertexReuseBlockCntl, ((numQDPipes * 4) - 2) & 0x1FF);

  // === CP queue thresholds ===
  writeReg32(kWiiGX2RegCPQueueThresholds,
    WiiGX2CPQueueThresholdsROQIB1Start(0x16) |
    WiiGX2CPQueueThresholdsROQIB2Start(0x2B));
  writeReg32(kWiiGX2RegCPMEQThresholds, WiiGX2CPMEQThresholdsSTQSplit(0x30));

  // === TA: disable cube wrap aniso ===
  writeReg32(kWiiGX2RegTACntlAux,
    readReg32(kWiiGX2RegTACntlAux) | kWiiGX2TACntlAuxDisableCubeAniso);

  // === VGT defaults ===
  writeReg32(kWiiGX2RegVGTNumInstances, 1);
  writeReg32(kWiiGX2RegPASCFifoSize,
    WiiGX2PASCFifoSizePrimFifoSize(0x40) |
    WiiGX2PASCFifoSizeHizTileFifoSize(0x30) |
    WiiGX2PASCFifoSizeEarlyZTileFifoSize(0x130));

  // === SPI_CONFIG_CNTL_1: VTX_DONE_DELAY(4) ===
  writeReg32(kWiiGX2RegSPIConfigCntl1, WiiGX2SPIConfigCntl1VtxDoneDelay(4));

  // === SQ_MS_FIFO_SIZES (RV710 values) ===
  // CACHE_FIFO_SIZE=16, FETCH_FIFO_HIWATER=1, DONE_FIFO_HIWATER=0xe0,
  // ALU_UPDATE_FIFO_HIWATER=0x8.
  writeReg32(kWiiGX2RegSQMSFifoSizes,
    WiiGX2SQMSFifoSizesCacheFifoSize(16) |
    WiiGX2SQMSFifoSizesFetchFifoHiwater(1) |
    WiiGX2SQMSFifoSizesDoneFifoHiwater(0xE0) |
    WiiGX2SQMSFifoSizesALUUpdateFifoHiwater(0x08));

  // === SQ_CONFIG (RV710: no vertex cache) ===
  sq_config = readReg32(kWiiGX2RegSQConfig);
  sq_config &= ~(BITRange(24, 31));       // Clear priority fields
  sq_config |= kWiiGX2SQConfigDX9Consts | kWiiGX2SQConfigExportSrcC;
  sq_config |= (0u << kWiiGX2SQConfigPSPrioShift) |
               (1u << kWiiGX2SQConfigVSPrioShift) |
               (2u << kWiiGX2SQConfigGSPrioShift) |
               (3u << kWiiGX2SQConfigESPrioShift);
  sq_config &= ~kWiiGX2SQConfigVCEnable;  // RV710: no vertex cache
  writeReg32(kWiiGX2RegSQConfig, sq_config);

  // === SQ GPR resource management (max_gprs=256) ===
  // PS=96, VS=96, TEMP=0(wraps), GS=28, ES=28.  Total=248.
  writeReg32(kWiiGX2RegSQGPRResourceMgmt1,
    (96u << kWiiGX2SQNumPSGPRsShift) |
    (96u << kWiiGX2SQNumVSGPRsShift) |
    (0u  << kWiiGX2SQNumClauseTempGPRsShift));
  writeReg32(kWiiGX2RegSQGPRResourceMgmt2,
    (28u << kWiiGX2SQNumGSGPRsShift) |
    (28u << kWiiGX2SQNumESGPRsShift));

  // === SQ thread resource management (max_threads=192) ===
  // PS=96, VS=48, GS=16, ES=24.  Total=184.
  writeReg32(kWiiGX2RegSQThreadResourceMgmt,
    (96u << kWiiGX2SQNumPSThreadsShift) |
    (48u << kWiiGX2SQNumVSThreadsShift) |
    (16u << kWiiGX2SQNumGSThreadsShift) |
    (24u << kWiiGX2SQNumESThreadsShift));

  // === SQ stack resource management (max_stack_entries=256) ===
  // PS=64, VS=64, GS=64, ES=64.  Total=256.
  writeReg32(kWiiGX2RegSQStackResourceMgmt1,
    (64u << kWiiGX2SQNumPSStackEntriesShift) |
    (64u << kWiiGX2SQNumVSStackEntriesShift));
  writeReg32(kWiiGX2RegSQStackResourceMgmt2,
    (64u << kWiiGX2SQNumGSStackEntriesShift) |
    (64u << kWiiGX2SQNumESStackEntriesShift));

  writeReg32(kWiiGX2RegPASCForceEOVMaxCnts,
    WiiGX2PASCForceEOVMaxClkCnt(4095) |
    WiiGX2PASCForceEOVMaxRezCnt(255));

  // === VGT cache invalidation (RV710: TC only, no VC) ===
  // CACHE_INVALIDATION=TC_ONLY(1), AUTO_INVLD_EN=ES_AND_GS_AUTO(3<<6).
  writeReg32(kWiiGX2RegVGTCacheInvalidation,
    WiiGX2VGTCacheInvalidationMode(kWiiGX2VGTCacheInvalidationTCOnly) |
    WiiGX2VGTCacheInvalidationAutoInvldEn(kWiiGX2VGTCacheInvalidationESAndGSAuto));

  // === VGT GS configuration ===
  writeReg32(kWiiGX2RegVGTESPerGS, 128);
  writeReg32(kWiiGX2RegVGTGSPerES, 160);
  writeReg32(kWiiGX2RegVGTGSPerVS, 2);
  writeReg32(kWiiGX2RegVGTGSVertexReuse, 16);

  // === Default render state ===
  writeReg32(kWiiGX2RegPASCLineStippleState, 0);
  writeReg32(kWiiGX2RegVGTStrmoutEn, 0);
  writeReg32(kWiiGX2RegSXMisc, 0);
  writeReg32(kWiiGX2RegPASCModeCntl, 0);
  writeReg32(kWiiGX2RegPASCEdgeRule, 0xAAAAAAAA);
  writeReg32(kWiiGX2RegPASCAAConfig, 0);
  writeReg32(kWiiGX2RegPASCCliprectRule, 0xFFFF);
  writeReg32(kWiiGX2RegPASCLineStipple, 0);
  writeReg32(kWiiGX2RegSPIInputZ, 0);
  writeReg32(kWiiGX2RegSPIPSInControl0, 2u << kWiiGX2SPIPSInCtl0NumInterpShift);
  writeReg32(kWiiGX2RegCBColor7Frag, 0);
  writeReg32(kWiiGX2RegPASCMultiChipCntl, 0);

  // Clear all 8 colour buffer base addresses.
  for (UInt32 i = 0; i < 8; i++) {
    writeReg32(kWiiGX2RegCBColor0Base + (i * 4), 0);
  }

  // === PA_CL_ENHANCE: vertex reorder + 3 clip sequences ===
  writeReg32(kWiiGX2RegPACLEnhance, BIT0 | (3u << 1));

  // === VC_ENHANCE ===
  writeReg32(kWiiGX2RegVCEnhance, 0);

  WIISYSLOG("GPU default state initialized (RV710 profile, %u active pipes)", activePipes);
  WIIDBGLOG("  SQ_CONFIG=0x%08X GPR1=0x%08X THREAD=0x%08X STACK1=0x%08X",
    readReg32(kWiiGX2RegSQConfig),
    readReg32(kWiiGX2RegSQGPRResourceMgmt1),
    readReg32(kWiiGX2RegSQThreadResourceMgmt),
    readReg32(kWiiGX2RegSQStackResourceMgmt1));

  return true;
}

// ========================================================================
// Phase 3: Minimal draw path — colour-clear via the CP.
//
// We hand-build a tiny pipeline:
//   * VS   = pre-compiled pass-through from r600_blit_shaders.c
//   * PS   = pre-compiled interpolator-export from r600_blit_shaders.c
//   * VB   = three vertices in clip space covering the viewport with the
//           user-supplied clear colour in the second attribute (R,G,B,A).
//   * CB0  = caller-provided physical address, LINEAR_GENERAL, 8_8_8_8.
//
// The Linux radeon driver uses the same VS/PS pair for `r600_blit_rect`.
// GLSL-level clears map onto it by setting the VB colour attribute to the
// clear colour — no compiler involvement needed.
// ========================================================================

// SQ vertex-buffer resource words — the VB descriptor is a single 7-DWORD
// block in SQ_VTX_CONSTANT space (slot 160 reserved by r600_blit for the
// draw-path VB). `type=VERTEX_BUFFER` lives in word6 at bits 30:31.
static const UInt32 kClearVBSlot = 0;

// Full-viewport-covering single-triangle vertex buffer. Each vertex is
// (x, y, z, w, r, g, b, a) = 8 floats (32 bytes). The Radeon blit path
// feeds window-space XY and uses PA_CL_VTE_CNTL=VTX_XY_FMT, so we mirror
// that here instead of relying on a viewport transform.
struct ClearVertex {
  float position[4];
  float colour[4];
};

// Returns the stride (bytes) of one clear vertex.
#define kWiiGX2ClearVBVertexStride (sizeof(ClearVertex))
#define kWiiGX2ClearVBVertexCount  3
#define kWiiGX2ClearVBBytes        (kWiiGX2ClearVBVertexStride * kWiiGX2ClearVBVertexCount)

// ---------------------------------------------------------------------
// PM4 helpers.
// ---------------------------------------------------------------------
void WiiGX2::pm4SetContextReg(UInt32 reg, UInt32 value) {
  ringWrite(WiiGX2PM4Packet3(kWiiGX2PM4OpSetContextReg, 1));
  ringWrite((reg - kWiiGX2PM4SetContextRegBase) >> 2);
  ringWrite(value);
}

void WiiGX2::pm4SetContextRegs(UInt32 reg, const UInt32 *values, UInt32 count) {
  if (count == 0 || values == NULL) {
    return;
  }
  ringWrite(WiiGX2PM4Packet3(kWiiGX2PM4OpSetContextReg, count));
  ringWrite((reg - kWiiGX2PM4SetContextRegBase) >> 2);
  for (UInt32 i = 0; i < count; i++) {
    ringWrite(values[i]);
  }
}

void WiiGX2::pm4SetConfigReg(UInt32 reg, UInt32 value) {
  ringWrite(WiiGX2PM4Packet3(kWiiGX2PM4OpSetConfigReg, 1));
  ringWrite((reg - kWiiGX2PM4SetConfigRegBase) >> 2);
  ringWrite(value);
}

// SURFACE_SYNC is a 5-DWORD Type-3 packet: header, CP_COHER_CNTL, size,
// base, poll-interval. `baseShifted` is the GPU base address >> 8, `size`
// is measured in "DW / 0x100" chunks per R6xx spec; callers typically pass
// 0xFFFFFFFF to just flush the world.
void WiiGX2::pm4SurfaceSync(UInt32 cpCoherCntl, UInt32 size, UInt32 baseShifted) {
  ringWrite(WiiGX2PM4Packet3(kWiiGX2PM4OpSurfaceSync, 3));
  ringWrite(cpCoherCntl);
  ringWrite(size);
  ringWrite(baseShifted);
  ringWrite(10); // poll interval
}

void WiiGX2::pm4SetVTXResource(UInt32 slot, IOPhysicalAddress base,
                               UInt32 sizeBytes, UInt32 stride) {
  const UInt32 slotOffset = kWiiGX2PM4SetResourceBase + (slot * 0x1C);
  UInt32 word2;

  // 7 DWORDs of resource + 1 DWORD (slot_offset>>2) = 8 DWORDs payload.
  ringWrite(WiiGX2PM4Packet3(kWiiGX2PM4OpSetResource, 7));
  ringWrite((slotOffset - kWiiGX2PM4SetResourceBase) >> 2);
  // word0: base_address
  ringWrite((UInt32) base);
  // word1: size - 1
  ringWrite(sizeBytes - 1);
  // word2: stride + format (float4 + float4 blob, treat as raw bytes for
  // fetch — we rely on the VS bytecode to pick the right per-vertex DWORDs
  // via the mega-fetch counter in the fetch shader itself).
  word2 = (stride << kWiiGX2VTXResourceWord2_StrideShift) |
          (kWiiGX2VTXFormat_32_32_32_32_FLOAT
                << kWiiGX2VTXResourceWord2_DataFormatShift) |
          (kWiiGX2VTXNumFormat_SCALED
                << kWiiGX2VTXResourceWord2_NumFormatAllShift) |
          (kWiiGX2VTXEndian_8IN32
                << kWiiGX2VTXResourceWord2_EndianShift);
  ringWrite(word2);
  // word3: mega-fetch = stride
  ringWrite(stride);
  // word4, word5: unused
  ringWrite(0);
  ringWrite(0);
  // word6: type = VERTEX_BUFFER, lives in [30:31]
  ringWrite(kWiiGX2VTXResourceType_VERTEX_BUFFER << 30);
}

// WAIT_REG_MEM polls a shared-memory DW until it equals the expected seq.
void WiiGX2::pm4WaitEopFence(IOPhysicalAddress fencePhys, UInt32 seq) {
  // WAIT_REG_MEM format (count=5): header, info, addr_lo, addr_hi, ref,
  // mask, poll-interval.
  //   info = (mem=1 << 4) | (function=EQUAL=3 << 0) | (engine=ME=0 << 8).
  ringWrite(WiiGX2PM4Packet3(kWiiGX2PM4OpWaitRegMem, 5));
  ringWrite((1u << 4) | 3u);
  ringWrite(((UInt32) fencePhys) & 0xFFFFFFFC);
  ringWrite((UInt32) (((UInt64) fencePhys >> 32) & 0xFFULL));
  ringWrite(seq);
  ringWrite(0xFFFFFFFF);
  ringWrite(16);
}

// ---------------------------------------------------------------------
// Pipeline setup / teardown.
// ---------------------------------------------------------------------
bool WiiGX2::setupClearPipeline(void) {
  if (_clearReady) {
    return true;
  }

  // 1. Upload VS bytecode (shader program start is 256-byte aligned).
  if (!allocateGPUBuffer(&_clearVSDesc, &_clearVSPtr, &_clearVSPhys,
      PAGE_SIZE, "clear VS")) {
    goto fail;
  }
  for (UInt32 i = 0; i < kR6xxBlitVSSize; i++) {
    _clearVSPtr[i] = kR6xxBlitVS[i];
  }

  // 2. Upload PS bytecode.
  if (!allocateGPUBuffer(&_clearPSDesc, &_clearPSPtr, &_clearPSPhys,
      PAGE_SIZE, "clear PS")) {
    goto fail;
  }
  for (UInt32 i = 0; i < kR6xxBlitPSSize; i++) {
    _clearPSPtr[i] = kR6xxBlitPS[i];
  }

  // 3. Vertex buffer storage (will be re-filled on every clear).
  if (!allocateGPUBuffer(&_clearVBDesc, &_clearVBPtr, &_clearVBPhys,
      PAGE_SIZE, "clear VB")) {
    goto fail;
  }

  // 4. Fence page for EOP completion.
  if (!allocateGPUBuffer(&_clearFenceDesc, &_clearFencePtr, &_clearFencePhys,
      PAGE_SIZE, "clear fence")) {
    goto fail;
  }
  _clearFencePtr[0] = 0;
  _clearFenceSeq    = 0;

  OSSynchronizeIO();
  flushHdp();

  _clearReady = true;
  WIIDBGLOG("Clear pipeline ready: VS=0x%08X PS=0x%08X VB=0x%08X FENCE=0x%08X",
    (UInt32) _clearVSPhys, (UInt32) _clearPSPhys,
    (UInt32) _clearVBPhys, (UInt32) _clearFencePhys);
  return true;

fail:
  teardownClearPipeline();
  return false;
}

void WiiGX2::teardownClearPipeline(void) {
  _clearReady = false;
  freeGPUBuffer(&_clearFenceDesc, &_clearFencePtr, &_clearFencePhys);
  freeGPUBuffer(&_clearVBDesc,    &_clearVBPtr,    &_clearVBPhys);
  freeGPUBuffer(&_clearPSDesc,    &_clearPSPtr,    &_clearPSPhys);
  freeGPUBuffer(&_clearVSDesc,    &_clearVSPtr,    &_clearVSPhys);
  _clearFenceSeq = 0;
}

// ---------------------------------------------------------------------
// submitColorClear — the actual draw submission.
// Mirrors r600_blit.c::set_* sequence from Linux radeon. Every comment
// with `[r600_blit]` references that file directly.
// ---------------------------------------------------------------------
bool WiiGX2::submitColorClear(IOPhysicalAddress cbPhys,
                              UInt32 widthPixels,
                              UInt32 heightPixels,
                              UInt32 formatCompSwap,
                              const float clearRGBA[4]) {
  union { float f; UInt32 u; } conv;
  ClearVertex verts[kWiiGX2ClearVBVertexCount];
  UInt32 pitchTile;
  UInt32 sliceTile;
  UInt32 coherCntl;
  UInt32 drawInit;
  UInt32 seq;

  if (!_cpRunning || !_clearReady || cbPhys == 0 ||
      widthPixels == 0 || heightPixels == 0 || clearRGBA == NULL) {
    return false;
  }
  // LINEAR_GENERAL: pitch must be a multiple of 8 texels (one tile wide).
  if ((widthPixels & 0x7) != 0) {
    WIISYSLOG("Clear width %u not a multiple of 8 (LINEAR_GENERAL)", widthPixels);
    return false;
  }

  // --- Build the full-screen triangle VB in window space. This matches the
  // Radeon blit setup where PA_CL_VTE_CNTL advertises screen-space XY.
  verts[0].position[0] = 0.0f;                     verts[0].position[1] = 0.0f;
  verts[0].position[2] =  0.0f; verts[0].position[3] =  1.0f;
  verts[1].position[0] = (float) widthPixels * 2.0f;
  verts[1].position[1] = 0.0f;
  verts[1].position[2] =  0.0f; verts[1].position[3] =  1.0f;
  verts[2].position[0] = 0.0f;
  verts[2].position[1] = (float) heightPixels * 2.0f;
  verts[2].position[2] =  0.0f; verts[2].position[3] =  1.0f;
  for (UInt32 v = 0; v < kWiiGX2ClearVBVertexCount; v++) {
    verts[v].colour[0] = clearRGBA[0];
    verts[v].colour[1] = clearRGBA[1];
    verts[v].colour[2] = clearRGBA[2];
    verts[v].colour[3] = clearRGBA[3];
  }
  bcopy(verts, (void *) _clearVBPtr, sizeof(verts));
  OSSynchronizeIO();
  flushHdp();

  // --- Flush all relevant GPU caches before the new draw.
  coherCntl = kWiiGX2PM4SurfaceSyncTCActionEna |
              kWiiGX2PM4SurfaceSyncVCActionEna |
              kWiiGX2PM4SurfaceSyncSHActionEna |
              kWiiGX2PM4SurfaceSyncCBActionEna |
              kWiiGX2PM4SurfaceSyncFullCacheEna;
  pm4SurfaceSync(coherCntl, 0xFFFFFFFF, 0);

  // --- VGT + primitive setup. `DRAW_INDEX_AUTO` reads VGT_DMA_* for the
  // count, VGT_PRIMITIVE_TYPE for the topology.
  pm4SetConfigReg(kWiiGX2RegVGTPrimitiveType, kWiiGX2VGTPrimTypeTriList);
  pm4SetConfigReg(kWiiGX2RegVGTIndexType, kWiiGX2VGTIndexType16Bit);
  pm4SetConfigReg(kWiiGX2RegVGTNumInstances, 1);

  // --- PA viewport / scissor (context space).
  pitchTile = (widthPixels / 8) - 1;
  sliceTile = ((widthPixels * heightPixels) / 64) - 1;

  pm4SetContextReg(kWiiGX2RegPASCWindowOffset, 0);
  pm4SetContextReg(kWiiGX2RegPASCScreenScissorTL, 0);
  pm4SetContextReg(kWiiGX2RegPASCScreenScissorBR,
    (heightPixels << 16) | widthPixels);
  pm4SetContextReg(kWiiGX2RegPASCWindowScissorTL, 0);
  pm4SetContextReg(kWiiGX2RegPASCWindowScissorBR,
    (heightPixels << 16) | widthPixels);
  pm4SetContextReg(kWiiGX2RegPASCGenericScissorTL,
    kWiiGX2PASCScissorWindowOffsetDisable);
  pm4SetContextReg(kWiiGX2RegPASCGenericScissorBR,
    (heightPixels << 16) | widthPixels);
  pm4SetContextReg(kWiiGX2RegPASCVPortScissor0TL,
    kWiiGX2PASCScissorWindowOffsetDisable);
  pm4SetContextReg(kWiiGX2RegPASCVPortScissor0BR,
    (heightPixels << 16) | widthPixels);
  pm4SetContextReg(kWiiGX2RegPASCAAMask, 0xFFFFFFFF);
  pm4SetContextReg(kWiiGX2RegPASCAAConfig, 0);
  pm4SetContextReg(kWiiGX2RegPASCLineCntl, 0);
  pm4SetContextReg(kWiiGX2RegPAClClipCntl,
    kWiiGX2PAClClipCntlClipDisable | kWiiGX2PAClClipCntlDXClipSpaceDef);
  pm4SetContextReg(kWiiGX2RegPAClVTECntl, kWiiGX2PAClVTECntlVTXXYFmt);
  pm4SetContextReg(kWiiGX2RegPASUSCModeCntl, 0);
  pm4SetContextReg(kWiiGX2RegPASUPointSize, 0);
  pm4SetContextReg(kWiiGX2RegPASULineCntl, 0);

  // Viewport 0 — XY already arrives in window space; keep Z in [0, 1].
  conv.f = 1.0f; pm4SetContextReg(kWiiGX2RegPAClVPortXScale0,  conv.u);
  conv.f = 0.0f; pm4SetContextReg(kWiiGX2RegPAClVPortXOffset0, conv.u);
  conv.f = 1.0f; pm4SetContextReg(kWiiGX2RegPAClVPortYScale0,  conv.u);
  conv.f = 0.0f; pm4SetContextReg(kWiiGX2RegPAClVPortYOffset0, conv.u);
  conv.f = 0.5f; pm4SetContextReg(kWiiGX2RegPAClVPortZScale0,  conv.u);
  conv.f = 0.5f; pm4SetContextReg(kWiiGX2RegPAClVPortZOffset0, conv.u);
  conv.f = 0.0f; pm4SetContextReg(kWiiGX2RegPASCVPortZMin0,    conv.u);
  conv.f = 1.0f; pm4SetContextReg(kWiiGX2RegPASCVPortZMax0,    conv.u);
  conv.f = 1.0f; pm4SetContextReg(kWiiGX2RegPAClGBVertClipAdj, conv.u);
  conv.f = 1.0f; pm4SetContextReg(kWiiGX2RegPAClGBVertDiscAdj, conv.u);
  conv.f = 1.0f; pm4SetContextReg(kWiiGX2RegPAClGBHorzClipAdj, conv.u);
  conv.f = 1.0f; pm4SetContextReg(kWiiGX2RegPAClGBHorzDiscAdj, conv.u);

  // --- CB / DB state.
  pm4SetContextReg(kWiiGX2RegDBDepthControl, 0);
  pm4SetContextReg(kWiiGX2RegDBShaderControl, 0);
  pm4SetContextReg(kWiiGX2RegDBDepthInfo, 0);
  pm4SetContextReg(kWiiGX2RegDBDepthBase, 0);
  pm4SetContextReg(kWiiGX2RegDBRenderControl, 0);
  pm4SetContextReg(kWiiGX2RegDBRenderOverride, 0);
  pm4SetContextReg(kWiiGX2RegSXAlphaTestControl, 0);

  pm4SetContextReg(kWiiGX2RegCBTargetMask, 0x0000000F);  // RT0 RGBA write
  pm4SetContextReg(kWiiGX2RegCBShaderMask, 0x0000000F);
  pm4SetContextReg(kWiiGX2RegCBBlendControl, 0);
  pm4SetContextReg(kWiiGX2RegCBBlend0Control, 0);
  pm4SetContextReg(kWiiGX2RegCBColorControl,
    WiiGX2CBColorControlRop3(kWiiGX2CBColorControlRop3Copy));

  // CB_COLOR0_BASE is physical >> 8.
  pm4SetContextReg(kWiiGX2RegCBColor0Base,  (UInt32) (cbPhys >> 8));
  pm4SetContextReg(kWiiGX2RegCBColor0Size,
    WiiGX2CBColorSizePitchTile(pitchTile) |
    WiiGX2CBColorSizeSliceTile(sliceTile));
  pm4SetContextReg(kWiiGX2RegCBColor0View, 0);
  pm4SetContextReg(kWiiGX2RegCBColor0Info,
    kWiiGX2CBColorInfoEndian8IN32 |
    (kWiiGX2CBColorFormat8_8_8_8 << kWiiGX2CBColorInfoFormatShift) |
    (kWiiGX2CBColorInfoArrayLinearGeneral
        << kWiiGX2CBColorInfoArrayModeShift) |
    (kWiiGX2CBColorInfoNumberUNORM
        << kWiiGX2CBColorInfoNumberTypeShift) |
    ((formatCompSwap & 0x3) << kWiiGX2CBColorInfoCompSwapShift) |
    kWiiGX2CBColorInfoBlendClamp);
  pm4SetContextReg(kWiiGX2RegCBColor0Tile, 0);
  pm4SetContextReg(kWiiGX2RegCBColor0Frag, 0);
  pm4SetContextReg(kWiiGX2RegCBColor0Mask, 0);

  // --- SPI: VS exports POSITION + 1 param (COLOR), PS reads 1 param.
  pm4SetContextReg(kWiiGX2RegSPIVSOutConfig,
    (0u << 0)        /* VS_EXPORT_COUNT = 0 means 1 export */ |
    (0u << 1)        /* VS_HALF_PACK    = no */              |
    (0u << 8)        /* VS_EXPORTS_FOG  = no */);
  pm4SetContextReg(kWiiGX2RegSPIVSOutID0, 0x00000000);
  pm4SetContextReg(kWiiGX2RegSPIPSInputCntl0,
    WiiGX2SPIPSInputCntl_Semantic(0) | kWiiGX2SPIPSInputCntlDefaultValOne);
  pm4SetContextReg(kWiiGX2RegSPIPSInControl0,
    (1u << kWiiGX2SPIPSInCtl0NumInterpShift) |
    kWiiGX2SPIPSInCtl0PerspGradientEna);
  pm4SetContextReg(kWiiGX2RegSPIPSInControl1, 0);
  pm4SetContextReg(kWiiGX2RegSPIInputZ, 0);
  pm4SetContextReg(kWiiGX2RegSPIThreadGroupingPS, 0);

  // --- SQ shader programs. START addresses are physical >> 8.
  pm4SetContextReg(kWiiGX2RegSQPgmStartVS, (UInt32) (_clearVSPhys >> 8));
  pm4SetContextReg(kWiiGX2RegSQPgmResourcesVS, 2u /* num_gprs */);
  pm4SetContextReg(kWiiGX2RegSQPgmStartPS, (UInt32) (_clearPSPhys >> 8));
  pm4SetContextReg(kWiiGX2RegSQPgmResourcesPS, 1u /* num_gprs */);
  pm4SetContextReg(kWiiGX2RegSQPgmExportsPS, 2u /* 1 colour export */);

  // --- Bind the clear vertex buffer as VTX resource slot `kClearVBSlot`.
  pm4SetVTXResource(kClearVBSlot, _clearVBPhys, kWiiGX2ClearVBBytes,
                    kWiiGX2ClearVBVertexStride);

  // --- VGT_DRAW_INITIATOR + per-draw immediate args. For DRAW_INDEX_AUTO
  // the packet body is: num_indices, draw_initiator. VGT pulls count from
  // the first DW and streams auto-indices.
  drawInit = kWiiGX2VGTSourceSelectAutoIndex | kWiiGX2VGTMajorModeVGT;
  ringWrite(WiiGX2PM4Packet3(kWiiGX2PM4OpDrawIndexAuto, 1));
  ringWrite(kWiiGX2ClearVBVertexCount);
  ringWrite(drawInit);

  // --- Post-draw CB cache flush + EOP fence with our own sequence value.
  pm4SurfaceSync(kWiiGX2PM4SurfaceSyncCBActionEna |
                 kWiiGX2PM4SurfaceSyncFullCacheEna,
                 0xFFFFFFFF, 0);

  seq = ++_clearFenceSeq;
  _clearFencePtr[0] = 0;
  OSSynchronizeIO();
  flushHdp();

  ringWrite(WiiGX2PM4Packet3(kWiiGX2PM4OpEventWriteEOP, 4));
  ringWrite(WiiGX2PM4EventType(kWiiGX2PM4EventCacheFlushAndInvTS) |
            WiiGX2PM4EventIndex(kWiiGX2PM4EventIndexTS));
  ringWrite((UInt32) ((UInt64) _clearFencePhys & 0xFFFFFFFCULL));
  ringWrite((UInt32) (((UInt64) _clearFencePhys >> 32) & 0xFFULL) |
            WiiGX2PM4EventWriteEOPDataSel(kWiiGX2PM4EventWriteEOPDataSelLow32) |
            WiiGX2PM4EventWriteEOPIntSel(kWiiGX2PM4EventWriteEOPIntSelNone));
  ringWrite(seq);
  ringWrite(0);

  ringCommit();

  // --- Wait for the EOP fence in CPU space (≤100 ms).
  {
    const UInt32 kTimeoutUS   = 100000;
    const UInt32 kPollInterval = 10;
    UInt32 elapsed = 0;
    UInt32 observed = 0;

    while (elapsed < kTimeoutUS) {
      OSSynchronizeIO();
      observed = OSSwapLittleToHostInt32(_clearFencePtr[0]);
      if (observed == seq) {
        WIIDBGLOG("submitColorClear: fence=%u reached in %u us (cb=0x%08X %ux%u)",
          seq, elapsed, (UInt32) cbPhys, widthPixels, heightPixels);
        return true;
      }
      IODelay(kPollInterval);
      elapsed += kPollInterval;
    }
    WIISYSLOG("submitColorClear: fence timeout seq=%u observed=0x%08X (cb=0x%08X)",
      seq, observed, (UInt32) cbPhys);
  }
  return false;
}

// ========================================================================
// Phase 4: userspace IB submission path.
//
// A single page of GPU-visible, wired, physically-contiguous memory holds
// the shared fence value. EVENT_WRITE_EOP updates the first DWORD after
// each user IB completes. Userspace maps this page read-only via
// IOUserClient::clientMemoryForType and polls it.
//
// The submission lock serialises PM4 emission from multiple user client
// instances sharing the single CP ring.
// ========================================================================

bool WiiGX2::setupUserFence(void) {
  if (_userFenceReady) {
    return true;
  }

  if (_userSubmitLock == NULL) {
    _userSubmitLock = IOLockAlloc();
    if (_userSubmitLock == NULL) {
      WIISYSLOG("setupUserFence: failed to allocate submit lock");
      return false;
    }
  }

  if (!allocateGPUBuffer(&_userFenceDesc, &_userFencePtr, &_userFencePhys,
                         page_size, "user fence")) {
    return false;
  }

  bzero((void *) _userFencePtr, page_size);
  OSSynchronizeIO();
  flushHdp();

  _userFenceSeq   = 0;
  _userFenceReady = true;
  WIIDBGLOG("setupUserFence: fence page phys=0x%08X", (UInt32) _userFencePhys);
  return true;
}

void WiiGX2::teardownUserFence(void) {
  freeGPUBuffer(&_userFenceDesc, &_userFencePtr, &_userFencePhys);
  _userFenceSeq   = 0;
  _userFenceReady = false;

  if (_userSubmitLock != NULL) {
    IOLockFree(_userSubmitLock);
    _userSubmitLock = NULL;
  }
}

UInt32 WiiGX2::readUserFence(void) const {
  if (!_userFenceReady) {
    return 0;
  }
  OSSynchronizeIO();
  return OSSwapLittleToHostInt32(_userFencePtr[0]);
}

bool WiiGX2::submitIndirectBuffer(IOPhysicalAddress ibPhys,
                                  UInt32 dwordCount,
                                  UInt32 *outFenceSeq) {
  UInt32 seq;
  UInt32 cpCoherCntl;

  if (!_cpRunning) {
    WIISYSLOG("submitIndirectBuffer: CP is not running");
    return false;
  }
  if (!_userFenceReady) {
    WIISYSLOG("submitIndirectBuffer: user fence not set up");
    return false;
  }
  if (ibPhys == 0 || (ibPhys & 0x3) != 0) {
    WIISYSLOG("submitIndirectBuffer: bad IB phys 0x%08X", (UInt32) ibPhys);
    return false;
  }
  if (dwordCount == 0 || dwordCount > 0x000FFFFF) {
    WIISYSLOG("submitIndirectBuffer: invalid dwordCount=%u", dwordCount);
    return false;
  }

  IOLockLock(_userSubmitLock);

  // Allocate a sequence number and pre-advance so zero never collides
  // with the initial cleared fence page.
  _userFenceSeq++;
  if (_userFenceSeq == 0) {
    _userFenceSeq = 1;
  }
  seq = _userFenceSeq;

  // PACKET3 INDIRECT_BUFFER (opcode 0x32), 3 body DWs.
  //   DW1: IB_BASE_LO (byte-aligned) | SWAP=2 (32-bit) on BE host.
  //   DW2: IB_BASE_HI & 0xFF.
  //   DW3: IB size in DWs (lower 20 bits); VMID left at 0.
  ringWrite(WiiGX2PM4Packet3(kWiiGX2PM4OpIndirectBuffer, 2));
  ringWrite(((UInt32) ((UInt64) ibPhys & 0xFFFFFFFCULL)) | (2U << 0));
  ringWrite((UInt32) (((UInt64) ibPhys >> 32) & 0xFFULL));
  ringWrite(dwordCount & 0x000FFFFFU);

  // Post-IB cache flush: invalidate TC/VC/SH, flush CB/DB.
  cpCoherCntl = kWiiGX2PM4SurfaceSyncTCActionEna |
                kWiiGX2PM4SurfaceSyncVCActionEna |
                kWiiGX2PM4SurfaceSyncSHActionEna |
                kWiiGX2PM4SurfaceSyncCBActionEna |
                kWiiGX2PM4SurfaceSyncDBActionEna |
                kWiiGX2PM4SurfaceSyncFullCacheEna;
  ringWrite(WiiGX2PM4Packet3(kWiiGX2PM4OpSurfaceSync, 3));
  ringWrite(cpCoherCntl);
  ringWrite(0xFFFFFFFFU);
  ringWrite(0x00000000U);
  ringWrite(10);

  // EOP fence into the shared user fence page.
  ringWrite(WiiGX2PM4Packet3(kWiiGX2PM4OpEventWriteEOP, 4));
  ringWrite(WiiGX2PM4EventType(kWiiGX2PM4EventCacheFlushAndInvTS) |
            WiiGX2PM4EventIndex(kWiiGX2PM4EventIndexTS));
  ringWrite((UInt32) ((UInt64) _userFencePhys & 0xFFFFFFFCULL));
  ringWrite((UInt32) (((UInt64) _userFencePhys >> 32) & 0xFFULL) |
            WiiGX2PM4EventWriteEOPDataSel(kWiiGX2PM4EventWriteEOPDataSelLow32) |
            WiiGX2PM4EventWriteEOPIntSel(kWiiGX2PM4EventWriteEOPIntSelNone));
  ringWrite(seq);
  ringWrite(0);

  ringCommit();

  IOLockUnlock(_userSubmitLock);

  if (outFenceSeq != NULL) {
    *outFenceSeq = seq;
  }
  return true;
}

bool WiiGX2::waitUserFence(UInt32 seq, UInt32 timeoutUS) {
  const UInt32 kPollInterval = 20;
  UInt32 elapsed = 0;
  UInt32 observed;

  if (!_userFenceReady) {
    return false;
  }
  if (seq == 0) {
    return true;
  }

  while (elapsed < timeoutUS) {
    OSSynchronizeIO();
    observed = OSSwapLittleToHostInt32(_userFencePtr[0]);
    // Use signed wrap-safe comparison so we tolerate the seq counter
    // rolling past 2^31 (fences always monotonically advance).
    if ((SInt32) (observed - seq) >= 0) {
      return true;
    }
    IODelay(kPollInterval);
    elapsed += kPollInterval;
  }
  return false;
}
