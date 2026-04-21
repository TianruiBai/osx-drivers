//
//  GX2Regs.hpp
//  Wii U GX2 GPU MMIO registers
//
// See https://wiiubrew.org/wiki/Hardware/GX2.
//
//  Copyright © 2025 John Davis. All rights reserved.
//

#ifndef GX2Regs_hpp
#define GX2Regs_hpp

#include "WiiCommon.hpp"

//
// Base address for GX2.
//
#define kWiiGX2BaseAddress          0x0C200000
#define kWiiGX2BaseLength           0x80000
#define kWiiGX2RegisterMemoryIndex  0
#define kWiiGX2FramebufferMemoryIndex 1

// Optional provider memory index for the Latte-side bridge window.
#define kWiiGX2BridgeMemoryIndex    2

// Wii U physical memory tiers (see decaf-emu src/libcpu/src/memorymap.cpp).
//   MEM0: 0x08000000 .. 0x082DFFFF (≈3 MiB 1T-SRAM, boot/OS)
//   MEM1: 0x00000000 .. 0x01FFFFFF (32 MiB EDRAM, attached to GPU7 / MC)
//   MEM2: 0x10000000 .. 0x8FFFFFFF (2 GiB DDR3 shared)
#define kWiiGX2MEM1PhysBase         0x00000000
#define kWiiGX2MEM1Size             0x02000000
#define kWiiGX2MEM2PhysBase         0x10000000
#define kWiiGX2MEM2Size             0x80000000U

// Soft MEM2 allocation budget for driver-internal buffers (CP ring, IH ring,
// DMA ring, writeback pages, blit shaders, etc.). Keep this modest — MEM2
// is shared with the system heap.
#define kWiiGX2MEM2BudgetBytes      (4 * 1024 * 1024)

#define kWiiGX2MaxCursorWidth       32
#define kWiiGX2MaxCursorHeight      32
#define kWiiGX2CursorMaxSize        (kWiiGX2MaxCursorWidth * kWiiGX2MaxCursorHeight * 4)
#define kWiiGX2CursorMemSize        (64 * kWiiGX2MaxCursorHeight * 4)

//
// Latte-side bridge registers.
//
// These registers live in the Latte MMIO block, not the direct GX2 window.
// They are used for identifying the SoC revision and accessing the GPU's
// indirect register space.
//
#define kWiiLatteRegHardwareResetControl          0x194
#define kWiiLatteRegHardwareClockGate             0x198
#define kWiiLatteRegClockControl                  0x5EC
#define kWiiLatteRegChipRevId                     0x5A0
#define kWiiLatteChipRevMagicMask                 0xFFFF0000
#define kWiiLatteChipRevMagicCAFE                 0xCAFE0000
#define kWiiLatteChipRevVersionHighShift          8
#define kWiiLatteChipRevVersionHighMask           BITRange(8, 15)
#define kWiiLatteChipRevVersionLowMask            BITRange(0, 7)

// Hardware reset/clock bits used for graphics bring-up observation.
#define kWiiLatteHWResetControlGFX                BIT13
#define kWiiLatteHWResetControlGFXTCPE            BIT12
#define kWiiLatteHWClockGateGFX                   BIT30

// Per-core Latte interrupt bank for PPC cores.
#define kWiiLatteRegPPCInterruptBase              0x440
#define kWiiLatteRegPPCInterruptStride            0x10
#define kWiiLatteRegPPCInterruptCause1            0x04
#define kWiiLatteRegPPCInterruptMask1             0x0C

#define kWiiLatteRegGPUIndirectAddr               0x620
#define kWiiLatteRegGPUIndirectData               0x624

#define kWiiGX2IndirectAddrRegSpaceShift          24
#define kWiiGX2IndirectAddrRegSpaceMask           BITRange(24, 31)
#define kWiiGX2IndirectAddrTileShift              16
#define kWiiGX2IndirectAddrTileMask               BITRange(16, 23)
#define kWiiGX2IndirectAddrOffsetMask             BITRange(0, 15)

#define kWiiGX2IndirectRegSpaceCpl                0x0
#define kWiiGX2IndirectRegSpaceMem1               0x2
#define kWiiGX2IndirectRegSpaceGpu                0x3

#define kWiiGX2IndirectTileCplCt                  0x0
#define kWiiGX2IndirectTileCplTr                  0x1
#define kWiiGX2IndirectTileCplTl                  0x2
#define kWiiGX2IndirectTileCplBr                  0x3
#define kWiiGX2IndirectTileCplBl                  0x4
#define kWiiGX2IndirectTileGpu                    0x0

inline UInt32 wiiGX2EncodeIndirectAddr(UInt32 regSpace, UInt32 tileId, UInt32 regOffset) {
	return ((regSpace << kWiiGX2IndirectAddrRegSpaceShift) & kWiiGX2IndirectAddrRegSpaceMask) |
		((tileId << kWiiGX2IndirectAddrTileShift) & kWiiGX2IndirectAddrTileMask) |
		(regOffset & kWiiGX2IndirectAddrOffsetMask);
}

// GPU7 interrupt source in Latte INT2 status/mask banks.
#define kWiiLatteIRQ2GPU7GC                        BIT11

//
// Direct GPU control/status registers.
//
#define kWiiGX2RegGRBMCntl                         0x2000
#define kWiiGX2RegGRBMStatus2                      0x2002
#define kWiiGX2RegGRBMStatus                       0x2004
#define kWiiGX2RegGRBMStatusSE0                    0x2005
#define kWiiGX2RegGRBMStatusSE1                    0x2006
#define kWiiGX2RegGRBMSoftReset                    0x2008
#define kWiiGX2RegGRBMGfxClkEnableControl          0x200C
#define kWiiGX2RegGRBMWaitIdleClocks               0x200D
#define kWiiGX2RegGRBMStatusSE2                    0x200E
#define kWiiGX2RegGRBMStatusSE3                    0x200F

#define kWiiGX2RegConfigControl                    0x5424
#define kWiiGX2RegConfigMemSize                    0x5428
#define kWiiGX2RegHdpMemCoherencyFlushControl      0x5480
#define kWiiGX2RegBifFramebufferEnable             0x5490
#define kWiiGX2RegHdpRegCoherencyFlushControl      0x54A0

#define kWiiGX2RegSRBMStatus                       0x0E50
#define kWiiGX2RegSRBMStatus2                      0x0EC4

#define kWiiGX2RegCPStalledStatus3                 0x8670
#define kWiiGX2RegCPStalledStatus1                 0x8674
#define kWiiGX2RegCPStalledStatus2                 0x8678
#define kWiiGX2RegCPBusyStatus                     0x867C
#define kWiiGX2RegCPStatus                         0x8680
#define kWiiGX2RegCPMeControl                      0x86D8
#define kWiiGX2RegCPQueueThresholds                0x8760
#define kWiiGX2RegCPMEQThresholds                  0x8764

#define WiiGX2CPQueueThresholdsROQIB1Start(x)      ((x) << 0)
#define WiiGX2CPQueueThresholdsROQIB2Start(x)      ((x) << 8)
#define WiiGX2CPMEQThresholdsSTQSplit(x)           ((x) << 0)

#define kWiiGX2RegCPRingBase                       0xC100
#define kWiiGX2RegCPRingControl                    0xC104
#define kWiiGX2RegCPRingReadPtrWrite               0xC108
#define kWiiGX2RegCPRingReadPtrAddr                0xC10C
#define kWiiGX2RegCPRingReadPtrAddrHi              0xC110
#define kWiiGX2RegCPRingWritePtr                   0xC114
#define kWiiGX2RegCPIntControl                     0xC124
#define kWiiGX2RegCPIntStatus                      0xC128
#define kWiiGX2RegCPPFPUCodeAddr                   0xC150
#define kWiiGX2RegCPPFPUCodeData                   0xC154
#define kWiiGX2RegCPMERamReadAddr                  0xC158
#define kWiiGX2RegCPMERamWriteAddr                 0xC15C
#define kWiiGX2RegCPMERamData                      0xC160

