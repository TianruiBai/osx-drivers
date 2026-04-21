//
//  WiiGX2UserClient.hpp
//  Userspace command-submission interface for the Wii U GX2 engine.
//
//  Targets Mac OS X 10.4 / 10.5 PowerPC. Uses only the pre-10.6 IOUserClient
//  surface:
//    - IOExternalMethod dispatch table via ::getTargetAndMethodForIndex
//    - ::clientMemoryForType for mmap-style buffer sharing
//    - ::clientClose / ::clientDied for teardown
//  No IOExternalMethodDispatch, no IOCommandGate async, no blocks.
//
//  Copyright © 2026 John Davis. All rights reserved.
//

#ifndef WiiGX2UserClient_hpp
#define WiiGX2UserClient_hpp

#include <IOKit/IOUserClient.h>
#include <IOKit/IOBufferMemoryDescriptor.h>

#include "WiiCommon.hpp"

class WiiCafeFB;
class WiiGX2;

//
// Selector indices for ::getTargetAndMethodForIndex. Must match the
// userspace header (shipped separately).
//
enum WiiGX2UserClientSelector {
  kWiiGX2UCSelectorGetInfo     = 0,
  kWiiGX2UCSelectorAllocBuffer = 1,
  kWiiGX2UCSelectorFreeBuffer  = 2,
  kWiiGX2UCSelectorSubmitIB    = 3,
  kWiiGX2UCSelectorWaitFence   = 4,
  kWiiGX2UCSelectorReadFence   = 5,
  kWiiGX2UCSelectorCount       = 6,
};

//
// clientMemoryForType() type codes:
//   type 0                             -> shared user fence page (read-only)
//   type (kWiiGX2UCMemoryBufferBase+h) -> allocated buffer with handle h
//
#define kWiiGX2UCMemoryUserFence        0u
#define kWiiGX2UCMemoryBufferBase       0x00010000u

// Explicit IOServiceOpen() type for the custom GX2 submission client.
// Type 0 is reserved for the standard IOFramebuffer user client path that
// WindowServer and IOGraphics expect, so the GX2 interface must opt in via
// a non-zero type.
#define kWiiGX2UCType                   0x47583255u

#define kWiiGX2UCMaxBuffers             32u
#define kWiiGX2UCMaxBufferBytes         (16u * 1024u * 1024u)

class WiiGX2UserClient : public IOUserClient {
  OSDeclareDefaultStructors(WiiGX2UserClient);
  WiiDeclareLogFunctions("gx2uc");
  typedef IOUserClient super;

private:
  WiiCafeFB               *_owner;
  WiiGX2                  *_gx2;
  task_t                   _task;
  bool                     _open;

  struct BufferSlot {
    IOBufferMemoryDescriptor *desc;
    IOPhysicalAddress         phys;
    IOByteCount               size;
  };
  BufferSlot               _buffers[kWiiGX2UCMaxBuffers];
  IOLock                  *_buffersLock;

  void freeAllBuffers(void);

public:

  // IOExternalMethod targets. Signatures per IOKit's scalar convention:
  //   scalarIScalarO variants take up to 6 UInt32 in/out via pointers.
  IOReturn methodGetInfo(UInt32 *chipRev, UInt32 *mem1Size,
                         UInt32 *mem1Used, UInt32 *mem2Live,
                         UInt32 *flags);
  IOReturn methodAllocBuffer(UInt32 sizeBytes, UInt32 usage,
                             UInt32 *outHandle, UInt32 *outPhysLo,
                             UInt32 *outPhysHi);
  IOReturn methodFreeBuffer(UInt32 handle);
  IOReturn methodSubmitIB(UInt32 handle, UInt32 dwordOffset,
                          UInt32 dwordCount, UInt32 *outFenceSeq);
  IOReturn methodWaitFence(UInt32 seq, UInt32 timeoutMs,
                           UInt32 *outReached);
  IOReturn methodReadFence(UInt32 *outSeq);

  // 10.4-era overrides.
  virtual bool     initWithTask(task_t owningTask, void *securityID,
                                UInt32 type);
  virtual bool     start(IOService *provider);
  virtual void     stop(IOService *provider);
  virtual void     free(void);
  virtual IOReturn clientClose(void);
  virtual IOReturn clientDied(void);
  virtual IOReturn clientMemoryForType(UInt32 type, IOOptionBits *options,
                                       IOMemoryDescriptor **memory);
  virtual IOExternalMethod *getTargetAndMethodForIndex(IOService **target,
                                                       UInt32 index);
};

#endif // WiiGX2UserClient_hpp
