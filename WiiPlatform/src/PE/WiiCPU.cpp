//
//  WiiCPU.cpp
//  Wii CPU platform device
//
//  Copyright © 2025 John Davis. All rights reserved.
//

#include <ppc/proc_reg.h>
#include <IOKit/IODeviceTreeSupport.h>
#include "WiiCPU.hpp"
#include "WiiPE.hpp"

OSDefineMetaClassAndStructors(WiiCPU, super);

static IOCPUInterruptController *gCPUIC;

//
// Overrides IOCPU::init()
//
bool WiiCPU::init(OSDictionary *dictionary) {
  WiiCheckDebugArgs();
  _isCafe     = false;
  _smpEnabled = false;
  return super::init(dictionary);
}

//
// Overrides IOCPU::start()
//
bool WiiCPU::start(IOService *provider) {
  kern_return_t       result;
  IORegistryEntry     *cpusRegEntry;
  OSIterator          *cpuIterator;
  IORegistryEntry     *cpuEntry;
  OSData              *tmpData;
  UInt32              physCPU;
  ml_processor_info_t processor_info;

  //
  // Ensure platform expert is WiiPE.
  //
  if (OSDynamicCast(WiiPE, getPlatform()) == NULL) {
    WIISYSLOG("Current platform is not a Wii");
    return false;
  }

  if (!super::start(provider)) {
    WIISYSLOG("super::start() returned false");
    return false;
  }

  //
  // Detect platform type.
  //
  _isCafe = checkPlatformCafe();

  //
  // SMP is only available on Cafe (Wii U) with 3 Espresso cores.
  // Enable it by default there and keep a boot-arg escape hatch for fallback.
  //
  _smpEnabled = _isCafe && !checkKernelArgument("-wiinosmp");

  //
  // Count CPUs from the device tree.
  //
  _numCPUs = 0;
  cpusRegEntry = fromPath("/cpus", gIODTPlane);
  if (cpusRegEntry == NULL) {
    WIISYSLOG("Failed to get /cpus from the device tree");
    return false;
  }

  cpuIterator = cpusRegEntry->getChildIterator(gIODTPlane);
  if (cpuIterator != NULL) {
    while ((cpuEntry = OSDynamicCast(IORegistryEntry, cpuIterator->getNextObject())) != NULL) {
      _numCPUs++;
    }
    cpuIterator->release();
  }
  cpusRegEntry->release();

  if (_numCPUs == 0) {
    WIISYSLOG("No CPUs found in the device tree");
    return false;
  }

  //
  // If SMP is not enabled, limit to 1 CPU.
  //
  if (!_smpEnabled) {
    _numCPUs = 1;
  }
  WIISYSLOG("SMP: %s, CPU count: %u, platform: %s",
    _smpEnabled ? "enabled" : "disabled", _numCPUs, _isCafe ? "Cafe" : "Wii");

  //
  // Set physical CPU number from the "reg" property.
  //
  tmpData = OSDynamicCast(OSData, provider->getProperty("reg"));
  if (tmpData == NULL) {
    WIISYSLOG("Failed to read reg property");
    return false;
  }
  physCPU = *((UInt32 *)tmpData->getBytesNoCopy());
  setCPUNumber(physCPU);

  //
  // Check if boot CPU.
  //
  _isBootCPU = false;
  tmpData = OSDynamicCast(OSData, provider->getProperty("state"));
  if (tmpData == 0) {
    WIISYSLOG("Failed to read state property");
    return false;
  }
  if (strcmp((char *)tmpData->getBytesNoCopy(), "running") == 0) {
    _isBootCPU = true;
  }
  WIISYSLOG("Physical CPU number: %u, boot CPU: %u", physCPU, _isBootCPU);

  //
  // If SMP is disabled, skip non-boot CPUs entirely.
  //
  if (!_smpEnabled && !_isBootCPU) {
    WIISYSLOG("SMP disabled, skipping secondary CPU %u", physCPU);
    return true;
  }

  //
  // Create the CPU interrupt controller (once, on boot CPU).
  // Only required when SMP is actually enabled; without it we should
  // behave exactly like the pre-SMP single-core path so we do not
  // perturb interrupt dispatch on the boot CPU.
  //
  if (_isBootCPU && _smpEnabled) {
    gCPUIC = new IOCPUInterruptController;
    if (gCPUIC == NULL) {
      WIISYSLOG("Failed to create IOCPUInterruptController");
      return false;
    }
    if (gCPUIC->initCPUInterruptController(_numCPUs) != kIOReturnSuccess) {
      WIISYSLOG("Failed to initialize IOCPUInterruptController");
      return false;
    }
    gCPUIC->attach(this);
    gCPUIC->registerCPUInterruptController();
  }

  //
  // CPU starts out uninitialized.
  //
  setCPUState(kIOCPUStateUninitalized);

  //
  // Register the CPU with XNU and start it.
  //
  WIISYSLOG("Registering CPU %u with XNU (boot=%u, smp=%u)", physCPU, _isBootCPU, _smpEnabled);
  if (physCPU < _numCPUs) {
    processor_info.cpu_id           = (cpu_id_t)this;
    processor_info.boot_cpu         = _isBootCPU;
    //
    // start_paddr is the physical address a secondary core will begin
    // executing at. For the boot CPU it is unused (the core is already
    // running); pass the stub address anyway to keep the value meaningful.
    //
    processor_info.start_paddr      = kEspressoSecondaryBootStub;
    //
    // XNU treats l2cr_value == 0 as "do not modify L2CR" on PPC. Espresso
    // secondary cores power up with L2 disabled; turning L2 on per-core is
    // left as a later optimization once SMP is validated.
    //
    processor_info.l2cr_value       = 0;
    processor_info.supports_nap     = false;
    processor_info.time_base_enable = NULL;

    result = ml_processor_register(&processor_info, &machProcessor, &ipi_handler);
    if (result == KERN_FAILURE) {
      WIISYSLOG("Failed to register the CPU with XNU");
      return false;
    }
    processor_start(machProcessor);
  }

  registerService();

  WIISYSLOG("Initialized CPU %u", physCPU);
  return true;
}

