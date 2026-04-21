//
//  WiiEspresso.hpp
//  Wii U Espresso CPU registers and SMP helpers
//
// See https://wiiubrew.org/wiki/Hardware/Espresso.
#ifndef WiiEspresso_hpp
#define WiiEspresso_hpp

#include "WiiCommon.hpp"

// Espresso special-purpose register indices.
#define kEspressoSPR_HID5                 0x3B0
#define kEspressoSPR_SCR                  0x3B3
#define kEspressoSPR_CAR                  0x3B4
#define kEspressoSPR_BCR                  0x3B5
#define kEspressoSPR_PIR                  0x3EF

// HID5 bits.
#define kEspressoHID5_EnablePIR           BIT30

// Secondary-core boot stub address in Cafe mode. This is the high-vector
// MEM0/ancast mapping used by secondary Espresso cores on Wii U.
#define kEspressoSecondaryBootStub        0x08100100
#define kEspressoSecondaryBootStubLength  0x40

// Cache coherency masks mirrored from Cafe OS / Linux Wii U SMP bring-up.
#define kEspressoCAR_Init                 0xFC100000
#define kEspressoBCR_Init                 0x08000000

#define WiiEspresso_StringifyValue(value) #value
#define WiiEspresso_Stringify(value) WiiEspresso_StringifyValue(value)

// PPC mfspr/mtspr require compile-time SPR immediates. Keep these as macros
// so Tiger's GCC can fold the register number directly into the instruction.
#define mfEspressoSPR(spr) \
({ \
  UInt32 __espressoValue; \
  asm volatile("mfspr %0, " WiiEspresso_Stringify(spr) \
               : "=r" (__espressoValue)); \
  __espressoValue; \
})

#define mtEspressoSPR(spr, value) \
do { \
  UInt32 __espressoValue = (value); \
  asm volatile("mtspr " WiiEspresso_Stringify(spr) ", %0" \
               :: "r" (__espressoValue)); \
} while (0)

// Corrected SCR ICI mapping on Espresso: core 0 = bit 20, core 1 = bit 19,
// core 2 = bit 18.
static inline UInt32 espressoICIMaskForCore(UInt32 core) {
  if (core >= 3) {
    return 0;
  }
  return (1U << (20 - core));
}

// SCR wake bits: core 0 = bit 23, core 1 = bit 22, core 2 = bit 21.
static inline UInt32 espressoWakeMaskForCore(UInt32 core) {
  if (core >= 3) {
    return 0;
  }
  return (1U << (23 - core));
}

#endif