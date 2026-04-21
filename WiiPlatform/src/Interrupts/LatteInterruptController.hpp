//
//  LatteInterruptController.hpp
//  Wii U Latte chipset interrupt controller
//
//  Copyright © 2025 John Davis. All rights reserved.
//

#ifndef LatteInterruptController_hpp
#define LatteInterruptController_hpp

#include <IOKit/IOInterrupts.h>
#include <IOKit/IOInterruptController.h>

#include "WiiCommon.hpp"

#ifdef WII_TIGER_IOINTERRUPT_API
typedef int IOInterruptVectorNumber;
#endif

//
// Represents the Latte chipset interrupt controller.
//
class LatteInterruptController : public IOInterruptController {
  OSDeclareDefaultStructors(LatteInterruptController);
  WiiDeclareLogFunctions("latteic");
  typedef IOInterruptController super;

private:
  IOMemoryMap         *_memoryMap;
  volatile void       *_baseAddr;

  inline UInt32 readReg32(UInt32 offset) {
    return OSReadBigInt32(_baseAddr, offset);
  }
  inline void writeReg32(UInt32 offset, UInt32 data) {
    OSWriteBigInt32(_baseAddr, offset, data);
  }

public:
  //
  // Overrides.
  //
  bool init(OSDictionary *dictionary = 0);
  bool start(IOService *provider);
  IOInterruptAction getInterruptHandlerAddress(void);
  IOReturn handleInterrupt(void *refCon, IOService *nub, int source);
  int getVectorType(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector);
  void disableVectorHard(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector);
  void enableVector(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector);

  //
  // Direct-vector registration for kexts whose providers are not wired up
  // with standard IOInterruptSpecifier properties (e.g. WiiGraphics hooking
  // the GPU7_GC vector). Returns kIOReturnSuccess on success, or an error if
  // the vector index is out of range or already registered. The handler is
  // invoked on every assertion of the specified vector until
  // `unregisterDirectHandler()` is called.
  //
  IOReturn registerDirectHandler(IOInterruptVectorNumber vectorNumber,
                                 void *target,
                                 IOInterruptHandler handler,
                                 void *refCon);
  IOReturn unregisterDirectHandler(IOInterruptVectorNumber vectorNumber);

  //
  // Cross-kext entry point. Responds to the following named functions:
  //
  //   "LatteRegisterDirectIRQ"  (UInt32 vectorNumber, void *target,
  //                              IOInterruptHandler handler, void *refCon)
  //   "LatteUnregisterDirectIRQ"(UInt32 vectorNumber, ...)
  //
  // This keeps consumers like WiiGraphics free of a direct static link
  // against LatteInterruptController while still allowing them to hook
  // vectors that are not declared in their device-tree interrupt specifier.
  //
  virtual IOReturn callPlatformFunction(const OSSymbol *functionName,
                                        bool waitForFunction,
                                        void *param1, void *param2,
                                        void *param3, void *param4);
};

// Function names usable via callPlatformFunction().
#define kLatteFunctionRegisterDirectIRQ    "LatteRegisterDirectIRQ"
#define kLatteFunctionUnregisterDirectIRQ  "LatteUnregisterDirectIRQ"

#endif