//
// Writes a PowerPC trampoline at the secondary boot vector.
//
// The trampoline is placed at 0x08100100 (the Espresso secondary boot
// vector in Cafe mode).  When a secondary core is woken via the SCR
// wake bit, it begins executing at the reset vector (0xFFF00100) which
// is mapped to physical 0x08100100 in MEM0.
//
// The trampoline loads start_paddr into CTR and branches to it.
// This is the XNU secondary entry point provided by processor_start().
//
//   lis   r3, hi16(start_paddr)
//   ori   r3, r3, lo16(start_paddr)
//   mtsrr0 r3                    // SRR0 = entry physical address
//   li    r3, 0
//   mtsrr1 r3                    // SRR1 = 0 (MSR: IP=0, DR=0, IR=0, EE=0)
//   rfi                          // jump with translation off
//
// This mirrors linux-wiiu's wiiu_do_exi_bootstub / wiiu_kick_cpu: using rfi
// instead of mtctr+bctr is required so the secondary leaves the reset-vector
// state with MSR[IP] cleared, the MMU off, and EE masked. The remainder of
// the stub area is zeroed so that any prefetch past the rfi hits well-defined
// (trap-free) instructions rather than stale ancast bytes.
//
bool WiiCPU::writeBootTrampoline(vm_offset_t start_paddr) {
  volatile UInt32 *stub = (volatile UInt32 *)ml_io_map(
    kEspressoSecondaryBootStub, kEspressoSecondaryBootStubLength);

  if (stub == NULL) {
    WIISYSLOG("Failed to map secondary boot stub at 0x%08lx", (unsigned long)kEspressoSecondaryBootStub);
    return false;
  }

  UInt32 hi = (start_paddr >> 16) & 0xFFFF;
  UInt32 lo = start_paddr & 0xFFFF;

  stub[0] = 0x3C600000 | hi;     // lis    r3, hi16(start_paddr)
  stub[1] = 0x60630000 | lo;     // ori    r3, r3, lo16(start_paddr)
  stub[2] = 0x7C7A03A6;          // mtsrr0 r3
  stub[3] = 0x38600000;          // li     r3, 0
  stub[4] = 0x7C7B03A6;          // mtsrr1 r3
  stub[5] = 0x4C000064;          // rfi

  //
  // Zero the remainder of the stub window (stub size / 4 words total).
  //
  const UInt32 stubWords = kEspressoSecondaryBootStubLength / sizeof(UInt32);
  for (UInt32 i = 6; i < stubWords; i++) {
    stub[i] = 0;
  }

  //
  // ml_io_map() returns a cache-inhibited, guarded mapping on PPC, so the
  // four stores above land directly in memory with no D-cache involvement.
  // That makes explicit dcbf/icbi ops via this alias no-ops, but we still
  // need to order the stores against the wake in startCPU() and ensure any
  // stale I-cache content on the secondary (which has not executed yet)
  // is not an issue. eieio() orders the cache-inhibited stores; sync()
  // drains the store queue before startCPU() sets the SCR wake bit.
  //
  eieio();
  sync();

  WIISYSLOG("Boot trampoline written at 0x%08x -> 0x%08lx",
    kEspressoSecondaryBootStub, (unsigned long)start_paddr);
  return true;
}