// GRBM status helpers.
#define kWiiGX2RegGRBMCntlReadTimeoutShift         0
#define kWiiGX2RegGRBMCntlReadTimeoutMask          BITRange(0, 7)
#define kWiiGX2RegGRBMStatusCmdFifoAvailMask       BITRange(0, 4)
#define kWiiGX2RegGRBMStatusCPRequestPending       BIT6
#define kWiiGX2RegGRBMStatusCFRequestPending       BIT7
#define kWiiGX2RegGRBMStatusPFRequestPending       BIT8
#define kWiiGX2RegGRBMStatusCPCoherencyBusy        BIT28
#define kWiiGX2RegGRBMStatusCPBusy                 BIT29
#define kWiiGX2RegGRBMStatusCBBusy                 BIT30
#define kWiiGX2RegGRBMStatusGUIActive              BIT31
#define kWiiGX2RegGRBMStatusSEBusyMask             (BIT23 | BIT24 | BIT25 | BIT26 | BIT27 | BIT28 | BIT29 | BIT30 | BIT31)

#define kWiiGX2RegSRBMStatusGRBMRequestPending     BIT5
#define kWiiGX2RegSRBMStatusVMCBusy                BIT8
#define kWiiGX2RegSRBMStatusMCBBusy                BIT9
#define kWiiGX2RegSRBMStatusRLCBusy                BIT15
#define kWiiGX2RegSRBMStatusIHBusy                 BIT17
#define kWiiGX2RegSRBMStatus2DMABusy               BIT5

#define kWiiGX2RegCPMeControlPFPHalt               BIT26
#define kWiiGX2RegCPMeControlMEHalt                BIT28

#define kWiiGX2RegCPRingControlBufferSizeShift     0
#define kWiiGX2RegCPRingControlBufferSizeMask      BITRange(0, 5)
#define kWiiGX2RegCPRingControlBlockSizeShift      8
#define kWiiGX2RegCPRingControlBlockSizeMask       BITRange(8, 13)
#define kWiiGX2RegCPRingControlBufferSwap32        (2 << 16)
#define kWiiGX2RegCPRingControlNoUpdate            BIT27
#define kWiiGX2RegCPRingControlReadPtrWriteEnable  BIT31

#define kWiiGX2RegCPIntControlTimestampEnable      BIT26
#define kWiiGX2RegCPIntControlIB2Enable            BIT29
#define kWiiGX2RegCPIntControlIB1Enable            BIT30
#define kWiiGX2RegCPIntControlRingBufferEnable     BIT31

#define kWiiGX2RegCPIntStatusTimestamp             BIT26
#define kWiiGX2RegCPIntStatusIB2                   BIT29
#define kWiiGX2RegCPIntStatusIB1                   BIT30
#define kWiiGX2RegCPIntStatusRingBuffer            BIT31

#define kWiiGX2RegBifFramebufferReadEnable         BIT0
#define kWiiGX2RegBifFramebufferWriteEnable        BIT1

// ========================================================================
// GRBM soft-reset bit definitions (R_008020_GRBM_SOFT_RESET).
// ========================================================================
#define kWiiGX2GRBMSoftResetCP                     BIT0
#define kWiiGX2GRBMSoftResetCB                     BIT1
#define kWiiGX2GRBMSoftResetCR                     BIT2
#define kWiiGX2GRBMSoftResetDB                     BIT3
#define kWiiGX2GRBMSoftResetPA                     BIT5
#define kWiiGX2GRBMSoftResetSC                     BIT6
#define kWiiGX2GRBMSoftResetSMX                    BIT7
#define kWiiGX2GRBMSoftResetSPI                    BIT8
#define kWiiGX2GRBMSoftResetSH                     BIT9
#define kWiiGX2GRBMSoftResetSX                     BIT10
#define kWiiGX2GRBMSoftResetTC                     BIT11
#define kWiiGX2GRBMSoftResetTA                     BIT12
#define kWiiGX2GRBMSoftResetVC                     BIT13
#define kWiiGX2GRBMSoftResetVGT                    BIT14

// ========================================================================
// SRBM soft-reset bit definitions (R_000E60_SRBM_SOFT_RESET).
// ========================================================================
#define kWiiGX2RegSRBMSoftReset                    0x0E60
#define kWiiGX2SRBMSoftResetBIF                    BIT1
#define kWiiGX2SRBMSoftResetDC                     BIT5
#define kWiiGX2SRBMSoftResetGRBM                   BIT8
#define kWiiGX2SRBMSoftResetHDP                    BIT9
#define kWiiGX2SRBMSoftResetIH                     BIT10
#define kWiiGX2SRBMSoftResetMC                     BIT11
#define kWiiGX2SRBMSoftResetRLC                    BIT13
#define kWiiGX2SRBMSoftResetSEM                    BIT15
#define kWiiGX2SRBMSoftResetVMC                    BIT17
#define kWiiGX2SRBMSoftResetDMA                    BIT12

// ========================================================================
// Sequencer (SQ) registers — shader resource management.
//
// These control the unified shader pipeline on R6xx/R7xx.
// GPU7 (Latte) is R700-class, so these addresses apply.
// ========================================================================
#define kWiiGX2RegSQConfig                         0x8C00
#define kWiiGX2SQConfigVCEnable                    BIT0
#define kWiiGX2SQConfigExportSrcC                  BIT1
#define kWiiGX2SQConfigDX9Consts                   BIT2
#define kWiiGX2SQConfigALUInstPreferVector         BIT3
#define kWiiGX2SQConfigDX10Clamp                   BIT4

#define kWiiGX2RegSQGPRResourceMgmt1               0x8C04
#define kWiiGX2SQNumPSGPRsShift                    0
#define kWiiGX2SQNumPSGPRsMask                     BITRange(0, 7)
#define kWiiGX2SQNumVSGPRsShift                    16
#define kWiiGX2SQNumVSGPRsMask                     BITRange(16, 23)
#define kWiiGX2SQNumClauseTempGPRsShift            28
#define kWiiGX2SQNumClauseTempGPRsMask             BITRange(28, 31)

#define kWiiGX2RegSQGPRResourceMgmt2               0x8C08
#define kWiiGX2SQNumGSGPRsShift                    0
#define kWiiGX2SQNumGSGPRsMask                     BITRange(0, 7)
#define kWiiGX2SQNumESGPRsShift                    16
#define kWiiGX2SQNumESGPRsMask                     BITRange(16, 23)

#define kWiiGX2RegSQThreadResourceMgmt             0x8C0C
#define kWiiGX2SQNumPSThreadsShift                 0
#define kWiiGX2SQNumPSThreadsMask                  BITRange(0, 7)
#define kWiiGX2SQNumVSThreadsShift                 8
#define kWiiGX2SQNumVSThreadsMask                  BITRange(8, 15)
#define kWiiGX2SQNumGSThreadsShift                 16
#define kWiiGX2SQNumGSThreadsMask                  BITRange(16, 23)
#define kWiiGX2SQNumESThreadsShift                 24
#define kWiiGX2SQNumESThreadsMask                  BITRange(24, 31)

#define kWiiGX2RegSQStackResourceMgmt1             0x8C10
#define kWiiGX2SQNumPSStackEntriesShift            0
#define kWiiGX2SQNumPSStackEntriesMask             BITRange(0, 11)
#define kWiiGX2SQNumVSStackEntriesShift            16
#define kWiiGX2SQNumVSStackEntriesMask             BITRange(16, 27)

#define kWiiGX2RegSQStackResourceMgmt2             0x8C14
#define kWiiGX2SQNumGSStackEntriesShift            0
#define kWiiGX2SQNumGSStackEntriesMask             BITRange(0, 11)
#define kWiiGX2SQNumESStackEntriesShift            16
#define kWiiGX2SQNumESStackEntriesMask             BITRange(16, 27)

