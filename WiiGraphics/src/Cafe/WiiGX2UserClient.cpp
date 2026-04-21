//
//  WiiGX2UserClient.cpp
//  Userspace command-submission interface for the Wii U GX2 engine.
//
//  Copyright © 2026 John Davis. All rights reserved.
//

#include "WiiGX2UserClient.hpp"
#include "WiiGX2.hpp"
#include "WiiCafeFB.hpp"

OSDefineMetaClassAndStructors(WiiGX2UserClient, IOUserClient);

// ========================================================================
// IOExternalMethod dispatch table.
//
// This is the pre-10.6 pattern: an array of IOExternalMethod entries
// indexed by selector, queried once per method invocation via
// ::getTargetAndMethodForIndex. The method pointers are member functions;
// they are cast through IOMethod on 10.4/10.5 PowerPC, which is safe as
// long as the class has single inheritance from IOService (true here).
// ========================================================================

#define kWiiGX2UCScalar kIOUCScalarIScalarO

static const IOExternalMethod gWiiGX2UserClientMethods[kWiiGX2UCSelectorCount] = {
  // 0: GetInfo        0 in,  5 out
  { NULL, (IOMethod) &WiiGX2UserClient::methodGetInfo,
    kWiiGX2UCScalar, 0, 5 },
  // 1: AllocBuffer    2 in,  3 out
  { NULL, (IOMethod) &WiiGX2UserClient::methodAllocBuffer,
    kWiiGX2UCScalar, 2, 3 },
  // 2: FreeBuffer     1 in,  0 out
  { NULL, (IOMethod) &WiiGX2UserClient::methodFreeBuffer,
    kWiiGX2UCScalar, 1, 0 },
  // 3: SubmitIB       3 in,  1 out
  { NULL, (IOMethod) &WiiGX2UserClient::methodSubmitIB,
    kWiiGX2UCScalar, 3, 1 },
  // 4: WaitFence      2 in,  1 out
  { NULL, (IOMethod) &WiiGX2UserClient::methodWaitFence,
    kWiiGX2UCScalar, 2, 1 },
  // 5: ReadFence      0 in,  1 out
  { NULL, (IOMethod) &WiiGX2UserClient::methodReadFence,
    kWiiGX2UCScalar, 0, 1 },
};

// ========================================================================
// Lifecycle.
// ========================================================================

bool WiiGX2UserClient::initWithTask(task_t owningTask, void *securityID,
                                    UInt32 type) {
  // Require non-NULL owning task so malicious KEXT callers cannot open
  // the interface from the kernel; the framework passes NULL for kernel-
  // side clients.
  if (owningTask == NULL) {
    return false;
  }
  if (!super::initWithTask(owningTask, securityID, type)) {
    return false;
  }

  WiiCheckDebugArgs();

  _owner        = NULL;
  _gx2          = NULL;
  _task         = owningTask;
  _open         = false;
  _buffersLock  = NULL;
  bzero(_buffers, sizeof (_buffers));

  return true;
}

bool WiiGX2UserClient::start(IOService *provider) {
  WiiCafeFB *owner = OSDynamicCast(WiiCafeFB, provider);

  if (owner == NULL) {
    WIISYSLOG("start: provider is not WiiCafeFB");
    return false;
  }
  if (!super::start(provider)) {
    return false;
  }

  _owner = owner;
  _gx2   = owner->getGX2();
  if (_gx2 == NULL) {
    WIISYSLOG("start: WiiCafeFB has no GX2 engine");
    return false;
  }

  // Fence page must exist before any IB submission. Idempotent on repeat
  // client opens.
  if (!_gx2->isUserFenceReady()) {
    if (!_gx2->setupUserFence()) {
      WIISYSLOG("start: failed to set up user fence page");
      return false;
    }
  }

  _buffersLock = IOLockAlloc();
  if (_buffersLock == NULL) {
    WIISYSLOG("start: IOLockAlloc failed");
    return false;
  }

  _open = true;
  WIIDBGLOG("start: user client ready (task=%p)", _task);
  return true;
}

void WiiGX2UserClient::stop(IOService *provider) {
  _open = false;
  super::stop(provider);
}

void WiiGX2UserClient::free(void) {
  freeAllBuffers();
  if (_buffersLock != NULL) {
    IOLockFree(_buffersLock);
    _buffersLock = NULL;
  }
  super::free();
}

IOReturn WiiGX2UserClient::clientClose(void) {
  _open = false;
  if (!isInactive()) {
    terminate();
  }
  return kIOReturnSuccess;
}

IOReturn WiiGX2UserClient::clientDied(void) {
  return clientClose();
}

