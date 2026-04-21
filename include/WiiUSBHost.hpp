//
//  WiiUSBHost.hpp
//  Wii and Wii U USB host-controller MMIO layout
//
// See https://wiiubrew.org/wiki/Hardware/USB_Host_Controller.
//
//  Copyright © 2025 John Davis. All rights reserved.
//

#ifndef WiiUSBHost_hpp
#define WiiUSBHost_hpp

// All Wii/Wii U USB host-controller register blocks are 64 KiB and big endian.
#define kWiiUSBHostControllerLength   0x10000

// EHCI/OHCI-0 block present on both Hollywood and Latte.
#define kWiiUSBHostEHCI0Base          0x0D040000
#define kWiiUSBHostOHCI0_0Base        0x0D050000
#define kWiiUSBHostOHCI0_1Base        0x0D060000

// Additional Latte-only EHCI/OHCI blocks.
#define kWiiUSBHostEHCI1Base          0x0D120000
#define kWiiUSBHostOHCI1_0Base        0x0D130000
#define kWiiUSBHostEHCI2Base          0x0D140000
#define kWiiUSBHostOHCI2_0Base        0x0D150000

#endif