// SQ shader program start addresses (GPU-space 256-byte aligned).
#define kWiiGX2RegSQPgmStartPS                     0x28840
#define kWiiGX2RegSQPgmResourcesPS                 0x28850
#define kWiiGX2RegSQPgmExportsPS                   0x28854
#define kWiiGX2RegSQPgmCFOffsetPS                  0x288CC
#define kWiiGX2RegSQPgmStartVS                     0x28858
#define kWiiGX2RegSQPgmResourcesVS                 0x28868
#define kWiiGX2RegSQPgmCFOffsetVS                  0x288D0
#define kWiiGX2RegSQPgmStartGS                     0x2886C
#define kWiiGX2RegSQPgmStartES                     0x28880
#define kWiiGX2RegSQPgmStartFS                     0x28894

// SQ FIFO tuning.
#define kWiiGX2RegSQMSFifoSizes                    0x8CF0

#define WiiGX2SQMSFifoSizesCacheFifoSize(x)        ((x) << 0)
#define WiiGX2SQMSFifoSizesFetchFifoHiwater(x)     ((x) << 8)
#define WiiGX2SQMSFifoSizesDoneFifoHiwater(x)      ((x) << 16)
#define WiiGX2SQMSFifoSizesALUUpdateFifoHiwater(x) ((x) << 24)

// SQ ALU constant caches (PS/VS/GS, 16 each).
#define kWiiGX2RegSQALUConstCachePS0               0x28940
#define kWiiGX2RegSQALUConstCacheVS0               0x28980
#define kWiiGX2RegSQALUConstCacheGS0               0x289C0

// SQ vertex constant words (base of vertex buffer table).
#define kWiiGX2RegSQVTXConstantWord0_0             0x30000
#define kWiiGX2RegSQVTXConstantWord1_0             0x30004
#define kWiiGX2RegSQVTXConstantWord2_0             0x30008

// SQ texture resource words (base of texture state table).
#define kWiiGX2RegSQTexResourceWord0_0             0x38000
#define kWiiGX2RegSQTexResourceWord4_0             0x38010
#define kWiiGX2RegSQTexResourceWord5_0             0x38014

// Shader pipe configuration.
#define kWiiGX2RegCCGCShaderPipeConfig             0x8950
#define kWiiGX2RegGCUserShaderPipeConfig           0x8954
#define kWiiGX2RegCCRBBackendDisable               0x98F4

// R6XX limits.
#define kR6XXMaxSHGPRs                             256
#define kR6XXMaxTempGPRs                           16
#define kR6XXMaxSHThreads                          256
#define kR6XXMaxSHStackEntries                     4096
#define kR6XXMaxBackends                           8
#define kR6XXMaxSimds                              8
#define kR6XXMaxPipes                              8

// ========================================================================
// CP ring buffer sizing defaults.
// ========================================================================
#define kWiiGX2CPRingBufferBytes          (64 * 1024)
#define kWiiGX2CPRingBufferDWords         (kWiiGX2CPRingBufferBytes / 4)
#define kWiiGX2CPRingBufferDWordMask      (kWiiGX2CPRingBufferDWords - 1)
#define kWiiGX2CPRingBufSizeLog2QW        13   // log2(64KB / 8 bytes per QW)

#define kWiiGX2IHRingBufferBytes          (64 * 1024)
#define kWiiGX2IHRingPtrMask              (kWiiGX2IHRingBufferBytes - 1)
#define kWiiGX2IHRingBufSizeLog2DWords    14   // log2(64KB / 4 bytes per DW)
#define kWiiGX2IHWPtrWritebackBytes       4

// R700 max hardware contexts (RV710 uses 4, RV770/RV740 use 8).
#define kWiiGX2R700MaxHWContexts          4

// SQ_CONFIG priority field shifts.
#define kWiiGX2SQConfigPSPrioShift         24
#define kWiiGX2SQConfigVSPrioShift         26
#define kWiiGX2SQConfigGSPrioShift         28
#define kWiiGX2SQConfigESPrioShift         30

// SQ_PGM_RESOURCES bitfields (shared by PS/VS/GS/ES stages).
#define kWiiGX2SQPgmResourcesNumGPRsShift      0
#define kWiiGX2SQPgmResourcesNumGPRsMask        BITRange(0, 7)
#define kWiiGX2SQPgmResourcesStackSizeShift     8
#define kWiiGX2SQPgmResourcesStackSizeMask      BITRange(8, 15)

// SQ_PGM_EXPORTS_PS bitfields.
#define kWiiGX2SQPgmExportsPSExportModeShift    0
#define kWiiGX2SQPgmExportsPSExportModeMask     BITRange(0, 4)

// ========================================================================
// Additional SQ resource / vertex-buffer (fetch) definitions used by the
// hand-coded clear/blit draw path. The SQ vertex-constant table starts at
// 0x30000; each VB descriptor is 7 DWORDs (R6xx) at stride 0x1C.
// ========================================================================
#define kWiiGX2SQVTXConstantStride             0x1C
#define kWiiGX2SQVTXConstantSlotCount          160
#define kWiiGX2SQVTXConstantSlot(i) \
  (kWiiGX2RegSQVTXConstantWord0_0 + ((i) * kWiiGX2SQVTXConstantStride))

// VTX_RESOURCE_WORD0: base_address
// VTX_RESOURCE_WORD1: size (bytes - 1)
// VTX_RESOURCE_WORD2: stride, dst_sel_x/y/z/w, format, num_format_all,
//                     format_comp_all, srf_mode_all, endian_swap
// VTX_RESOURCE_WORD3: mega-fetch count
// VTX_RESOURCE_WORD6: type (2 = VERTEX_BUFFER)
#define kWiiGX2VTXResourceWord2_StrideShift      0
#define kWiiGX2VTXResourceWord2_ClampXShift     11
#define kWiiGX2VTXResourceWord2_DataFormatShift 12
#define kWiiGX2VTXResourceWord2_NumFormatAllShift 18
#define kWiiGX2VTXResourceWord2_FormatCompAllShift 20
#define kWiiGX2VTXResourceWord2_SrfModeAllShift 21
#define kWiiGX2VTXResourceWord2_EndianShift     29

#define kWiiGX2VTXFormat_32_32_32_FLOAT         0x2F
#define kWiiGX2VTXFormat_32_32_32_32_FLOAT      0x30
#define kWiiGX2VTXFormat_32_FLOAT               0x0C
#define kWiiGX2VTXFormat_32_32_FLOAT            0x1E
#define kWiiGX2VTXNumFormat_NORM                0
#define kWiiGX2VTXNumFormat_INT                 1
#define kWiiGX2VTXNumFormat_SCALED              2
#define kWiiGX2VTXEndian_None                   0
#define kWiiGX2VTXEndian_8IN16                  1
#define kWiiGX2VTXEndian_8IN32                  2
#define kWiiGX2VTXResourceType_VERTEX_BUFFER    2

// ========================================================================
// Colour buffer extended state used for draws.
// ========================================================================
#define kWiiGX2RegCBColor0Size_                   kWiiGX2RegCBColor0Size

// CB_COLOR0_SIZE: PITCH_TILE (width/8 - 1, bits 0:9), SLICE_TILE (w*h/64 - 1, bits 10:30).
#define WiiGX2CBColorSizePitchTile(x)            (((x) & 0x3FF) << 0)
#define WiiGX2CBColorSizeSliceTile(x)            (((x) & 0xFFFFF) << 10)