void WiiGX2UserClient::freeAllBuffers(void) {
  UInt32 i;

  if (_buffersLock == NULL) {
    return;
  }
  IOLockLock(_buffersLock);
  for (i = 0; i < kWiiGX2UCMaxBuffers; i++) {
    if (_buffers[i].desc != NULL) {
      _buffers[i].desc->complete();
      _buffers[i].desc->release();
      _buffers[i].desc = NULL;
      _buffers[i].phys = 0;
      _buffers[i].size = 0;
    }
  }
  IOLockUnlock(_buffersLock);
}

// ========================================================================
// Dispatch and mmap.
// ========================================================================

IOExternalMethod *
WiiGX2UserClient::getTargetAndMethodForIndex(IOService **target, UInt32 index) {
  if (index >= kWiiGX2UCSelectorCount) {
    return NULL;
  }
  if (target == NULL) {
    return NULL;
  }
  if (!_open || isInactive()) {
    return NULL;
  }
  *target = this;
  return (IOExternalMethod *) &gWiiGX2UserClientMethods[index];
}

IOReturn
WiiGX2UserClient::clientMemoryForType(UInt32 type, IOOptionBits *options,
                                      IOMemoryDescriptor **memory) {
  IOBufferMemoryDescriptor *desc = NULL;
  UInt32 handle;

  if (memory == NULL || options == NULL) {
    return kIOReturnBadArgument;
  }
  *memory  = NULL;
  *options = 0;

  if (!_open || _gx2 == NULL) {
    return kIOReturnNotOpen;
  }

  if (type == kWiiGX2UCMemoryUserFence) {
    desc = _gx2->getUserFenceDescriptor();
    if (desc == NULL) {
      return kIOReturnNoResources;
    }
    // Fence page is shared across clients — read-only mapping keeps user
    // code from clobbering it.
    *options = kIOMapReadOnly;
    desc->retain();
    *memory = desc;
    return kIOReturnSuccess;
  }

  if (type >= kWiiGX2UCMemoryBufferBase) {
    handle = type - kWiiGX2UCMemoryBufferBase;
    if (handle >= kWiiGX2UCMaxBuffers) {
      return kIOReturnBadArgument;
    }
    IOLockLock(_buffersLock);
    desc = _buffers[handle].desc;
    if (desc != NULL) {
      desc->retain();
    }
    IOLockUnlock(_buffersLock);
    if (desc == NULL) {
      return kIOReturnNotFound;
    }
    *memory = desc;
    return kIOReturnSuccess;
  }

  return kIOReturnUnsupported;
}

// ========================================================================
// Method implementations.
// ========================================================================

IOReturn
WiiGX2UserClient::methodGetInfo(UInt32 *chipRev, UInt32 *mem1Size,
                                UInt32 *mem1Used, UInt32 *mem2Live,
                                UInt32 *flags) {
  if (chipRev == NULL || mem1Size == NULL || mem1Used == NULL ||
      mem2Live == NULL || flags == NULL) {
    return kIOReturnBadArgument;
  }
  if (_gx2 == NULL) {
    return kIOReturnNotReady;
  }

  *chipRev  = _gx2->getChipRevision();
  *mem1Size = _gx2->getMEM1Size();
  *mem1Used = _gx2->getMEM1Used();
  *mem2Live = _gx2->getMEM2LiveBytes();

  UInt32 f = 0;
  if (_gx2->isCPRunning())         { f |= (1u << 0); }
  if (_gx2->isInterruptRingReady()){ f |= (1u << 1); }
  if (_gx2->isDMARingReady())      { f |= (1u << 2); }
  if (_gx2->isMEM1HeapReady())     { f |= (1u << 3); }
  if (_gx2->isUserFenceReady())    { f |= (1u << 4); }
  *flags = f;
  return kIOReturnSuccess;
}

