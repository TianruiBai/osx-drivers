//
//  WiiCafeFB.hpp
//  Wii U Cafe graphics framebuffer
//
//  Copyright © 2025 John Davis. All rights reserved.
//

#ifndef WiiCafeFB_hpp
#define WiiCafeFB_hpp

#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/graphics/IOFramebuffer.h>
#include "WiiGX2.hpp"
#include "WiiCommon.hpp"

typedef struct {
  UInt8 red[256];
  UInt8 green[256];
  UInt8 blue[256];
} CafeGammaTable;

typedef struct {
  UInt8 red;
  UInt8 green;
  UInt8 blue;
} CafeClutEntry;

//
// Represents the Wii U graphics framebuffer.
//
class WiiCafeFB : public IOFramebuffer {
  OSDeclareDefaultStructors(WiiCafeFB);
  WiiDeclareLogFunctions("fb");
  typedef IOFramebuffer super;

private:
  WiiGX2              *_gx2;
  IODeviceMemory      *_fbMemory;

  // Display and colors.
  IODisplayModeID     _currentDisplayModeId;
  IOIndex             _currentDepth;
  CafeGammaTable      _gammaTable;
  CafeClutEntry       _clutEntries[256];
  bool                _gammaValid;
  bool                _clutValid;

  // Hardware cursor.
  UInt32                    *_cursorBuffer;
  IOBufferMemoryDescriptor  *_cursorHwDesc;
  volatile UInt32           *_cursorHwPtr;
  IOPhysicalAddress         _cursorHwPhysAddr;

  // Latte interrupt controller hook for GPU7_GC vector. We retain the
  // IOService pointer so we can unregister on stop/free. The hook is
  // installed lazily from ::start() via callPlatformFunction() and calls
  // back into WiiCafeFB::ihTrampoline(), which drains the GPU IH ring.
  IOService                 *_latteInterruptController;
  bool                       _ihHandlerRegistered;
  static void ihTrampoline(void *target, void *refCon, IOService *nub,
                           int source);
  void serviceIHRing(void);

  inline UInt32 readReg32(UInt32 offset) {
    return (_gx2 != NULL) ? _gx2->readReg32(offset) : 0;
  }
  inline void writeReg32(UInt32 offset, UInt32 data) {
    if (_gx2 != NULL) {
      _gx2->writeReg32(offset, data);
    }
  }

  void loadHardwareLUT(void);

public:
  //
  // Overrides.
  //
  bool init(OSDictionary *dictionary = 0);
  void free(void);
  bool start(IOService *provider);
  IOReturn enableController(void);
  IODeviceMemory *getApertureRange(IOPixelAperture aperture);
  const char *getPixelFormats(void);
  IOItemCount getDisplayModeCount(void);
  IOReturn getDisplayModes( IODisplayModeID * allDisplayModes);
  IOReturn getInformationForDisplayMode(IODisplayModeID displayMode, IODisplayModeInformation *info);
  UInt64 getPixelFormatsForDisplayMode(IODisplayModeID displayMode, IOIndex depth);
  IOReturn getPixelInformation(IODisplayModeID displayMode, IOIndex depth, IOPixelAperture aperture, IOPixelInformation *pixelInfo);
  IOReturn getCurrentDisplayMode(IODisplayModeID *displayMode, IOIndex *depth);
  IOReturn setDisplayMode(IODisplayModeID displayMode, IOIndex depth);
  IOReturn getStartupDisplayMode(IODisplayModeID *displayMode, IOIndex *depth);
  IOReturn setCLUTWithEntries(IOColorEntry *colors, UInt32 index, UInt32 numEntries, IOOptionBits options);
  IOReturn setGammaTable(UInt32 channelCount, UInt32 dataCount, UInt32 dataWidth, void *data);
  IOReturn getAttribute(IOSelect attribute, uintptr_t *value);
  IOReturn setCursorImage(void *cursorImage);
  IOReturn setCursorState(SInt32 x, SInt32 y, bool visible);

  // Accessor used by WiiGX2UserClient to reach the underlying GPU engine.
  WiiGX2 *getGX2(void) const { return _gx2; }

  // IOUserClient factory for userspace command submission. Called by the
  // IOKit framework from IOServiceOpen on behalf of a user task.
  virtual IOReturn newUserClient(task_t owningTask, void *securityID,
                                 UInt32 type, IOUserClient **handler);
};

#endif