// CB_COLOR0_INFO bits.
#define kWiiGX2CBColorInfoEndianNone             (0u << 0)
#define kWiiGX2CBColorInfoEndian8IN16            (1u << 0)
#define kWiiGX2CBColorInfoEndian8IN32            (2u << 0)
#define kWiiGX2CBColorInfoEndian8IN64            (3u << 0)
#define kWiiGX2CBColorInfoFormatShift            2
#define kWiiGX2CBColorInfoArrayModeShift         8
#define kWiiGX2CBColorInfoArrayLinearGeneral     1
#define kWiiGX2CBColorInfoNumberTypeShift        12
#define kWiiGX2CBColorInfoNumberUNORM            0
#define kWiiGX2CBColorInfoReadSizeShift          15
#define kWiiGX2CBColorInfoCompSwapShift          16
#define kWiiGX2CBColorInfoCompSwapStd            0 // R,G,B,A order
#define kWiiGX2CBColorInfoCompSwapAlt            1 // A,R,G,B order (BGRA host)
#define kWiiGX2CBColorInfoBlendClamp             BIT20
#define kWiiGX2CBColorInfoBlendBypass            BIT21
#define kWiiGX2CBColorInfoSourceFormatFloat      BIT27

// CB_COLOR_CONTROL.
#define kWiiGX2RegCBBlendControl                  0x28804
#define kWiiGX2RegCBBlend0Control                 0x28780
#define WiiGX2CBColorControlRop3(x)              ((x) << 16)
#define kWiiGX2CBColorControlRop3Copy            0xCC
#define kWiiGX2CBColorControlPerMrtBlend         BIT5
#define kWiiGX2CBColorControlTargetBlendEnable   (0u << 8) // none
#define kWiiGX2CBColorControlSpecialOpNormal     (0u << 4)

// ========================================================================
// PA extended state used for the clear draw.
// ========================================================================
#define kWiiGX2RegPAClVTECntl                     0x28818
#define kWiiGX2PAClVTECntlVPortXScaleEna          BIT0
#define kWiiGX2PAClVTECntlVPortXOffsetEna         BIT1
#define kWiiGX2PAClVTECntlVPortYScaleEna          BIT2
#define kWiiGX2PAClVTECntlVPortYOffsetEna         BIT3
#define kWiiGX2PAClVTECntlVPortZScaleEna          BIT4
#define kWiiGX2PAClVTECntlVPortZOffsetEna         BIT5
#define kWiiGX2PAClVTECntlVTXXYFmt                BIT8
#define kWiiGX2PAClVTECntlVTXZFmt                 BIT9
#define kWiiGX2PAClVTECntlVTXW0Fmt                BIT10

#define kWiiGX2RegPAClClipCntl                    0x28810
#define kWiiGX2PAClClipCntlDXClipSpaceDef         BIT19
#define kWiiGX2PAClClipCntlClipDisable            BIT16
#define kWiiGX2PAClClipCntlVTXKillOR              BIT21

#define kWiiGX2RegPASUSCModeCntl                  0x28814
#define kWiiGX2RegPASUPointSize                   0x28A00
#define kWiiGX2RegPASULineCntl                    0x28A08
#define kWiiGX2RegPASCWindowOffset                0x28200
#define kWiiGX2RegPASCWindowScissorBR             0x28208
#define kWiiGX2RegPASCClipRectRule                0x2820C
#define kWiiGX2RegPASCScreenScissorBR             0x28034
#define kWiiGX2RegPASCGenericScissorBR            0x28244
#define kWiiGX2RegPASCVPortScissor0TL             0x28250
#define kWiiGX2RegPASCVPortScissor0BR             0x28254
#define kWiiGX2RegPASCVPortZMin0                  0x282D0
#define kWiiGX2RegPASCVPortZMax0                  0x282D4
#define kWiiGX2RegPAClVPortXScale0                0x282A0
#define kWiiGX2RegPAClVPortXOffset0               0x282A4
#define kWiiGX2RegPAClVPortYScale0                0x282A8
#define kWiiGX2RegPAClVPortYOffset0               0x282AC
#define kWiiGX2RegPAClVPortZScale0                0x282B0
#define kWiiGX2RegPAClVPortZOffset0               0x282B4
#define kWiiGX2RegPAClGBVertClipAdj               0x28C0C
#define kWiiGX2RegPAClGBVertDiscAdj               0x28C10
#define kWiiGX2RegPAClGBHorzClipAdj               0x28C14
#define kWiiGX2RegPAClGBHorzDiscAdj               0x28C18
#define kWiiGX2RegPASCAAMask                      0x28C48
#define kWiiGX2RegPASCLineCntl                    0x28C00
// Scissor "window offset disable" bit (forces absolute coords).
#define kWiiGX2PASCScissorWindowOffsetDisable     BIT31

// ========================================================================
// DB extended state.
// ========================================================================
#define kWiiGX2RegDBShaderControl                 0x286E0
#define kWiiGX2RegDBRenderOverride                0x28B70
#define kWiiGX2RegDBRenderControl                 0x28D0C

// ========================================================================
// SX blend / alpha test.
// ========================================================================
#define kWiiGX2RegSXAlphaTestControl              0x28410
#define kWiiGX2RegSXAlphaRefreshControl           0x28414

// ========================================================================
// SPI extended state.
// ========================================================================
#define kWiiGX2RegSPIVSOutConfig                  0x286C4
#define kWiiGX2RegSPIVSOutID0                     0x286E4
#define kWiiGX2RegSPIPSInputCntl0                 0x28644
#define WiiGX2SPIPSInputCntl_Semantic(x)          ((x) << 0)
#define kWiiGX2SPIPSInputCntlDefaultValOne        (1u << 8)
#define kWiiGX2SPIPSInputCntlFlatShade            BIT10
#define kWiiGX2RegSPIThreadGroupingPS             0x286C8

// ========================================================================
// VGT extra draw-time registers.
// ========================================================================
#define kWiiGX2RegVGTIndexType                    0x895C
#define kWiiGX2VGTIndexType16Bit                  0
#define kWiiGX2VGTIndexType32Bit                  1
#define kWiiGX2RegVGTDrawInitiator                0x287F0
#define kWiiGX2VGTSourceSelectAutoIndex           (2u << 0)
#define kWiiGX2VGTMajorModeVGT                    (0u << 4)
#define kWiiGX2VGTPrimTypePointList               1
#define kWiiGX2VGTPrimTypeLineList                2
#define kWiiGX2VGTPrimTypeTriList                 4
#define kWiiGX2VGTPrimTypeTriStrip                6
#define kWiiGX2VGTPrimTypeRect                    17

// ========================================================================
// CP_COHER_CNTL fields used with SURFACE_SYNC.
// ========================================================================
#define WiiGX2CPCoherCntlSize(dwordCount)         (dwordCount)

// ========================================================================
// PA_CL_VTE_CNTL convenient default for window-space draws (all scale/offset
// on, XY and Z in vertex float format, W = 1/W).
// ========================================================================
#define kWiiGX2PAClVTECntlClearDefault \
  (kWiiGX2PAClVTECntlVPortXScaleEna  | kWiiGX2PAClVTECntlVPortXOffsetEna | \
   kWiiGX2PAClVTECntlVPortYScaleEna  | kWiiGX2PAClVTECntlVPortYOffsetEna | \
   kWiiGX2PAClVTECntlVPortZScaleEna  | kWiiGX2PAClVTECntlVPortZOffsetEna | \
   kWiiGX2PAClVTECntlVTXW0Fmt)

// ========================================================================
// Shader Processor Interpolator (SPI) registers.
// ========================================================================
#define kWiiGX2RegSPIConfigCntl                    0x9100
#define kWiiGX2SPIConfigCntlDisableInterp1         BIT5
#define kWiiGX2RegSPIConfigCntl1                   0x913C
#define WiiGX2SPIConfigCntl1VtxDoneDelay(x)        ((x) << 0)
#define kWiiGX2RegSPIPSInControl0                  0x286CC
#define kWiiGX2RegSPIPSInControl1                  0x286D0
#define kWiiGX2RegSPIInputZ                        0x286D8

#define kWiiGX2SPIPSInCtl0NumInterpShift           0
#define kWiiGX2SPIPSInCtl0NumInterpMask            BITRange(0, 5)
#define kWiiGX2SPIPSInCtl0PositionEna              BIT8
#define kWiiGX2SPIPSInCtl0PositionCentroid         BIT9
#define kWiiGX2SPIPSInCtl0PerspGradientEna         BIT28
#define kWiiGX2SPIPSInCtl0LinearGradientEna        BIT29