IOReturn
WiiGX2UserClient::methodAllocBuffer(UInt32 sizeBytes, UInt32 usage,
                                    UInt32 *outHandle, UInt32 *outPhysLo,
                                    UInt32 *outPhysHi) {
  IOBufferMemoryDescriptor *desc;
  IOPhysicalAddress         phys;
  IOByteCount               segLen;
  UInt32                    rounded;
  UInt32                    slot;
  IOReturn                  status;

  (void) usage;   // reserved for future flags (e.g. EDRAM placement hint)

  if (outHandle == NULL || outPhysLo == NULL || outPhysHi == NULL) {
    return kIOReturnBadArgument;
  }
  *outHandle = 0;
  *outPhysLo = 0;
  *outPhysHi = 0;

  if (sizeBytes == 0 || sizeBytes > kWiiGX2UCMaxBufferBytes) {
    return kIOReturnBadArgument;
  }

  rounded = (sizeBytes + page_size - 1) & ~(page_size - 1);

  desc = IOBufferMemoryDescriptor::withOptions(
    kIOMemoryPhysicallyContiguous | kIODirectionInOut,
    rounded, page_size);
  if (desc == NULL) {
    return kIOReturnNoMemory;
  }
  status = desc->prepare();
  if (status != kIOReturnSuccess) {
    desc->release();
    return status;
  }

  segLen = 0;
  phys = desc->getPhysicalSegment(0, &segLen);
  if (phys == 0 || segLen < rounded) {
    desc->complete();
    desc->release();
    return kIOReturnNoResources;
  }
  bzero(desc->getBytesNoCopy(), rounded);

  IOLockLock(_buffersLock);
  for (slot = 0; slot < kWiiGX2UCMaxBuffers; slot++) {
    if (_buffers[slot].desc == NULL) {
      _buffers[slot].desc = desc;
      _buffers[slot].phys = phys;
      _buffers[slot].size = rounded;
      break;
    }
  }
  IOLockUnlock(_buffersLock);

  if (slot >= kWiiGX2UCMaxBuffers) {
    desc->complete();
    desc->release();
    return kIOReturnNoResources;
  }

  *outHandle = slot;
  *outPhysLo = (UInt32) ((UInt64) phys & 0xFFFFFFFFULL);
  *outPhysHi = (UInt32) (((UInt64) phys >> 32) & 0xFFFFFFFFULL);
  return kIOReturnSuccess;
}

IOReturn
WiiGX2UserClient::methodFreeBuffer(UInt32 handle) {
  IOBufferMemoryDescriptor *desc = NULL;

  if (handle >= kWiiGX2UCMaxBuffers) {
    return kIOReturnBadArgument;
  }

  IOLockLock(_buffersLock);
  desc = _buffers[handle].desc;
  _buffers[handle].desc = NULL;
  _buffers[handle].phys = 0;
  _buffers[handle].size = 0;
  IOLockUnlock(_buffersLock);

  if (desc == NULL) {
    return kIOReturnNotFound;
  }
  desc->complete();
  desc->release();
  return kIOReturnSuccess;
}

IOReturn
WiiGX2UserClient::methodSubmitIB(UInt32 handle, UInt32 dwordOffset,
                                 UInt32 dwordCount, UInt32 *outFenceSeq) {
  IOPhysicalAddress ibPhys;
  IOByteCount       size;
  UInt32            seq = 0;
  UInt64            lastByte;

  if (outFenceSeq == NULL) {
    return kIOReturnBadArgument;
  }
  *outFenceSeq = 0;

  if (handle >= kWiiGX2UCMaxBuffers) {
    return kIOReturnBadArgument;
  }
  if (_gx2 == NULL || !_gx2->isCPRunning()) {
    return kIOReturnNotReady;
  }
  if (dwordCount == 0 || dwordCount > 0x000FFFFFu) {
    return kIOReturnBadArgument;
  }

  IOLockLock(_buffersLock);
  ibPhys = _buffers[handle].phys;
  size   = _buffers[handle].size;
  IOLockUnlock(_buffersLock);

  if (ibPhys == 0 || size == 0) {
    return kIOReturnNotFound;
  }

  // Bounds check the submitted range against the allocated buffer.
  lastByte = ((UInt64) dwordOffset + (UInt64) dwordCount) * 4ULL;
  if (lastByte > (UInt64) size) {
    return kIOReturnBadArgument;
  }

  ibPhys += (IOPhysicalAddress) (dwordOffset * 4u);
  if ((ibPhys & 0x3u) != 0) {
    return kIOReturnBadArgument;
  }

  if (!_gx2->submitIndirectBuffer(ibPhys, dwordCount, &seq)) {
    return kIOReturnIOError;
  }
  *outFenceSeq = seq;
  return kIOReturnSuccess;
}

IOReturn
WiiGX2UserClient::methodWaitFence(UInt32 seq, UInt32 timeoutMs,
                                  UInt32 *outReached) {
  UInt32 timeoutUS;

  if (outReached == NULL) {
    return kIOReturnBadArgument;
  }
  *outReached = 0;

  if (_gx2 == NULL) {
    return kIOReturnNotReady;
  }

  // Cap the kernel-side blocking window so a buggy user client cannot
  // stall the thread forever. 2 seconds is plenty for any single IB.
  if (timeoutMs > 2000u) {
    timeoutMs = 2000u;
  }
  timeoutUS = timeoutMs * 1000u;

  *outReached = _gx2->waitUserFence(seq, timeoutUS) ? 1u : 0u;
  return kIOReturnSuccess;
}

IOReturn
WiiGX2UserClient::methodReadFence(UInt32 *outSeq) {
  if (outSeq == NULL) {
    return kIOReturnBadArgument;
  }
  if (_gx2 == NULL) {
    return kIOReturnNotReady;
  }
  *outSeq = _gx2->readUserFence();
  return kIOReturnSuccess;
}
