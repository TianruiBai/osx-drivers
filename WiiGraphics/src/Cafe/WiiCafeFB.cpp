//
//  WiiCafeFB.hpp
//  Wii U Cafe graphics framebuffer
//
//  Copyright © 2025 John Davis. All rights reserved.
//

#include "WiiCafeFB.hpp"
#include "GX2Regs.hpp"
#include "WiiGX2UserClient.hpp"

OSDefineMetaClassAndStructors(WiiCafeFB, super);

// Latte GPU7_GC vector index (IRQ2 bit 11 = vector 43). Kept here rather
// than in LatteRegs.hpp to avoid a WiiPlatform include from WiiGraphics.
#define kWiiCafeGPU7VectorNumber           43
// Function names exposed by LatteInterruptController via
// callPlatformFunction().
#define kWiiCafeFunctionRegisterDirectIRQ    "LatteRegisterDirectIRQ"
#define kWiiCafeFunctionUnregisterDirectIRQ  "LatteUnregisterDirectIRQ"
#define kWiiCafeLatteControllerClass         "LatteInterruptController"

#define kCursorPosOffset   4

enum {
  kWiiCafeFBDepth32bpp = 0,
  kWiiCafeFBDepth16bpp,
  kWiiCafeFBDepth8bpp,
  kWiiCafeFBDepthMax
};

//
// Overrides IOFramebuffer::init().
//
bool WiiCafeFB::init(OSDictionary *dictionary) {
  WiiCheckDebugArgs();

  _gx2         = NULL;
  _fbMemory     = NULL;

  _currentDisplayModeId = 1;
  _currentDepth         = kWiiCafeFBDepth32bpp;
  _gammaValid           = false;
  _clutValid            = false;

  _cursorBuffer = NULL;
  _cursorHwDesc = NULL;

  _latteInterruptController = NULL;
  _ihHandlerRegistered      = false;

  return super::init(dictionary);
}

//
// Overrides OSObject::free().
//
void WiiCafeFB::free(void) {
  if (_ihHandlerRegistered && _latteInterruptController != NULL) {
    const OSSymbol *sym = OSSymbol::withCString(kWiiCafeFunctionUnregisterDirectIRQ);
    if (sym != NULL) {
      _latteInterruptController->callPlatformFunction(sym, false,
        (void *)(uintptr_t) kWiiCafeGPU7VectorNumber,
        NULL, NULL, NULL);
      sym->release();
    }
    _ihHandlerRegistered = false;
  }
  OSSafeReleaseNULL(_latteInterruptController);

  OSSafeReleaseNULL(_gx2);
  OSSafeReleaseNULL(_cursorHwDesc);

  if (_cursorBuffer != NULL) {
    IOFree(_cursorBuffer, kWiiGX2CursorMaxSize);
    _cursorBuffer = NULL;
  }

  super::free();
}