// ========================================================================
// Colour Buffer (CB) registers.
// ========================================================================
#define kWiiGX2RegCBColorControl                   0x28808
#define kWiiGX2RegCBColor0Base                     0x28040
#define kWiiGX2RegCBColor0Size                     0x28060
#define kWiiGX2RegCBColor0View                     0x28080
#define kWiiGX2RegCBColor0Info                     0x280A0
#define kWiiGX2RegCBColor0Tile                     0x280C0
#define kWiiGX2RegCBColor0Frag                     0x280E0
#define kWiiGX2RegCBColor7Frag                     0x280FC
#define kWiiGX2RegCBColor0Mask                     0x28100
#define kWiiGX2RegCBTargetMask                     0x28238
#define kWiiGX2RegCBShaderMask                     0x2823C

// CB_COLOR0_INFO format values.
#define kWiiGX2CBColorFormatInvalid                0x00
#define kWiiGX2CBColorFormat8                      0x01
#define kWiiGX2CBColorFormat8_8                    0x07
#define kWiiGX2CBColorFormat5_6_5                  0x08
#define kWiiGX2CBColorFormat8_8_8_8                0x1A
#define kWiiGX2CBColorFormat32_FLOAT               0x0E
#define kWiiGX2CBColorFormat32_32_FLOAT            0x1E
#define kWiiGX2CBColorFormat16_16_16_16_FLOAT      0x20
#define kWiiGX2CBColorFormat32_32_32_32_FLOAT      0x23

// ========================================================================
// Depth Buffer (DB) registers.
// ========================================================================
#define kWiiGX2RegDBDepthSize                      0x28000
#define kWiiGX2RegDBDepthView                      0x28004
#define kWiiGX2RegDBDepthBase                      0x2800C
#define kWiiGX2RegDBDepthInfo                      0x28010
#define kWiiGX2RegDBHTileDataBase                  0x28014
#define kWiiGX2RegDBHTileSurface                   0x28D24
#define kWiiGX2RegDBDepthControl                   0x28800
#define kWiiGX2RegDBWatermarks                     0x9838

// DB_DEPTH_CONTROL bitfields.
#define kWiiGX2DBDepthControlStencilEnable         BIT0
#define kWiiGX2DBDepthControlZEnable               BIT1
#define kWiiGX2DBDepthControlZWriteEnable          BIT2
#define kWiiGX2DBDepthControlZFuncShift            4
#define kWiiGX2DBDepthControlZFuncMask             BITRange(4, 6)

// DB depth format values.
#define kWiiGX2DBDepthFormatInvalid                0x00
#define kWiiGX2DBDepthFormat16                     0x01
#define kWiiGX2DBDepthFormat8_24                   0x03
#define kWiiGX2DBDepthFormat32_FLOAT               0x06
#define kWiiGX2DBDepthFormatX24_8_32_FLOAT         0x07

// ========================================================================
// Primitive Assembly (PA) registers.
// ========================================================================
#define kWiiGX2RegPACLEnhance                      0x8A14
#define kWiiGX2PACLEnhanceClipVtxReorderEna        BIT0
#define kWiiGX2RegPASCAAConfig                     0x28C04
#define kWiiGX2RegPASCCliprectRule                 0x2820C
#define kWiiGX2RegPASCEdgeRule                     0x28230
#define kWiiGX2RegPASCModeCntl                     0x28A4C
#define kWiiGX2RegPASCScreenScissorTL              0x28030
#define kWiiGX2RegPASCGenericScissorTL             0x28240
#define kWiiGX2RegPASCWindowScissorTL              0x28204
#define kWiiGX2RegPASCEnhance                      0x8BF0
#define kWiiGX2RegPASCFifoSize                     0x8BCC
#define kWiiGX2RegPASCForceEOVMaxCnts              0x8B24
#define kWiiGX2RegPASCLineStipple                  0x28A0C
#define kWiiGX2RegPASCLineStippleState             0x8B10
#define kWiiGX2RegPASCMultiChipCntl                0x8B20

#define WiiGX2PASCFifoSizePrimFifoSize(x)          ((x) << 0)
#define WiiGX2PASCFifoSizeHizTileFifoSize(x)       ((x) << 12)
#define WiiGX2PASCFifoSizeEarlyZTileFifoSize(x)    ((x) << 20)

#define WiiGX2PASCForceEOVMaxClkCnt(x)             ((x) << 0)
#define WiiGX2PASCForceEOVMaxRezCnt(x)             ((x) << 16)

// ========================================================================
// Vertex Geometry Tessellator (VGT) registers.
// ========================================================================
#define kWiiGX2RegVGTPrimitiveType                 0x8958
#define kWiiGX2RegVGTNumInstances                  0x8974
#define kWiiGX2RegVGTCacheInvalidation             0x88C4
#define kWiiGX2RegVGTESPerGS                       0x88CC
#define kWiiGX2RegVGTGSPerES                       0x88C8
#define kWiiGX2RegVGTGSPerVS                       0x88E8
#define kWiiGX2RegVGTGSVertexReuse                 0x88D4
#define kWiiGX2RegVGTOutDeallocCntl                0x28C5C
#define kWiiGX2RegVGTVertexReuseBlockCntl          0x28C58
#define kWiiGX2RegVGTStrmoutEn                     0x28AB0
#define kWiiGX2RegVGTStrmoutBufferEn               0x28B20
#define kWiiGX2RegVGTEventInitiator                0x28A90
#define kWiiGX2RegVGTDMABase                       0x287E8
#define kWiiGX2RegVGTDMABaseHi                     0x287E4

// VGT_CACHE_INVALIDATION values.
#define WiiGX2VGTCacheInvalidationMode(x)          ((x) << 0)
#define kWiiGX2VGTCacheInvalidationVCOnly          0
#define kWiiGX2VGTCacheInvalidationTCOnly          1
#define kWiiGX2VGTCacheInvalidationVCAndTC         2
#define WiiGX2VGTCacheInvalidationAutoInvldEn(x)   ((x) << 6)
#define kWiiGX2VGTCacheInvalidationAutoNone        0
#define kWiiGX2VGTCacheInvalidationESAuto          1
#define kWiiGX2VGTCacheInvalidationGSAuto          2
#define kWiiGX2VGTCacheInvalidationESAndGSAuto     3

// ========================================================================
// Shader eXport (SX) registers.
// ========================================================================
#define kWiiGX2RegSXMisc                           0x28350
#define kWiiGX2RegSXMemoryExportBase               0x9010
#define kWiiGX2RegSXDebug1                         0x9054

// ========================================================================
// Texture Addressing and Cache (TA / TC) registers.
// ========================================================================
#define kWiiGX2RegTACntlAux                        0x9508
#define kWiiGX2TACntlAuxDisableCubeWrap            BIT0
#define kWiiGX2TACntlAuxDisableCubeAniso           BIT1
#define kWiiGX2TACntlAuxSyncGradient               BIT24
#define kWiiGX2TACntlAuxSyncWalker                 BIT25
#define kWiiGX2TACntlAuxSyncAligner                BIT26
#define kWiiGX2TACntlAuxBilinearPrecision8Bit      BIT31

#define kWiiGX2RegTCCntl                           0x9608

// ========================================================================
// Tiling configuration registers.
// ========================================================================
#define kWiiGX2RegGBTilingConfig                   0x98F0
#define kWiiGX2RegDCPTilingConfig                  0x6CA0

// ========================================================================
// Scratch registers (used for fences / sync).
// ========================================================================
#define kWiiGX2RegScratchReg0                      0x8500
#define kWiiGX2RegScratchReg1                      0x8504
#define kWiiGX2RegScratchReg2                      0x8508
#define kWiiGX2RegScratchReg3                      0x850C
#define kWiiGX2RegScratchReg4                      0x8510
#define kWiiGX2RegScratchReg5                      0x8514
#define kWiiGX2RegScratchReg6                      0x8518
#define kWiiGX2RegScratchReg7                      0x851C
#define kWiiGX2RegScratchUMsk                      0x8540
#define kWiiGX2RegScratchAddr                      0x8544

