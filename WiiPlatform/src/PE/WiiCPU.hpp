//
//  WiiCPU.hpp
//  Wii CPU platform device
//
//  Copyright © 2025 John Davis. All rights reserved.
//

#ifndef WiiCPU_hpp
#define WiiCPU_hpp

#include <IOKit/IOCPU.h>
#include "WiiCommon.hpp"
#include "WiiEspresso.hpp"

//
// Represents a Wii platform CPU.
//
class WiiCPU : public IOCPU {
  OSDeclareDefaultStructors(WiiCPU);
  WiiDeclareLogFunctions("cpu");
  typedef IOCPU super;

private:
  bool                _isBootCPU;
  UInt32              _numCPUs;
  bool                _isCafe;
  bool                _smpEnabled;

  //
  // Writes the trampoline code to the secondary boot vector address.
  // The trampoline is a small block of PPC instructions that sets up r3
  // and branches to start_paddr, which is the XNU secondary entry point.
  //
  bool writeBootTrampoline(vm_offset_t start_paddr);

  //
  // Initialize Espresso SPRs for cache coherency on the current core.
  //
  void initEspressoCoherency(void);

  //
  // Send an ICI (Inter-Core Interrupt) to a target core.
  //
  void sendICI(UInt32 targetCPU);

  //
  // Acknowledge an ICI on the current core.
  //
  void ackICI(void);

  void ipiHandler(void *refCon, void *nub, int source);

public:
  //
  // Overrides.
  //
  bool init(OSDictionary *dictionary = 0);
  bool start(IOService *provider);
  void initCPU(bool boot);
  void quiesceCPU(void);
  kern_return_t startCPU(vm_offset_t start_paddr, vm_offset_t arg_paddr);
  void haltCPU(void);
  void signalCPU(IOCPU *target);
  void signalCPUDeferred(IOCPU *target);
  void signalCPUCancel(IOCPU *target);
  const OSSymbol *getCPUName(void);
};

#endif