//
// Initialize Espresso SPRs for cross-core cache coherency on the current core.
//
// From linux-wiiu: CAR and BCR must be set to enable cache coherency
// between cores, otherwise data accesses from secondaries will be stale.
//
void WiiCPU::initEspressoCoherency(void) {
  UInt32 hid5;

  //
  // Enable PIR and other features via HID5. We only OR in required bits so
  // we never stomp firmware-established state on either the boot CPU or a
  // secondary that was warm-reset.
  //
  hid5 = mfEspressoSPR(kEspressoSPR_HID5);
  hid5 |= kEspressoHID5_EnablePIR;
  mtEspressoSPR(kEspressoSPR_HID5, hid5);
  isync();

  //
  // Set cache/bus coherency attributes. On the boot CPU these are expected
  // to already be valid (the system has been running on it up to this
  // point). Writing known-good values on secondaries guarantees coherency
  // before they touch any shared memory. We skip the write on the boot CPU
  // to avoid perturbing live state if firmware chose different values.
  //
  if (!_isBootCPU) {
    mtEspressoSPR(kEspressoSPR_CAR, kEspressoCAR_Init);
    mtEspressoSPR(kEspressoSPR_BCR, kEspressoBCR_Init);
    //
    // A full sync is required before the secondary accesses shared memory.
    // isync alone only flushes the instruction pipeline; it does not drain
    // pending stores or flush cache state established under the old CAR.
    //
    sync();
    isync();
  }
}

//
// Send an ICI (Inter-Core Interrupt) to a target CPU.
//
// From linux-wiiu: Write the ICI bit for the target core in the SCR.
// Bit layout (CORRECTED from WiiUBrew):
//   Bit 20 = Core 0, Bit 19 = Core 1, Bit 18 = Core 2
//
void WiiCPU::sendICI(UInt32 targetCPU) {
  const UInt32 mask = espressoICIMaskForCore(targetCPU);
  UInt32 scr;

  //
  // linux-wiiu discovered that a single SCR write does not always latch the
  // ICI pending bit on Espresso; Cafe OS retries until the bit reads back as
  // set. Mirror that behaviour here.
  //
  do {
    scr = mfEspressoSPR(kEspressoSPR_SCR);
    if (scr & mask) {
      break;
    }
    mtEspressoSPR(kEspressoSPR_SCR, scr | mask);
  } while (1);
  isync();
}