// ========================================================================
// Vertex Cache registers.
// ========================================================================
#define kWiiGX2RegVCEnhance                        0x9714

// ========================================================================
// Wait-Until synchronisation register.
// ========================================================================
#define kWiiGX2RegWaitUntil                        0x8040
#define kWiiGX2WaitUntilCPDMAIdle                  BIT8
#define kWiiGX2WaitUntil2DIdle                     BIT14
#define kWiiGX2WaitUntil3DIdle                     BIT15
#define kWiiGX2WaitUntil2DIdleClean                BIT16
#define kWiiGX2WaitUntil3DIdleClean                BIT17

// ========================================================================
// Async DMA engine registers.
// ========================================================================
// R6xx/R7xx async DMA — single instance on GPU7. See Linux r600d.h /
// drivers/gpu/drm/radeon/r600_dma.c for the canonical init sequence.
#define kWiiGX2RegDMARBCntl                        0xD000
#define kWiiGX2DMARBEnable                         BIT0
#define WiiGX2DMARBCntlSize(x)                     ((x) << 1) // log2(ring bytes / 4)
#define kWiiGX2DMARBSwapEnable                     BIT9       // 8IN32, needed on PPC
#define kWiiGX2DMARPtrWritebackEnable              BIT12
#define kWiiGX2DMARPtrWritebackSwapEnable          BIT13      // 8IN32, needed on PPC
#define WiiGX2DMARPtrWritebackTimer(x)             ((x) << 16) // log2, 0 = immediate

#define kWiiGX2RegDMARBBase                        0xD004
#define kWiiGX2RegDMARBRPtr                        0xD008
#define kWiiGX2RegDMARBWPtr                        0xD00C
#define kWiiGX2RegDMARBRPtrAddrHi                  0xD01C
#define kWiiGX2RegDMARBRPtrAddrLo                  0xD020
#define kWiiGX2RegDMAIBCntl                        0xD024
#define kWiiGX2DMAIBEnable                         BIT0
#define kWiiGX2DMAIBSwapEnable                     BIT4       // 8IN32, needed on PPC
#define kWiiGX2RegDMAIBRPtr                        0xD028
#define kWiiGX2RegDMACntl                          0xD02C
#define kWiiGX2DMACntlTrapEnable                   BIT0
#define kWiiGX2DMACntlSemIncompleteIntEnable       BIT1
#define kWiiGX2DMACntlSemWaitIntEnable             BIT2
#define kWiiGX2DMACntlDataSwapEnable               BIT3
#define kWiiGX2DMACntlFenceSwapEnable              BIT4
#define kWiiGX2DMACntlCtxEmptyIntEnable            BIT28
#define kWiiGX2RegDMAStatusReg                     0xD034
#define kWiiGX2DMAStatusIdle                       BIT0
#define kWiiGX2RegDMASemIncompleteTimerCntl        0xD010
#define kWiiGX2RegDMASemWaitFailTimerCntl          0xD014
#define kWiiGX2RegDMAMode                          0xD0BC      // rv770+ only

// Minimum ring size as required by R600 async DMA: 256 bytes = 64 DWORDs.
#define kWiiGX2DMARingBufferBytes                  (64 * 1024)
#define kWiiGX2DMARingBufferDWords                 (kWiiGX2DMARingBufferBytes / 4)
#define kWiiGX2DMARingBufferDWordMask              (kWiiGX2DMARingBufferDWords - 1)
#define kWiiGX2DMARPtrWritebackBytes               4

// R6xx DMA PM4-like packet header:
//   bits [31:28] = packet type (0=NOP, 1=WRITE, 3=COPY, 4=INDIRECT_BUFFER, 5=FENCE, 6=TRAP, 8=SRBM_WRITE, 0xC=CONSTANT_FILL, 0xD=COPY_TILED, 0xE=COPY_PARTIAL, ...)
//   bits [27:16] = count (semantics vary by type)
//   bits [15:0]  = flags / tile info
#define WiiGX2DMAPacket(type, flags, count, extra) \
  (((UInt32)(type) << 28) | (((UInt32)(count) & 0xFFFF) << 16) | \
   (((UInt32)(flags) & 0xFF) << 8) | ((UInt32)(extra) & 0xFF))
#define kWiiGX2DMAPacketNOP                        0x0
#define kWiiGX2DMAPacketWrite                      0x2
#define kWiiGX2DMAPacketCopy                       0x3
#define kWiiGX2DMAPacketIndirect                   0x4
#define kWiiGX2DMAPacketFence                      0x5
#define kWiiGX2DMAPacketTrap                       0x6
#define kWiiGX2DMAPacketSRBMWrite                  0x9
#define kWiiGX2DMAPacketConstFill                  0xD

// ========================================================================
// Interrupt handler (IH) ring registers.
// ========================================================================
#define kWiiGX2RegIHRBCntl                         0x3E00
#define kWiiGX2IHRBEnable                          BIT0
#define WiiGX2IHRBCntlSize(x)                      ((x) << 1)
#define kWiiGX2IHWPtrWritebackEnable               BIT8
#define kWiiGX2IHWPtrOverflowEnable                BIT16
#define kWiiGX2IHWPtrOverflowClear                 BIT31

#define kWiiGX2RegIHRBBase                         0x3E04
#define kWiiGX2RegIHRBRPtr                         0x3E08
#define kWiiGX2RegIHRBWPtr                         0x3E0C
#define kWiiGX2RegIHRBWPtrAddrHi                   0x3E10
#define kWiiGX2RegIHRBWPtrAddrLo                   0x3E14
#define kWiiGX2RegIHCntl                           0x3E18
#define kWiiGX2IHCntlEnableIntr                    BIT0
#define WiiGX2IHCntlMCSwap(x)                      ((x) << 1)
#define kWiiGX2IHCntlMCSwapNone                    0
#define kWiiGX2IHCntlMCSwap16Bit                   1
#define kWiiGX2IHCntlMCSwap32Bit                   2
#define kWiiGX2IHCntlMCSwap64Bit                   3
#define kWiiGX2IHCntlRPtrRearm                     BIT4
#define WiiGX2IHCntlMCWrReqCredit(x)               ((x) << 15)
#define WiiGX2IHCntlMCWrCleanCnt(x)                ((x) << 20)

#define kWiiGX2RegInterruptCntl                    0x5468
#define kWiiGX2InterruptCntlGenIHIntEn             BIT8
#define kWiiGX2RegInterruptCntl2                   0x546C
#define kWiiGX2RegGRBMIntCntl                      0x8060

// IH ring entry is 4 DWORDs on R6xx/R7xx.
#define kWiiGX2IHEntryDWords                       4
#define kWiiGX2IHEntryBytes                        (kWiiGX2IHEntryDWords * 4)

// Interrupt source IDs seen on Latte/R700. Matches linux radeon r600_reg.h
// and decaf-emu's CP_INT_SRC_ID enum (see libgpu/latte/latte_enum_cp.h).
#define kWiiGX2IntSrcD1Vsync                       1
#define kWiiGX2IntSrcD1TrigA                       2
#define kWiiGX2IntSrcD2Vsync                       5
#define kWiiGX2IntSrcD2TrigA                       6
#define kWiiGX2IntSrcD1Vupdate                     8
#define kWiiGX2IntSrcD1Pflip                       9
#define kWiiGX2IntSrcD2Vupdate                     10
#define kWiiGX2IntSrcD2Pflip                       11
#define kWiiGX2IntSrcHpdHotplug                    19
#define kWiiGX2IntSrcHdmi                          21
#define kWiiGX2IntSrcDvocap                        22
#define kWiiGX2IntSrcCPRingBuffer                  176
#define kWiiGX2IntSrcCPIB1                         177
#define kWiiGX2IntSrcCPIB2                         178
#define kWiiGX2IntSrcCPReservedBits                180
#define kWiiGX2IntSrcCPEOP                         181
#define kWiiGX2IntSrcScratch                       182
#define kWiiGX2IntSrcCPBadOpcode                   183
#define kWiiGX2IntSrcCPCtxEmpty                    187
#define kWiiGX2IntSrcCPCtxBusy                     188
#define kWiiGX2IntSrcDMATrap                       224
#define kWiiGX2IntSrcDMASemIncomplete              225
#define kWiiGX2IntSrcDMASemWait                    226
#define kWiiGX2IntSrcThermalLowHigh                230
#define kWiiGX2IntSrcThermalHighLow                231
#define kWiiGX2IntSrcGUIIdle                       233
#define kWiiGX2IntSrcDMACtxEmpty                   243