//
// Overrides IOFramebuffer::start().
//
bool WiiCafeFB::start(IOService *provider) {
  WIISYSLOG("Initializing Cafe framebuffer");

  //
  // Initialize low-level GX2 register access.
  //
  _gx2 = new WiiGX2;
  if ((_gx2 == NULL) || !_gx2->init(provider)) {
    WIISYSLOG("Failed to initialize GX2 register access");
    OSSafeReleaseNULL(_gx2);
    return false;
  }
  _gx2->logHardwareState();

  //
  // Initialize the GPU command processor and default state.
  // If the CP fails to start (e.g. microcode not loaded by boot firmware),
  // we continue in display-only mode — the framebuffer still works.
  //
  if (_gx2->startCP()) {
    WIISYSLOG("GPU command processor started");
    if (_gx2->initGPUDefaultState()) {
      WIISYSLOG("GPU default state initialized");
    } else {
      WIISYSLOG("Failed to initialize GPU default state");
    }

    if (_gx2->setupInterruptRing(false)) {
      WIIDBGLOG("GPU IH ring staged (interrupt delivery masked)");

      //
      // Try to hook the Latte GPU7_GC vector (IRQ2 bit 11) so the IH ring
      // is drained on live interrupts. We reach the controller via a
      // platform-expert lookup + callPlatformFunction so WiiGraphics does
      // not need to link against WiiPlatform.
      //
      OSDictionary *match = IOService::serviceMatching(kWiiCafeLatteControllerClass);
      if (match != NULL) {
        mach_timespec_t waitTime;
        waitTime.tv_sec = 5;
        waitTime.tv_nsec = 0;
        IOService *controller = IOService::waitForService(match, &waitTime);
        match->release();
        if (controller != NULL) {
          const OSSymbol *sym = OSSymbol::withCString(kWiiCafeFunctionRegisterDirectIRQ);
          if (sym != NULL) {
            IOReturn regRet = controller->callPlatformFunction(sym, false,
              (void *)(uintptr_t) kWiiCafeGPU7VectorNumber,
              this,
              (void *) &WiiCafeFB::ihTrampoline,
              this);
            sym->release();
            if (regRet == kIOReturnSuccess) {
              _latteInterruptController = controller; // retained by wait
              _ihHandlerRegistered = true;
              _gx2->setInterruptRingEnabled(true);
              WIIDBGLOG("GPU7_GC IH hook installed on vector %d", kWiiCafeGPU7VectorNumber);
            } else {
              WIISYSLOG("Failed to register GPU7_GC handler (0x%08X)", regRet);
              controller->release();
            }
          } else {
            controller->release();
          }
        } else {
          WIIDBGLOG("LatteInterruptController not found; IH remains polled");
        }
      }
    } else {
      WIISYSLOG("Failed to stage GPU IH ring");
    }

    //
    // Bring up the async DMA engine ring. Non-fatal — failure simply means
    // we cannot issue DMA copies yet, display/CP still work.
    //
    if (_gx2->setupDMARing()) {
      WIIDBGLOG("Async DMA engine ring initialized");
    } else {
      WIIDBGLOG("Async DMA engine ring unavailable");
    }

    //
    // Map the MEM1 (EDRAM) aperture as a bump heap. Used for render-target
    // placement once Phase 3 lands. Non-fatal — if the mapping is refused
    // we silently fall back to MEM2 for everything.
    //
    if (_gx2->setupMEM1Heap()) {
      WIIDBGLOG("MEM1 heap ready (%u bytes)", _gx2->getMEM1Size());
    } else {
      WIIDBGLOG("MEM1 heap unavailable");
    }

    //
    // Phase 3: stage the clear pipeline (VS+PS bytecode, VB + fence
    // pages). Non-fatal: if staging fails we simply never attempt the
    // GPU clear smoke test.
    //
    if (_gx2->setupClearPipeline()) {
      WIIDBGLOG("Clear pipeline ready");
      //
      // Fire a single full-framebuffer black clear as the Phase 3
      // functional gate. Any failure logs SYSLOG but does not block
      // display-path start — the software rasteriser still works. We
      // deliberately clear to a recognisable pattern (dark grey) so an
      // actual GPU write is visible on scanout if it lands.
      //
      IODeviceMemory *fbMem = provider->getDeviceMemoryWithIndex(1);
      if (fbMem != NULL) {
        IOPhysicalAddress fbPhys = fbMem->getPhysicalAddress();
        const float clearColour[4] = { 0.05f, 0.05f, 0.10f, 1.0f };
        if (_gx2->submitColorClear(fbPhys, 1280, 720, 1 /* BGRA */,
                                   clearColour)) {
          WIIDBGLOG("Phase 3 GPU clear smoke test passed (fb phys=0x%08X)",
            (UInt32) fbPhys);
        } else {
          WIISYSLOG("Phase 3 GPU clear smoke test FAILED (fb phys=0x%08X)",
            (UInt32) fbPhys);
        }
      } else {
        WIIDBGLOG("Phase 3 clear skipped: no framebuffer memory");
      }
    } else {
      WIIDBGLOG("Clear pipeline staging failed");
    }

    //
    // Phase 4: stage the shared user fence page used by WiiGX2UserClient
    // submissions. Non-fatal — if allocation fails the user client will
    // be refused at open time but the framebuffer path keeps working.
    //
    if (_gx2->setupUserFence()) {
      WIIDBGLOG("User IB fence page ready (phys=0x%08X)",
        (UInt32) _gx2->getUserFencePhys());
    } else {
      WIIDBGLOG("User IB fence page unavailable (user client disabled)");
    }
  } else {
    WIISYSLOG("GPU command processor failed to start (display-only mode)");
  }
  //
  // Get the framebuffer memory.
  //
  _fbMemory = provider->getDeviceMemoryWithIndex(1);
  if (_fbMemory == NULL) {
    WIISYSLOG("Failed to get framebuffer memory");
    return false;
  }

  if (!super::start(provider)) {
    WIISYSLOG("super::start() returned false");
    return false;
  }

  WIISYSLOG("Initialized Cafe framebuffer");
  return true;
}

