//
//  EHCIRegs.hpp
//  EHCI USB controller register definitions
//
//  Copyright © 2025 John Davis. All rights reserved.
//

#ifndef EHCIRegs_hpp
#define EHCIRegs_hpp

#include <IOKit/usb/USB.h>

#include "WiiCommon.hpp"

#ifndef USB_CONSTANT16
#define USB_CONSTANT16(x)	((((x) >> 8) & 0x0FF) | ((x & 0xFF) << 8))
#endif

// Capability registers.
#define kEHCIRegCapLengthVersion             0x00
#define kEHCIRegCapLengthMask                0x000000FF
#define kEHCIRegHCIVersionShift              16
#define kEHCIRegHCIVersionMask               0xFFFF0000
#define kEHCIRegHCSParams                    0x04
#define kEHCIRegHCSParamsNumPortsMask        0x0000000F
#define kEHCIRegHCSParamsPortPowerControl    0x00000010
#define kEHCIRegHCSPParams                   0x08

// Operational register offsets, relative to CAPLENGTH.
#define kEHCIRegUSBCmd                       0x00
#define kEHCIRegUSBCmdRunStop                0x00000001
#define kEHCIRegUSBCmdHostControllerReset    0x00000002
#define kEHCIRegUSBCmdPeriodicScheduleEnable 0x00000010
#define kEHCIRegUSBCmdAsyncScheduleEnable    0x00000020
#define kEHCIRegUSBCmdInterruptOnAsyncAdvance 0x00000040
#define kEHCIRegUSBSts                       0x04
#define kEHCIRegUSBStsUSBInterrupt           0x00000001
#define kEHCIRegUSBStsUSBErrorInterrupt      0x00000002
#define kEHCIRegUSBStsPortChangeDetect       0x00000004
#define kEHCIRegUSBStsFrameListRollover      0x00000008
#define kEHCIRegUSBStsHostSystemError        0x00000010
#define kEHCIRegUSBStsInterruptOnAsyncAdvance 0x00000020
#define kEHCIRegUSBStsHCHalted               0x00001000
#define kEHCIRegUSBStsPeriodicScheduleStatus 0x00004000
#define kEHCIRegUSBStsAsyncScheduleStatus    0x00008000
#define kEHCIRegUSBIntr                      0x08
#define kEHCIRegUSBIntrUSBInterruptEnable    0x00000001
#define kEHCIRegUSBIntrUSBErrorInterruptEnable 0x00000002
#define kEHCIRegUSBIntrPortChangeEnable      0x00000004
#define kEHCIRegUSBIntrHostSystemErrorEnable 0x00000010
#define kEHCIRegFrameIndex                   0x0C
#define kEHCIRegCtrlDSegment                 0x10
#define kEHCIRegPeriodicListBase            0x14
#define kEHCIRegAsyncListAddr               0x18
#define kEHCIRegConfigFlag                   0x40
#define kEHCIRegConfigFlagRoutePortsEHCI     0x00000001
#define kEHCIRegPortStatusControlBase        0x44

#define kEHCIRegPortStatusControlConnection           0x00000001
#define kEHCIRegPortStatusControlConnectionChange     0x00000002
#define kEHCIRegPortStatusControlEnable               0x00000004
#define kEHCIRegPortStatusControlEnableChange         0x00000008
#define kEHCIRegPortStatusControlOverCurrentActive    0x00000010
#define kEHCIRegPortStatusControlOverCurrentChange    0x00000020
#define kEHCIRegPortStatusControlForcePortResume      0x00000040
#define kEHCIRegPortStatusControlSuspend              0x00000080
#define kEHCIRegPortStatusControlPortReset            0x00000100
#define kEHCIRegPortStatusControlLineStatusMask       0x00000C00
#define kEHCIRegPortStatusControlLineStatusKState     0x00000400
#define kEHCIRegPortStatusControlLineStatusJState     0x00000800
#define kEHCIRegPortStatusControlPortPower            0x00001000
#define kEHCIRegPortStatusControlPortOwner            0x00002000
#define kEHCIRegPortStatusControlWakeOnConnect        0x00100000
#define kEHCIRegPortStatusControlWakeOnDisconnect     0x00200000
#define kEHCIRegPortStatusControlWakeOnOverCurrent    0x00400000