// ========================================================================
// Memory Controller (MC) and Virtual Memory (VM) registers.
// ========================================================================
#define kWiiGX2RegMCConfig                         0x2000
#define kWiiGX2RegMCVMFBLocation                   0x2180
#define kWiiGX2RegMCVMAGPTop                       0x2184
#define kWiiGX2RegMCVMAGPBot                       0x2188
#define kWiiGX2RegMCVMAGPBase                      0x218C
#define kWiiGX2RegMCVMSysApertureLow               0x2190
#define kWiiGX2RegMCVMSysApertureHigh              0x2194
#define kWiiGX2RegMCVMSysApertureDefault           0x2198

#define kWiiGX2RegVMContext0Cntl                   0x1410
#define kWiiGX2VMContextEnable                     BIT0
#define kWiiGX2RegVMContext0PageTableBase           0x1574
#define kWiiGX2RegVMContext0PageTableStart          0x1594
#define kWiiGX2RegVMContext0PageTableEnd            0x15B4
#define kWiiGX2RegVML2Cntl                         0x1400
#define kWiiGX2VML2CacheEnable                     BIT0
#define kWiiGX2RegVML2Cntl2                        0x1404
#define kWiiGX2VML2InvalidateAllL1TLBs             BIT0
#define kWiiGX2VML2InvalidateL2Cache               BIT1
#define kWiiGX2RegVML2Status                       0x140C

// ========================================================================
// Run Level Controller (RLC) registers.
// ========================================================================
#define kWiiGX2RegRLCCntl                          0x3F00
#define kWiiGX2RLCEnable                           BIT0
#define kWiiGX2RegRLCUCodeAddr                     0x3F2C
#define kWiiGX2RegRLCUCodeData                     0x3F30

// ========================================================================
// PM4 packet construction macros.
//
// These follow the Radeon R6xx/R7xx PM4 packet format:
//   Type 0: direct register write
//   Type 2: padding / NOP
//   Type 3: command packet
//
// Reference: Linux drivers/gpu/drm/radeon/r600d.h
// ========================================================================
#define kWiiGX2PM4PacketType0                      0
#define kWiiGX2PM4PacketType2                      2
#define kWiiGX2PM4PacketType3                      3

#define WiiGX2PM4Packet0(reg, n) \
  ((kWiiGX2PM4PacketType0 << 30) | (((reg) >> 2) & 0xFFFF) | (((n) & 0x3FFF) << 16))

#define WiiGX2PM4Packet2() \
  ((UInt32)0x80000000)

#define WiiGX2PM4Packet3(op, n) \
  ((kWiiGX2PM4PacketType3 << 30) | (((op) & 0xFF) << 8) | (((n) & 0x3FFF) << 16))

// PM4 Type 3 opcodes.
#define kWiiGX2PM4OpNOP                            0x10
#define kWiiGX2PM4OpIndirectBufferEnd              0x17
#define kWiiGX2PM4OpSetPredication                 0x20
#define kWiiGX2PM4OpRegRMW                         0x21
#define kWiiGX2PM4OpCondExec                       0x22
#define kWiiGX2PM4OpStart3DCmdBuf                  0x24
#define kWiiGX2PM4OpDrawIndex2                     0x27
#define kWiiGX2PM4OpContextControl                 0x28
#define kWiiGX2PM4OpIndexType                      0x2A
#define kWiiGX2PM4OpDrawIndex                      0x2B
#define kWiiGX2PM4OpDrawIndexAuto                  0x2D
#define kWiiGX2PM4OpDrawIndexImmd                  0x2E
#define kWiiGX2PM4OpNumInstances                   0x2F
#define kWiiGX2PM4OpStrmoutBufferUpdate            0x34
#define kWiiGX2PM4OpIndirectBufferMP               0x38
#define kWiiGX2PM4OpMemSemaphore                   0x39
#define kWiiGX2PM4OpWaitRegMem                     0x3C
#define kWiiGX2PM4OpMemWrite                       0x3D
#define kWiiGX2PM4OpIndirectBuffer                 0x32
#define kWiiGX2PM4OpCPDMA                          0x41
#define kWiiGX2PM4OpPFPSyncMe                      0x42
#define kWiiGX2PM4OpSurfaceSync                    0x43
#define kWiiGX2PM4OpMEInitialize                   0x44
#define kWiiGX2PM4OpCondWrite                      0x45
#define kWiiGX2PM4OpEventWrite                     0x46
#define kWiiGX2PM4OpEventWriteEOP                  0x47
#define kWiiGX2PM4OpOneRegWrite                    0x57
#define kWiiGX2PM4OpSetConfigReg                   0x68
#define kWiiGX2PM4OpSetContextReg                  0x69
#define kWiiGX2PM4OpSetALUConst                    0x6A
#define kWiiGX2PM4OpSetBoolConst                   0x6B
#define kWiiGX2PM4OpSetLoopConst                   0x6C
#define kWiiGX2PM4OpSetResource                    0x6D
#define kWiiGX2PM4OpSetSampler                     0x6E
#define kWiiGX2PM4OpSetCTLConst                    0x6F
#define kWiiGX2PM4OpStrmoutBaseUpdate              0x72
#define kWiiGX2PM4OpSurfaceBaseUpdate              0x73

// SET_*_REG offset ranges (byte-aligned register space).
#define kWiiGX2PM4SetConfigRegBase                 0x00008000
#define kWiiGX2PM4SetConfigRegEnd                  0x0000AC00
#define kWiiGX2PM4SetContextRegBase                0x00028000
#define kWiiGX2PM4SetContextRegEnd                 0x00029000
#define kWiiGX2PM4SetALUConstBase                  0x00030000
#define kWiiGX2PM4SetALUConstEnd                   0x00032000
#define kWiiGX2PM4SetResourceBase                  0x00038000
#define kWiiGX2PM4SetResourceEnd                   0x0003C000
#define kWiiGX2PM4SetSamplerBase                   0x0003C000
#define kWiiGX2PM4SetSamplerEnd                    0x0003CFF0
#define kWiiGX2PM4SetCTLConstBase                  0x0003CFF0
#define kWiiGX2PM4SetCTLConstEnd                   0x0003E200
#define kWiiGX2PM4SetLoopConstBase                 0x0003E200
#define kWiiGX2PM4SetLoopConstEnd                  0x0003E380
#define kWiiGX2PM4SetBoolConstBase                 0x0003E380
#define kWiiGX2PM4SetBoolConstEnd                  0x00040000

// SURFACE_SYNC bitfields.
#define kWiiGX2PM4SurfaceSyncCB0DestBaseEna        BIT6
#define kWiiGX2PM4SurfaceSyncFullCacheEna          BIT20
#define kWiiGX2PM4SurfaceSyncTCActionEna           BIT23
#define kWiiGX2PM4SurfaceSyncVCActionEna           BIT24
#define kWiiGX2PM4SurfaceSyncCBActionEna           BIT25
#define kWiiGX2PM4SurfaceSyncDBActionEna           BIT26
#define kWiiGX2PM4SurfaceSyncSHActionEna           BIT27
#define kWiiGX2PM4SurfaceSyncSMXActionEna          BIT28