//
// Overrides IOFramebuffer::enableController().
//
IOReturn WiiCafeFB::enableController(void) {
  return super::enableController();
}

//
// Static trampoline invoked by LatteInterruptController on every GPU7_GC
// assertion. The Latte ISR calls us with vector locks held and interrupts
// disabled; we must keep this path short. `consumeIHRing()` drains the
// shared-memory IH ring and advances IH_RB_RPTR — no allocation, no
// blocking, no callouts outside of MMIO access.
//
void WiiCafeFB::ihTrampoline(void *target, void * /*refCon*/,
                             IOService * /*nub*/, int /*source*/) {
  WiiCafeFB *self = (WiiCafeFB *) target;
  if (self != NULL) {
    self->serviceIHRing();
  }
}

//
// Drain up to a bounded number of IH entries per interrupt. The Latte
// controller is level-triggered and will re-assert if the GPU keeps pushing
// entries, so we intentionally cap the drain count to keep ISR latency
// bounded. Future phases will dispatch specific source IDs (CP_EOP,
// SCRATCH, DMA_TRAP) to workloop-side consumers.
//
void WiiCafeFB::serviceIHRing(void) {
  enum { kIHBurst = 16 };
  WiiGX2::InterruptEntry entries[kIHBurst];
  UInt32 consumed;

  if (_gx2 == NULL) {
    return;
  }

  consumed = _gx2->consumeIHRing(entries, kIHBurst);
  (void) consumed;
}

//
// Overrides IOFramebuffer::IOFramebuffer().
//
// Gets the framebuffer memory.
//
IODeviceMemory *WiiCafeFB::getApertureRange(IOPixelAperture aperture) {
  if (aperture != kIOFBSystemAperture) {
    return NULL;
  }

  _fbMemory->retain();
  return _fbMemory;
}

//
// Overrides IOFramebuffer::getPixelFormats().
//
// Gets the supported pixel formats.
//
const char *WiiCafeFB::getPixelFormats(void) {
  static const char *pixelFormats =
    IO32BitDirectPixels "\0"
    IO16BitDirectPixels "\0"
    IO8BitIndexedPixels "\0"
    "\0";
  return pixelFormats;
}

//
// Overrides IOFramebuffer::getDisplayModeCount().
//
// Gets the number of supported display modes.
//
IOItemCount WiiCafeFB::getDisplayModeCount(void) {
  WIIDBGLOG("getDisplayModeCount");
  return 1;
}

//
// Overrides IOFramebuffer::getDisplayModes().
//
// Gets the supported display modes.
//
IOReturn WiiCafeFB::getDisplayModes(IODisplayModeID *allDisplayModes) {
  WIIDBGLOG("getDisplayModes");
  *allDisplayModes = 1;
  return kIOReturnSuccess;
}