//
// Acknowledge an ICI on the current core.
//
// The ICI pending bit for this core must be cleared in the SCR.
// The current core number is read from the Espresso PIR.
//
void WiiCPU::ackICI(void) {
  const UInt32 pir  = mfEspressoSPR(kEspressoSPR_PIR);
  const UInt32 mask = espressoICIMaskForCore(pir);
  UInt32 scr;

  //
  // Symmetric retry with sendICI: clear the ICI pending bit until the SCR
  // read-back confirms the bit is low. Matches linux-wiiu's ack loop.
  //
  do {
    scr = mfEspressoSPR(kEspressoSPR_SCR);
    if (!(scr & mask)) {
      break;
    }
    mtEspressoSPR(kEspressoSPR_SCR, scr & ~mask);
  } while (1);
  isync();
}

//
// Overrides IOCPU::initCPU()
//
void WiiCPU::initCPU(bool boot) {
  //
  // When SMP is disabled we must behave exactly like the pre-SMP single
  // core path: no Espresso coherency SPR writes, no CPU IC, no IPI
  // registration. Touching HID5/CAR/BCR on the boot CPU after firmware
  // has already configured it introduces the triple-core cache coherency
  // quirks documented by the homebrew community and has been observed to
  // stall peripheral MMIO / interrupt delivery (e.g. SDHC CMD0 hang).
  //
  if (!_smpEnabled) {
    setCPUState(kIOCPUStateRunning);
    return;
  }

  if (boot) {
    //
    // Boot CPU: Initialize Espresso coherency SPRs if on Cafe.
    //
    if (_isCafe) {
      initEspressoCoherency();
    }

    gCPUIC->enableCPUInterrupt(this);

    //
    // Register and enable IPIs.
    //
	  cpuNub->registerInterrupt(0, this,
#if __MAC_OS_X_VERSION_MIN_REQUIRED >= __MAC_10_2
      OSMemberFunctionCast(IOInterruptAction, this, &WiiCPU::ipiHandler),
#else
      (IOInterruptAction) &WiiCPU::ipiHandler,
#endif
      0);
    cpuNub->enableInterrupt(0);
  }
  else
  {
    //
    // Secondary CPU initialization.
    // This runs on the secondary core after XNU has started it.
    //

    //
    // Initialize Espresso SPRs for cache coherency.
    //
    if (_isCafe) {
      initEspressoCoherency();
    }

    //
    // Enable this CPU's interrupt handling and register its IPI.
    //
    gCPUIC->enableCPUInterrupt(this);

    cpuNub->registerInterrupt(0, this,
#if __MAC_OS_X_VERSION_MIN_REQUIRED >= __MAC_10_2
      OSMemberFunctionCast(IOInterruptAction, this, &WiiCPU::ipiHandler),
#else
      (IOInterruptAction) &WiiCPU::ipiHandler,
#endif
      0);
    cpuNub->enableInterrupt(0);
  }

  //
  // CPU is now running.
  //
  setCPUState(kIOCPUStateRunning);
}

//
// Overrides IOCPU::quiesceCPU()
//
void WiiCPU::quiesceCPU(void) {
  //
  // Non-boot CPUs reach here on their own shutdown path after XNU has
  // already migrated work away and transitioned them offline.
  // We only report Stopped once the target CPU is actually quiescing.
  //
  setCPUState(kIOCPUStateStopped);
}