// EVENT_WRITE types.
#define WiiGX2PM4EventType(x)                        ((x) << 0)
#define WiiGX2PM4EventIndex(x)                       ((x) << 8)
#define kWiiGX2PM4EventCacheFlushAndInvTS          0x14
#define kWiiGX2PM4EventCacheFlushAndInv            0x16
#define kWiiGX2PM4EventIndexTS                     5

// EVENT_WRITE_EOP fields.
#define WiiGX2PM4EventWriteEOPIntSel(x)            ((x) << 24)
#define kWiiGX2PM4EventWriteEOPIntSelNone          0
#define kWiiGX2PM4EventWriteEOPIntSelIrqOnly       1
#define kWiiGX2PM4EventWriteEOPIntSelConfirm       2
#define WiiGX2PM4EventWriteEOPDataSel(x)           ((x) << 29)
#define kWiiGX2PM4EventWriteEOPDataSelDiscard      0
#define kWiiGX2PM4EventWriteEOPDataSelLow32        1
#define kWiiGX2PM4EventWriteEOPDataSel64           2
#define kWiiGX2PM4EventWriteEOPDataSelCounter64    3

// ME_INITIALIZE device ID.
#define kWiiGX2PM4MEInitDeviceIDShift              16

//
// Registers.
//

//
// Primary Display Graphics Control registers.
//
// Graphics enable.
#define kWiiGX2RegD1GrphEnable                      0x6100
#define kWiiGX2RegD1GrphEnableBit                   BIT0
// Graphics control.
#define kWiiGX2RegD1GrphControl                     0x6104
#define kWiiGX2RegD1GrphControlDepth8bpp            (0 << 0)
#define kWiiGX2RegD1GrphControlDepth16bpp           (1 << 0)
#define kWiiGX2RegD1GrphControlDepth32bpp           (2 << 0)
#define kWiiGX2RegD1GrphControlDepthMask            BITRange(0, 1)
#define kWiiGX2RegD1GrphControlFormat8bppIndexed    (0 << 8)
#define kWiiGX2RegD1GrphControlFormat16bppARGB555   (0 << 8)
#define kWiiGX2RegD1GrphControlFormat32bppARGB8888  (0 << 8)
#define kWiiGX2RegD1GrphControlFormatMask           BITRange(8, 10)
// LUT selection.
#define kWiiGX2RegD1GrphLutSelect                   0x6108
#define kWiiGX2RegD1GrphLutSelectB                  BIT0
// Swap control.
#define kWiiGX2RegD1GrphSwapControl                 0x610C
#define kWiiGX2RegD1GrphSwapControlEndianSwapNone   (0 << 0)
#define kWiiGX2RegD1GrphSwapControlEndianSwap16Bit  (1 << 0)
#define kWiiGX2RegD1GrphSwapControlEndianSwap32Bit  (2 << 0)
#define kWiiGX2RegD1GrphSwapControlEndianSwap64Bit  (3 << 0)
#define kWiiGX2RegD1GrphSwapControlEndianSwapMask   BITRange(0, 1)
// Primary surface address.
#define kWiiGX2RegD1GrphPriSurfaceAddress           0x6110
// Secondary surface address.
#define kWiiGX2RegD1GrphSecSurfaceAddress           0x6118
// Pitch.
#define kWiiGX2RegD1GrphPitch                       0x6120
// Surface offset X.
#define kWiiGX2RegD1GrphSurfaceOffsetX              0x6124
// Surface offset Y.
#define kWiiGX2RegD1GrphSurfaceOffsetY              0x6128
// X start coordinate.
#define kWiiGX2RegD1GrphStartX                      0x612C
// Y start coordinate.
#define kWiiGX2RegD1GrphStartY                      0x6130
// X end coordinate.
#define kWiiGX2RegD1GrphEndX                        0x6134
// Y end coordinate.
#define kWiiGX2RegD1GrphEndY                        0x6138

//
// Primary display hardware cursor registers.
//
// Cursor control.
#define kWiiGX2RegD1CursorControl                   0x6400
#define kWiiGX2RegD1CursorControlEnable             BIT0
#define kWiiGX2RegD1CursorControlMode2Bit           0
#define kWiiGX2RegD1CursorControlMode32BitAND       BIT8
#define kWiiGX2RegD1CursorControlMode32BitPreAlpha  BIT9
#define kWiiGX2RegD1CursorControlMode32BitUnAlpha   (BIT8 | BIT9)
#define kWiiGX2RegD1CursorControlModeMask           (BIT8 | BIT9)
// Cursor surface address.
#define kWiiGX2RegD1CursorSurfaceAddress            0x6408
// Cursor size.
#define kWiiGX2RegD1CursorSize                      0x6410
#define kWiiGX2RegD1CursorSizeHeightMask            BITRange(0, 5)
#define kWiiGX2RegD1CursorSizeWidthShift            16
#define kWiiGX2RegD1CursorSizeWidthMask             BITRange(16, 21)
// Cursor position.
#define kWiiGX2RegD1CursorPosition                  0x6414
#define kWiiGX2RegD1CursorPositionYMask             BITRange(0, 12)
#define kWiiGX2RegD1CursorPositionXShift            16
#define kWiiGX2RegD1CursorPositionXMask             BITRange(16, 28)
// Cursor hot spot.
#define kWiiGX2RegD1CursorHotSpot                   0x6418
#define kWiiGX2RegD1CursorHotSpotYMask              BITRange(0, 5)
#define kWiiGX2RegD1CursorHotSpotXShift             16
#define kWiiGX2RegD1CursorHotSpotXMask              BITRange(16, 21)
// Cursor color 1.
#define kWiiGX2RegD1CursorColor1                    0x641C
// Cursor color 2.
#define kWiiGX2RegD1CursorColor2                    0x6420

//
// Display Lookup Table Control registers.
//
// LUT read/write selection.
#define kWiiGX2RegDcLutRwSelect           0x6480
#define kWiiGX2RegDcLutRwSelectUpper      BIT0
// LUT read/write mode.
#define kWiiGX2RegDcLutRwMode             0x6484
#define kWiiGX2RegDcLutRwModePwl          BIT0
// LUT read/write index.
#define kWiiGX2RegDcLutRwIndex            0x6488
#define kWiiGX2RegDcLutRwIndexMask        BITRange(0, 7)
// LUT 30-bit color.
#define kWiiGX2RegDcLutColor              0x6494
#define kWiiGX2RegDcLutColorBlueMask      BITRange(0, 9)
#define kWiiGX2RegDcLutColorGreenShift    10
#define kWiiGX2RegDcLutColorGreenMask     BITRange(10, 19)
#define kWiiGX2RegDcLutColorRedShift      20
#define kWiiGX2RegDcLutColorRedMask       BITRange(20, 29)
// LUT read pipe selection.
#define kWiiGX2RegDcLutReadPipeSelect     0x6498
#define kWiiGX2RegDcLutReadPipeSelect1    BIT0
// LUT write enable mask.
#define kWiiGX2RegDcLutWriteEnMask        0x649C
#define kWiiGX2RegDcLutWriteEnMaskAll     0x3F
// LUT autofill.
#define kWiiGX2RegDcLutAutofill           0x64A0
#define kWiiGX2RegDcLutAutofillStart      BIT0
#define kWiiGX2RegDcLutAutofillDone       BIT1

//
// Display Lookup Table A registers.
//
#define kWiiGX2RegDcLutAControl           0x64C0
#define kWiiGX2RegDcLutABlackOffsetBlue   0x64C4
#define kWiiGX2RegDcLutABlackOffsetGreen  0x64C8
#define kWiiGX2RegDcLutABlackOffsetRed    0x64CC
#define kWiiGX2RegDcLutAWhiteOffsetBlue   0x64D0
#define kWiiGX2RegDcLutAWhiteOffsetGreen  0x64D4
#define kWiiGX2RegDcLutAWhiteOffsetRed    0x64D8

#endif