//
// Overrides IOFramebuffer::getInformationForDisplayMode().
//
// Gets detailed information for the specified display mode.
//
IOReturn WiiCafeFB::getInformationForDisplayMode(IODisplayModeID displayMode, IODisplayModeInformation *info) {
  if ((displayMode == 0) || (displayMode > 1)) {
    return kIOReturnBadArgument;
  }

  bzero(info, sizeof (*info));
  info->nominalWidth  = 1280;
  info->nominalHeight = 720;
  info->refreshRate   = 60 << 16;
  info->maxDepthIndex = kWiiCafeFBDepthMax - 1;

  return kIOReturnSuccess;
}

//
// Overrides IOFramebuffer::getInformationForDisplayMode().
//
// Obsolete method.
//
UInt64 WiiCafeFB::getPixelFormatsForDisplayMode(IODisplayModeID displayMode, IOIndex depth) {
  return 0;
}

//
// Overrides IOFramebuffer::getPixelInformation().
//
// Gets pixel information for the specified display mode.
//
IOReturn WiiCafeFB::getPixelInformation(IODisplayModeID displayMode, IOIndex depth, IOPixelAperture aperture, IOPixelInformation *pixelInfo) {
  if (aperture != kIOFBSystemAperture) {
    return kIOReturnUnsupportedMode;
  }

  if ((displayMode == 0) || (displayMode > 1) || (depth >= kWiiCafeFBDepthMax)) {
    return kIOReturnBadArgument;
  }

  bzero(pixelInfo, sizeof (*pixelInfo));
  pixelInfo->activeWidth  = 1280;
  pixelInfo->activeHeight = 720;

  if (depth == kWiiCafeFBDepth32bpp) {
    pixelInfo->pixelType         = kIORGBDirectPixels;
    pixelInfo->bytesPerRow       = pixelInfo->activeWidth * 4;
    pixelInfo->bitsPerPixel      = 32;
    pixelInfo->bitsPerComponent  = 8;
    pixelInfo->componentCount    = 3;
    pixelInfo->componentMasks[0] = 0xFF0000;
    pixelInfo->componentMasks[1] = 0x00FF00;
    pixelInfo->componentMasks[2] = 0x0000FF;

    strncpy(pixelInfo->pixelFormat, IO32BitDirectPixels, sizeof (pixelInfo->pixelFormat));
  } else if (depth == kWiiCafeFBDepth16bpp) {
    pixelInfo->pixelType         = kIORGBDirectPixels;
    pixelInfo->bytesPerRow       = pixelInfo->activeWidth * 2;
    pixelInfo->bitsPerPixel      = 16;
    pixelInfo->bitsPerComponent  = 5;
    pixelInfo->componentCount    = 3;
    pixelInfo->componentMasks[0] = 0x7C00;
    pixelInfo->componentMasks[1] = 0x03E0;
    pixelInfo->componentMasks[2] = 0x001F;

    strncpy(pixelInfo->pixelFormat, IO16BitDirectPixels, sizeof (pixelInfo->pixelFormat));
  } else if (depth == kWiiCafeFBDepth8bpp) {
    pixelInfo->pixelType         = kIOCLUTPixels;
    pixelInfo->bytesPerRow       = pixelInfo->activeWidth;
    pixelInfo->bitsPerPixel      = 8;
    pixelInfo->bitsPerComponent  = 8;
    pixelInfo->componentCount    = 1;
    pixelInfo->componentMasks[0] = 0xFF;

    strncpy(pixelInfo->pixelFormat, IO8BitIndexedPixels, sizeof (pixelInfo->pixelFormat));
  }

  return kIOReturnSuccess;
}

//
// Overrides IOFramebuffer::getCurrentDisplayMode().
//
// Gets the current display mode.
//
IOReturn WiiCafeFB::getCurrentDisplayMode(IODisplayModeID *displayMode, IOIndex *depth) {
  *displayMode = _currentDisplayModeId;
  *depth       = _currentDepth;

  WIIDBGLOG("Current mode: %d, depth: %d", _currentDisplayModeId, _currentDepth);
  return kIOReturnSuccess;
}