#define kEHCIRegPortStatusControlChangeMask \
	(kEHCIRegPortStatusControlConnectionChange | kEHCIRegPortStatusControlEnableChange | kEHCIRegPortStatusControlOverCurrentChange)

#define kEHCIRegPortStatusControlPreserveMask \
	(kEHCIRegPortStatusControlEnable | kEHCIRegPortStatusControlForcePortResume | kEHCIRegPortStatusControlSuspend \
	 | kEHCIRegPortStatusControlPortReset | kEHCIRegPortStatusControlPortPower | kEHCIRegPortStatusControlPortOwner \
	 | kEHCIRegPortStatusControlWakeOnConnect | kEHCIRegPortStatusControlWakeOnDisconnect | kEHCIRegPortStatusControlWakeOnOverCurrent)

#define kEHCIRegPortStatusControlHandleMask \
	(kEHCIRegPortStatusControlConnection | kEHCIRegPortStatusControlConnectionChange | kEHCIRegPortStatusControlEnable \
	 | kEHCIRegPortStatusControlEnableChange | kEHCIRegPortStatusControlOverCurrentActive | kEHCIRegPortStatusControlOverCurrentChange \
	 | kEHCIRegPortStatusControlForcePortResume | kEHCIRegPortStatusControlSuspend | kEHCIRegPortStatusControlPortReset \
	 | kEHCIRegPortStatusControlLineStatusMask | kEHCIRegPortStatusControlPortPower | kEHCIRegPortStatusControlPortOwner)

// Asynchronous queue head/qTD descriptor definitions.
#define kEHCIListPointerTerminate            0x00000001

#define kEHCIQHLinkTypeQH                    0x00000002
#define kEHCIQHLinkPointerMask               0xFFFFFFE0

#define kEHCIQHInfo1EndpointShift            8
#define kEHCIQHInfo1SpeedShift               12
#define kEHCIQHInfo1HeadOfReclamation        BIT15
#define kEHCIQHInfo1ToggleControl            BIT14
#define kEHCIQHInfo1HighSpeed                (2U << kEHCIQHInfo1SpeedShift)
#define kEHCIQHInfo1LowSpeed                 (1U << kEHCIQHInfo1SpeedShift)
#define kEHCIQHInfo1FullSpeed                (0U << kEHCIQHInfo1SpeedShift)
#define kEHCIQHInfo1MaxPacketShift           16
#define kEHCIQHInfo1ControlEndpoint          BIT27
#define kEHCIQHInfo1NAKReloadShift           28

#define kEHCIQHInfo2SMask                    0x000000FF
#define kEHCIQHInfo2CMask                    0x0000FF00
#define kEHCIQHInfo2HubAddrShift             16
#define kEHCIQHInfo2PortShift                23
#define kEHCIQHInfo2MultShift                30
#define kEHCIQHInfo2Mult1                    (1U << kEHCIQHInfo2MultShift)

#define kEHCIqTDTokenPingState               BIT0
#define kEHCIqTDTokenSplitState              BIT1
#define kEHCIqTDTokenMissedMicroFrame        BIT2
#define kEHCIqTDTokenTransactionError        BIT3
#define kEHCIqTDTokenBabbleDetected          BIT4
#define kEHCIqTDTokenDataBufferError         BIT5
#define kEHCIqTDTokenHalted                  BIT6
#define kEHCIqTDTokenActive                  BIT7
#define kEHCIqTDTokenPIDShift                8
#define kEHCIqTDTokenErrorCounterShift       10
#define kEHCIqTDTokenInterruptOnComplete     BIT15
#define kEHCIqTDTokenBytesShift              16
#define kEHCIqTDTokenBytesMask               0x7FFF0000
#define kEHCIqTDTokenDataToggle              BIT31

#define kEHCIqTDPIDOut                       0
#define kEHCIqTDPIDIn                        1
#define kEHCIqTDPIDSetup                     2

#endif