//
// Overrides IOCPU::startCPU()
//
// This is called by XNU on the boot CPU to start a secondary CPU.
// start_paddr is the physical address the secondary should jump to.
//
kern_return_t WiiCPU::startCPU(vm_offset_t start_paddr, vm_offset_t arg_paddr) {
  UInt32 physCPU;
  UInt32 scr;

  (void)arg_paddr;

  if (!_isCafe) {
    WIISYSLOG("startCPU called on non-Cafe platform");
    return KERN_FAILURE;
  }

  physCPU = getCPUNumber();
  WIISYSLOG("startCPU: starting CPU %u, start_paddr=0x%08lx", physCPU, (unsigned long)start_paddr);

  //
  // Step 1: Write the boot trampoline at the secondary boot vector.
  // The trampoline loads start_paddr and branches to it.
  //
  if (!writeBootTrampoline(start_paddr)) {
    WIISYSLOG("Failed to install boot trampoline for CPU %u", physCPU);
    return KERN_FAILURE;
  }

  //
  // Step 2: Wake the target core by setting its wake bit in the SCR.
  // From linux-wiiu: bit (23 - coreNum) wakes the core.
  //
  scr = mfEspressoSPR(kEspressoSPR_SCR);
  scr |= espressoWakeMaskForCore(physCPU);
  mtEspressoSPR(kEspressoSPR_SCR, scr);
  isync();

  WIISYSLOG("Sent wake signal to CPU %u (SCR=0x%08x)", physCPU, scr);

  //
  // Step 3: Wait for the secondary to come up.
  // XNU expects startCPU to return KERN_SUCCESS once the core is awake.
  // The secondary will call initCPU(false) and set its state to running.
  //
  // Give the secondary a reasonable time to start.
  //
  for (int i = 0; i < 1000; i++) {
    //
    // Force a coherent read of the CPU state written by the secondary
    // from its own initCPU(false) path. eieio() / isync() ensure we do
    // not observe a stale cached copy on the boot CPU.
    //
    eieio();
    isync();
    if (getCPUState() == kIOCPUStateRunning) {
      WIISYSLOG("CPU %u is now running (after %d ms)", physCPU, i);
      return KERN_SUCCESS;
    }
    IODelay(1000); // 1ms
  }

  WIISYSLOG("Timeout waiting for CPU %u to start", physCPU);
  return KERN_FAILURE;
}

//
// Overrides IOCPU::haltCPU()
//
void WiiCPU::haltCPU(void) {
  //
  // XNU drives non-boot CPUs into their target-side shutdown path with
  // processor_sleep(), which eventually calls quiesceCPU() on the target.
  // Do not mark them Stopped here while they are still executing.
  //
  // The boot CPU never reaches processor_sleep() on the generic IOCPU path,
  // but this platform also prevents sleep entirely. If a sleep attempt still
  // reaches us, bracket it with the generic platform quiesce/active actions
  // instead of lying about the CPU state.
  //
  if (_isBootCPU) {
    WIISYSLOG("haltCPU called on boot CPU, but Wii sleep is not supported");
    IOCPURunPlatformQuiesceActions();
    IOCPURunPlatformActiveActions();
  }
}

//
// Overrides IOCPU::signalCPU()
//
void WiiCPU::signalCPU(IOCPU *target) {
  if (!_isCafe || !_smpEnabled || target == NULL) {
    return;
  }

  sendICI(target->getCPUNumber());
}

//
// Overrides IOCPU::signalCPUDeferred()
//
void WiiCPU::signalCPUDeferred(IOCPU *target) {
  signalCPU(target);
}

//
// Overrides IOCPU::signalCPUCancel()
//
void WiiCPU::signalCPUCancel(IOCPU *target) {
  (void)target;

  // Deferred IPIs use the same transport as regular ICIs here, so there is
  // no safe way to retract only deferred wakeups once issued.
}

//
// Overrides IOCPU::getCPUName()
//
const OSSymbol *WiiCPU::getCPUName(void) {
  char tmpStr[256];
  snprintf(tmpStr, sizeof (tmpStr), "Primary%lu", getCPUNumber());
  return OSSymbol::withCString(tmpStr);
}

void WiiCPU::ipiHandler(void *refCon, void *nub, int source) {
  //
  // Only touch the SCR ICI bit when SMP is actually enabled. With SMP
  // off we never send ICIs, so any spurious call here must not poke the
  // shared SCR register (which can perturb other cores).
  //
  if (_isCafe && _smpEnabled) {
    ackICI();
  }

  //
  // Call the IPI handler for this CPU.
  //
  if (ipi_handler != NULL) {
    ipi_handler();
  }
}