//
// Overrides IOFramebuffer::setDisplayMode().
//
// Sets the current display mode.
//
IOReturn WiiCafeFB::setDisplayMode(IODisplayModeID displayMode, IOIndex depth) {
  UInt32 control;
  UInt32 swap;

  if ((displayMode == 0) || (displayMode > 1) || (depth >= kWiiCafeFBDepthMax)) {
    return kIOReturnBadArgument;
  }

  //
  // Disable display.
  //
  writeReg32(kWiiGX2RegD1GrphEnable, 0);

  //
  // Adjust depth and endianness swapping.
  //
  control = readReg32(kWiiGX2RegD1GrphControl);
  control &= ~(kWiiGX2RegD1GrphControlDepthMask | kWiiGX2RegD1GrphControlFormatMask);

  if (depth == kWiiCafeFBDepth32bpp) {
    control |= kWiiGX2RegD1GrphControlDepth32bpp | kWiiGX2RegD1GrphControlFormat32bppARGB8888;
    swap     = kWiiGX2RegD1GrphSwapControlEndianSwap32Bit;
  } else if (depth == kWiiCafeFBDepth16bpp) {
    control |= kWiiGX2RegD1GrphControlDepth16bpp | kWiiGX2RegD1GrphControlFormat16bppARGB555;
    swap     = kWiiGX2RegD1GrphSwapControlEndianSwap16Bit;
  } else if (depth == kWiiCafeFBDepth8bpp) {
    control |= kWiiGX2RegD1GrphControlDepth8bpp | kWiiGX2RegD1GrphControlFormat8bppIndexed;
    swap     = kWiiGX2RegD1GrphSwapControlEndianSwapNone;
  }

  writeReg32(kWiiGX2RegD1GrphControl, control);
  writeReg32(kWiiGX2RegD1GrphSwapControl, swap);

  //
  // Re-enable display.
  //
  writeReg32(kWiiGX2RegD1GrphEnable, kWiiGX2RegD1GrphEnableBit);

  _currentDisplayModeId = displayMode;
  _currentDepth         = depth;

  return kIOReturnSuccess;
}

//
// Overrides IOFramebuffer::getStartupDisplayMode().
//
// Gets the startup display mode.
//
IOReturn WiiCafeFB::getStartupDisplayMode(IODisplayModeID *displayMode, IOIndex *depth) {
  *displayMode = 1;
  *depth       = kWiiCafeFBDepth32bpp;
  return kIOReturnSuccess;
}

//
// Overrides IOFramebuffer::setCLUTWithEntries().
//
// Sets the color lookup table.
//
IOReturn WiiCafeFB::setCLUTWithEntries(IOColorEntry *colors, UInt32 index, UInt32 numEntries, IOOptionBits options) {
  bool byValue = options & kSetCLUTByValue;

  //
  // Build internal color table.
  //
  for (UInt32 i = 0; i < numEntries; i++) {
    UInt32 offset = byValue ? colors[i].index : index + i;
		if (offset > 255){
      continue;
    }

    _clutEntries[offset].red = colors[i].red >> 8;
    _clutEntries[offset].green = colors[i].green >> 8;
    _clutEntries[offset].blue = colors[i].blue >> 8;
  }

  _clutValid = true;
  loadHardwareLUT();

  return kIOReturnSuccess;
}

//
// Overrides IOFramebuffer::setGammaTable().
//
// Sets the gamma table.
//
IOReturn WiiCafeFB::setGammaTable(UInt32 channelCount, UInt32 dataCount, UInt32 dataWidth, void *data) {
  UInt8   *gammaData8;
  UInt16  *gammaData16;

  //
  // Build internal gamma table.
  // OS X 10.1 uses 8-bit data, 10.2 and newer use 16-bit.
  //
  if (dataWidth == 8) {
    gammaData8 = (UInt8 *)data;
    if (channelCount == 3) {
      bcopy(gammaData8, &_gammaTable, 256 * 3);
    } else if (channelCount == 1) {
      for (UInt32 i = 0; i < 256; i++) {
        _gammaTable.red[i]   = gammaData8[i];
        _gammaTable.green[i] = gammaData8[i];
        _gammaTable.blue[i]  = gammaData8[i];
      }
    } else {
      return kIOReturnUnsupported;
    }
  } else if (dataWidth == 16) {
    gammaData16 = (UInt16 *)data;
    if (channelCount == 3) {
      for (UInt32 i = 0; i < 256; i++) {
        _gammaTable.red[i]   = gammaData16[i] >> 8;
        _gammaTable.green[i] = gammaData16[i + 256] >> 8;
        _gammaTable.blue[i]  = gammaData16[i + 512] >> 8;
      }
    } else if (channelCount == 1) {
      for (UInt32 i = 0; i < 256; i++) {
        _gammaTable.red[i]   = gammaData16[i] >> 8;
        _gammaTable.green[i] = gammaData16[i] >> 8;
        _gammaTable.blue[i]  = gammaData16[i] >> 8;
      }
    } else {
      return kIOReturnUnsupported;
    }
  } else {
    return kIOReturnUnsupported;
  }

  _gammaValid = true;
  loadHardwareLUT();

  return kIOReturnSuccess;
}

//
// Overrides IOFramebuffer::getAttribute().
//
// Gets a framebuffer attribute.
//
IOReturn WiiCafeFB::getAttribute(IOSelect attribute, uintptr_t *value) {
  //
  // Report that a hardware cursor is supported.
  //
  if (attribute == kIOHardwareCursorAttribute) {
    if (value != NULL) {
      *value = 1;
    }
    WIIDBGLOG("Hardware cursor supported");
    return kIOReturnSuccess;
  }

  return super::getAttribute(attribute, value);
}

//
// Overrides IOFramebuffer::setCursorImage().
//
// Sets a cursor image as the current hardware cursor.
//
IOReturn WiiCafeFB::setCursorImage(void *cursorImage) {
  IOHardwareCursorDescriptor  cursorDescriptor;
  IOHardwareCursorInfo        cursorInfo;
  IOByteCount                 length;

  //
  // Allocate cursor memory if needed.
  // Max cursor is 32x32x4 (one 4KB page). Cursor must be page-aligned.
  //
  if (_cursorHwDesc == NULL) {
    _cursorHwDesc = IOBufferMemoryDescriptor::withOptions(kIOMemoryPhysicallyContiguous, kWiiGX2CursorMemSize, PAGE_SIZE);
    if (_cursorHwDesc == NULL) {
      return kIOReturnNoMemory;
    }

    _cursorHwPtr = (volatile UInt32*) _cursorHwDesc->getBytesNoCopy();
    _cursorHwPhysAddr  = _cursorHwDesc->getPhysicalSegment(0, &length);
  }

  if (_cursorBuffer == NULL) {
    _cursorBuffer = (UInt32 *)IOMalloc(kWiiGX2CursorMaxSize);
    if (_cursorBuffer == NULL) {
      return kIOReturnNoMemory;
    }
  }

  //
  // Setup cursor descriptor / info structures and convert the cursor image.
  //
  bzero(&cursorDescriptor, sizeof (cursorDescriptor));
  cursorDescriptor.majorVersion = kHardwareCursorDescriptorMajorVersion;
  cursorDescriptor.minorVersion = kHardwareCursorDescriptorMinorVersion;
  cursorDescriptor.width        = kWiiGX2MaxCursorWidth;
  cursorDescriptor.height       = kWiiGX2MaxCursorHeight;
  cursorDescriptor.bitDepth     = 32U;

  bzero(&cursorInfo, sizeof (cursorInfo));
  cursorInfo.majorVersion       = kHardwareCursorInfoMajorVersion;
  cursorInfo.minorVersion       = kHardwareCursorInfoMinorVersion;
  cursorInfo.hardwareCursorData = (UInt8*) _cursorBuffer;

  if (!convertCursorImage(cursorImage, &cursorDescriptor, &cursorInfo)) {
    WIIDBGLOG("Failed to convert hardware cursor image");
    return kIOReturnUnsupported;
  }
  if ((cursorInfo.cursorWidth == 0) || (cursorInfo.cursorHeight == 0)) {
    WIIDBGLOG("Converted hardware cursor image is invalid size");
    return kIOReturnUnsupported;
  }
  WIIDBGLOG("Converted hardware cursor image at %p (%ux%u)", cursorInfo.hardwareCursorData, cursorInfo.cursorWidth, cursorInfo.cursorHeight);

  //
  // Copy cursor image to hardware buffer.
  //
  // Cursor must be swapped to little endian, and each row needs to be 64 pixels wide.
  //
  for (UInt32 h = 0; h < cursorInfo.cursorHeight; h += 2) {
    for (UInt32 w = 0; w < cursorInfo.cursorWidth; w++) {
      _cursorHwPtr[(h * 64) + w] = OSSwapHostToLittleInt32(_cursorBuffer[h * cursorInfo.cursorWidth + w]);
      _cursorHwPtr[((h + 1) * 64) + w] = OSSwapHostToLittleInt32(_cursorBuffer[(h + 1) * cursorInfo.cursorWidth + w]);
    }
  }
  flushDataCache(_cursorHwPtr, kWiiGX2CursorMemSize);

  //
  // Update hardware buffer to signal to hardware there is a new cursor.
  // OS X seems to offset the position by 4, set hotspot as the hardware cannot handle a negative position.
  //
  writeReg32(kWiiGX2RegD1CursorSurfaceAddress, _cursorHwPhysAddr);
  writeReg32(kWiiGX2RegD1CursorSize,
    ((cursorInfo.cursorHeight - 1) & kWiiGX2RegD1CursorSizeHeightMask) |
    (((cursorInfo.cursorWidth - 1) << kWiiGX2RegD1CursorSizeWidthShift) & kWiiGX2RegD1CursorSizeWidthMask));
  writeReg32(kWiiGX2RegD1CursorHotSpot, (kCursorPosOffset & kWiiGX2RegD1CursorHotSpotYMask) |
  ((kCursorPosOffset << kWiiGX2RegD1CursorHotSpotXShift) & kWiiGX2RegD1CursorHotSpotXMask));
  writeReg32(kWiiGX2RegD1CursorControl, (readReg32(kWiiGX2RegD1CursorControl) & kWiiGX2RegD1CursorControlEnable) | kWiiGX2RegD1CursorControlMode32BitUnAlpha);

  return kIOReturnSuccess;
}

//
// Overrides IOFramebuffer::setCursorState().
//
// Sets the position and visibility of the hardware cursor.
//
IOReturn WiiCafeFB::setCursorState(SInt32 x, SInt32 y, bool visible) {
  UInt32 cursorControl;

  writeReg32(kWiiGX2RegD1CursorPosition,
    ((y + kCursorPosOffset) & kWiiGX2RegD1CursorPositionYMask) |
    (((x + kCursorPosOffset) << kWiiGX2RegD1CursorPositionXShift) & kWiiGX2RegD1CursorPositionXMask));
  cursorControl = readReg32(kWiiGX2RegD1CursorControl);
  if (visible) {
    cursorControl |= kWiiGX2RegD1CursorControlEnable;
  } else {
    cursorControl &= ~(kWiiGX2RegD1CursorControlEnable);
  }
  writeReg32(kWiiGX2RegD1CursorControl, cursorControl);
  return kIOReturnSuccess;
}

//
// Load color/gamma tables into the hardware.
//
void WiiCafeFB::loadHardwareLUT(void) {
  UInt32 colorValue;

  if (!_clutValid || !_gammaValid) {
    return;
  }

  //
  // Reset LUT A.
  //
  writeReg32(kWiiGX2RegDcLutAControl, 0);
  writeReg32(kWiiGX2RegDcLutABlackOffsetBlue, 0);
  writeReg32(kWiiGX2RegDcLutABlackOffsetGreen, 0);
  writeReg32(kWiiGX2RegDcLutABlackOffsetRed, 0);
  writeReg32(kWiiGX2RegDcLutAWhiteOffsetBlue, 0xFFFF);
  writeReg32(kWiiGX2RegDcLutAWhiteOffsetGreen, 0xFFFF);
  writeReg32(kWiiGX2RegDcLutAWhiteOffsetRed, 0xFFFF);

  //
  // Select LUT A for writing color info.
  //
  writeReg32(kWiiGX2RegDcLutRwSelect, 0);
  writeReg32(kWiiGX2RegDcLutRwMode, 0);
  writeReg32(kWiiGX2RegDcLutWriteEnMask, kWiiGX2RegDcLutWriteEnMaskAll);

  //
  // Only load indexed colors in 8-bit mode.
  // Other modes use generated LUT.
  //
  if (_currentDepth == kWiiCafeFBDepth8bpp) {
    writeReg32(kWiiGX2RegDcLutRwIndex, 0);
    for (UInt32 i = 0; i < 256; i++) {
      //
      // Write each color to the LUT.
      // Gamma/color combo is 8-bit, need to shift over to 10-bit.
      //
      colorValue  = (_gammaTable.blue[_clutEntries[i].blue]    << 2) & kWiiGX2RegDcLutColorBlueMask;
      colorValue |= ((_gammaTable.green[_clutEntries[i].green] << 2) << kWiiGX2RegDcLutColorGreenShift) & kWiiGX2RegDcLutColorGreenMask;
      colorValue |= ((_gammaTable.red[_clutEntries[i].red]     << 2) << kWiiGX2RegDcLutColorRedShift) & kWiiGX2RegDcLutColorRedMask;
      writeReg32(kWiiGX2RegDcLutColor, colorValue);
    }
  } else {
    //
    // Start autofill of LUT and wait for completion.
    //
    writeReg32(kWiiGX2RegDcLutAutofill, kWiiGX2RegDcLutAutofillStart);
    while ((readReg32(kWiiGX2RegDcLutAutofill) & kWiiGX2RegDcLutAutofillDone) == 0);
  }

  //
  // Use LUT A for the primary graphics.
  //
  writeReg32(kWiiGX2RegD1GrphLutSelect, 0);
}

//
// Phase 4: IOUserClient factory.
//
// Invoked by the IOKit framework when a user process calls IOServiceOpen
// on the WiiCafeFB service.
//
// Type 0 and any other non-GX2 type must be delegated to IOFramebuffer's
// built-in user-client factory so WindowServer can start in plain
// framebuffer mode even if the GPU command processor is unavailable.
// Only our explicit kWiiGX2UCType opens should create WiiGX2UserClient.
//
IOReturn WiiCafeFB::newUserClient(task_t owningTask, void *securityID,
                                  UInt32 type, IOUserClient **handler) {
  WiiGX2UserClient *client;

  if (handler == NULL) {
    return kIOReturnBadArgument;
  }
  *handler = NULL;

  if (type != kWiiGX2UCType) {
    WIIDBGLOG("newUserClient: delegating type 0x%08X to IOFramebuffer", type);
    return super::newUserClient(owningTask, securityID, type, handler);
  }

  if (_gx2 == NULL || !_gx2->isCPRunning()) {
    WIISYSLOG("newUserClient: GX2 user client requested but GPU not ready");
    return kIOReturnNotReady;
  }

  client = new WiiGX2UserClient;
  if (client == NULL) {
    return kIOReturnNoMemory;
  }
  if (!client->initWithTask(owningTask, securityID, type)) {
    client->release();
    return kIOReturnInternalError;
  }
  if (!client->attach(this)) {
    client->release();
    return kIOReturnInternalError;
  }
  if (!client->start(this)) {
    client->detach(this);
    client->release();
    return kIOReturnInternalError;
  }

  *handler = client;
  return kIOReturnSuccess;
}
