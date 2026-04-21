//
//  WiiEHCI.hpp
//  Wii U EHCI USB controller scaffold
//
//  Copyright © 2025 John Davis. All rights reserved.
//

#ifndef WiiEHCI_hpp
#define WiiEHCI_hpp

#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOLocks.h>
#include <IOKit/usb/IOUSBControllerV2.h>

#include "WiiCommon.hpp"
#include "EHCIRegs.hpp"

#define kWiiEHCI0Location    "d040000"

class WiiEHCI : public IOUSBControllerV2 {
  OSDeclareDefaultStructors(WiiEHCI);
  WiiDeclareLogFunctions("ehci");
  typedef IOUSBControllerV2 super;

private:
  struct EHCITransferDescriptor {
    UInt32 nextQTD;
    UInt32 altNextQTD;
    UInt32 token;
    UInt32 buffer[5];
  } __attribute__((aligned(32)));

  struct EHCIQueueHead {
    UInt32 nextQH;
    UInt32 info1;
    UInt32 info2;
    UInt32 currentQTD;
    UInt32 overlayNextQTD;
    UInt32 overlayAltNextQTD;
    UInt32 overlayToken;
    UInt32 overlayBuffer[5];
    UInt32 reserved[4];
  } __attribute__((aligned(32)));

  struct WiiEHCIAsyncTransaction;

  struct WiiEHCIAsyncTransfer {
    EHCITransferDescriptor   *qtd;
    IOPhysicalAddress        physAddr;
    WiiEHCIAsyncTransfer     *nextFree;
    WiiEHCIAsyncTransfer     *nextTransfer;
    WiiEHCIAsyncTransaction  *transaction;
    UInt32                   transferLength;
    UInt8                    pid;
  };

  struct WiiEHCIAsyncTransaction {
    IOUSBCompletion          completion;
    IOMemoryDescriptor       *sourceBuffer;
    IOBufferMemoryDescriptor *bounceBufferDescriptor;
    void                     *bounceBuffer;
    IOPhysicalAddress        bounceBufferPhysAddr;
    UInt32                   totalLength;
    WiiEHCIAsyncTransfer     *firstTransfer;
    WiiEHCIAsyncTransfer     *lastTransfer;
    WiiEHCIAsyncTransaction  *nextTransaction;
    short                    direction;
  };

  struct WiiEHCIEndpointState {
    EHCIQueueHead            *queueHead;
    IOPhysicalAddress        qhPhysAddr;
    WiiEHCIAsyncTransfer     *dummyTransfer;
    WiiEHCIAsyncTransaction  *transactionHead;
    WiiEHCIAsyncTransaction  *transactionTail;
    WiiEHCIEndpointState     *nextEndpoint;
    UInt8                    functionNumber;
    UInt8                    endpointNumber;
    UInt8                    direction;
    UInt8                    speed;
    UInt16                   maxPacketSize;
    bool                     isControl;
  };

  IOMemoryMap   *_memoryMap;
  IOInterruptEventSource *_interruptEventSource;
  volatile void *_baseAddr;
  UInt32        _hcsParams;
  UInt8         _capLength;
  UInt8         _numPorts;
  UInt16        _rootHubAddress;
  UInt16        _portResetChangeBitmap;
  UInt16        _portResetActiveBitmap;
  WiiInvalidateDataCacheFunc _invalidateCacheFunc;

  IOBufferMemoryDescriptor *_asyncQueueHeadDescriptor;
  IOBufferMemoryDescriptor *_asyncTransferDescriptor;
  EHCIQueueHead            *_asyncQueueHeads;
  EHCITransferDescriptor   *_asyncTransfers;
  EHCIQueueHead            *_asyncHeadQH;
  IOPhysicalAddress        _asyncHeadQHPhysAddr;
  WiiEHCIEndpointState     *_asyncEndpointStates;
  WiiEHCIEndpointState     *_freeEndpointStateHead;
  WiiEHCIEndpointState     *_endpointStateHead;
  WiiEHCIAsyncTransfer     *_asyncTransferStates;
  WiiEHCIAsyncTransfer     *_freeAsyncTransferHead;

  struct WiiEHCIRootHubIntTransaction {
    IOMemoryDescriptor  *buffer;
    UInt32              bufferLength;
    IOUSBCompletion     completion;
  } _rootHubInterruptTransactions[4];
  IOLock *_rootHubInterruptTransLock;

  inline UInt32 readReg32(UInt32 offset) {
    return OSReadBigInt32(_baseAddr, offset);
  }
  inline void writeReg32(UInt32 offset, UInt32 value) {
    OSWriteBigInt32(_baseAddr, offset, value);
  }
  inline UInt32 readOpReg32(UInt32 offset) {
    return readReg32(_capLength + offset);
  }
  inline void writeOpReg32(UInt32 offset, UInt32 value) {
    writeReg32(_capLength + offset, value);
  }
  inline UInt32 readPortReg32(UInt16 port) {
    return readOpReg32(kEHCIRegPortStatusControlBase + ((port - 1) * sizeof (UInt32)));
  }
  inline void writePortReg32(UInt16 port, UInt32 value) {
    writeOpReg32(kEHCIRegPortStatusControlBase + ((port - 1) * sizeof (UInt32)), value);
  }
  inline UInt32 hostToDescriptor(UInt32 value) const {
    return HostToUSBLong(value);
  }
  inline UInt32 descriptorToHost(UInt32 value) const {
    return USBToHostLong(value);
  }

  bool waitForUSBCmdBit(UInt32 mask, bool set, UInt32 retries, UInt32 delayUS);
  bool waitForUSBStsBit(UInt32 mask, bool set, UInt32 retries, UInt32 delayUS);
  bool waitForPortBit(UInt16 port, UInt32 mask, bool set, UInt32 retries, UInt32 delayUS);
  IOReturn allocateAsyncStructures(void);
  void freeAsyncStructures(void);
  void initializeQueueHead(EHCIQueueHead *queueHead);
  void initializeTransferDescriptor(WiiEHCIAsyncTransfer *transfer, bool terminate);
  IOReturn setAsyncScheduleEnabled(bool enabled);
  IOReturn rebuildAsyncQueue(WiiEHCIEndpointState *endpoint);
  WiiEHCIEndpointState *allocateEndpointState(void);
  void releaseEndpointState(WiiEHCIEndpointState *endpoint);
  WiiEHCIAsyncTransfer *allocateAsyncTransfer(void);
  void releaseAsyncTransfer(WiiEHCIAsyncTransfer *transfer);
  WiiEHCIEndpointState *findEndpointState(UInt8 functionNumber, UInt8 endpointNumber, UInt8 direction);
  IOReturn createAsyncEndpoint(UInt8 functionNumber, UInt8 endpointNumber, UInt8 direction, UInt8 speed,
                               UInt16 maxPacketSize, bool isControl, USBDeviceAddress highSpeedHub, int highSpeedPort);
  IOReturn allocateBounceBuffer(UInt32 length, IOBufferMemoryDescriptor **descriptor, void **buffer, IOPhysicalAddress *physAddr);
  IOReturn submitAsyncTransfer(UInt8 functionNumber, UInt8 endpointNumber, UInt8 direction, IOUSBCompletion completion,
                               IOMemoryDescriptor *buffer, UInt32 bufferSize, bool bufferRounding, bool controlTransfer);
  IOReturn convertAsyncTokenStatus(UInt32 token) const;
  bool evaluateAsyncTransaction(WiiEHCIEndpointState *endpoint, WiiEHCIAsyncTransaction *transaction,
                                IOReturn *status, UInt32 *bufferRemainder, bool *repairQueue);
  void releaseAsyncTransaction(WiiEHCIAsyncTransaction *transaction);
  void abortEndpointTransactions(WiiEHCIEndpointState *endpoint, IOReturn status);
  void completeAsyncTransactions(void);
  void enableLatteInterruptNotification(void);
  UInt16 getPortBit(UInt16 port) const;
  void routePortsToEHCI(void);
  void powerOnPorts(void);
  void handPortToCompanion(UInt16 port, UInt32 portStatus);
  void updatePortChangeBits(void);
  bool hasPendingPortChange(UInt32 usbSts);
  IOReturn resetRootHubPort(UInt16 port);
  void handleInterrupt(IOInterruptEventSource *intEventSource, int count);
  void acknowledgeInterruptStatus(UInt32 usbSts);
  void writePortFeature(UInt16 port, UInt32 setBits, UInt32 clearBits, UInt32 clearChangeBits);
  IOReturn simulateRootHubControlEDCreate(UInt8 endpointNumber, UInt16 maxPacketSize, UInt8 speed);
  IOReturn simulateRootHubInterruptEDCreate(short endpointNumber, UInt8 direction, short speed, UInt16 maxPacketSize);
  void simulateRootHubInterruptTransfer(short endpointNumber, IOUSBCompletion completion,
                                        IOMemoryDescriptor *CBP, bool bufferRounding, UInt32 bufferSize, short direction);
  void completeRootHubInterruptTransfer(bool abort);

protected:
  IOReturn UIMInitialize(IOService *provider);
  IOReturn UIMFinalize(void);

  IOReturn UIMCreateControlEndpoint(UInt8 functionNumber, UInt8 endpointNumber, UInt16 maxPacketSize, UInt8 speed);
  IOReturn UIMCreateControlEndpoint(UInt8 functionNumber, UInt8 endpointNumber, UInt16 maxPacketSize, UInt8 speed,
                                    USBDeviceAddress highSpeedHub, int highSpeedPort);
  IOReturn UIMCreateControlTransfer(short functionNumber, short endpointNumber, IOUSBCompletion completion, void *CBP,
                                    bool bufferRounding, UInt32 bufferSize, short direction);
  IOReturn UIMCreateControlTransfer(short functionNumber, short endpointNumber, IOUSBCompletion completion,
                                    IOMemoryDescriptor *CBP, bool bufferRounding, UInt32 bufferSize, short direction);

  IOReturn UIMCreateBulkEndpoint(UInt8 functionNumber, UInt8 endpointNumber, UInt8 direction, UInt8 speed, UInt8 maxPacketSize);
  IOReturn UIMCreateBulkEndpoint(UInt8 functionNumber, UInt8 endpointNumber, UInt8 direction, UInt8 speed,
                                 UInt16 maxPacketSize, USBDeviceAddress highSpeedHub, int highSpeedPort);
  IOReturn UIMCreateBulkTransfer(short functionNumber, short endpointNumber, IOUSBCompletion completion,
                                 IOMemoryDescriptor *CBP, bool bufferRounding, UInt32 bufferSize, short direction);

  IOReturn UIMCreateInterruptEndpoint(short functionAddress, short endpointNumber, UInt8 direction,
                                      short speed, UInt16 maxPacketSize, short pollingRate);
  IOReturn UIMCreateInterruptEndpoint(short functionAddress, short endpointNumber, UInt8 direction,
                                      short speed, UInt16 maxPacketSize, short pollingRate,
                                      USBDeviceAddress highSpeedHub, int highSpeedPort);
  IOReturn UIMCreateInterruptTransfer(short functionNumber, short endpointNumber, IOUSBCompletion completion,
                                      IOMemoryDescriptor *CBP, bool bufferRounding, UInt32 bufferSize, short direction);

  IOReturn UIMCreateIsochEndpoint(short functionAddress, short endpointNumber, UInt32 maxPacketSize, UInt8 direction);
  IOReturn UIMCreateIsochEndpoint(short functionAddress, short endpointNumber, UInt32 maxPacketSize, UInt8 direction,
                                  USBDeviceAddress highSpeedHub, int highSpeedPort);
  IOReturn UIMCreateIsochTransfer(short functionAddress, short endpointNumber, IOUSBIsocCompletion completion, UInt8 direction,
                                  UInt64 frameStart, IOMemoryDescriptor *pBuffer, UInt32 frameCount, IOUSBIsocFrame *pFrames);

  IOReturn UIMAbortEndpoint(short functionNumber, short endpointNumber, short direction);
  IOReturn UIMDeleteEndpoint(short functionNumber, short endpointNumber, short direction);
  IOReturn UIMClearEndpointStall(short functionNumber, short endpointNumber, short direction);
  void UIMRootHubStatusChange(void);
  void UIMRootHubStatusChange(bool abort);

public:
  bool init(OSDictionary *dictionary = 0);
  void free(void);
  IOService *probe(IOService *provider, SInt32 *score);

  IOReturn GetRootHubDeviceDescriptor(IOUSBDeviceDescriptor *desc);
  IOReturn GetRootHubDescriptor(IOUSBHubDescriptor *desc);
  IOReturn SetRootHubDescriptor(OSData *buffer);
  IOReturn GetRootHubConfDescriptor(OSData *desc);
  IOReturn GetRootHubStatus(IOUSBHubStatus *status);
  IOReturn SetRootHubFeature(UInt16 wValue);
  IOReturn ClearRootHubFeature(UInt16 wValue);
  IOReturn GetRootHubPortStatus(IOUSBHubPortStatus *status, UInt16 port);
  IOReturn SetRootHubPortFeature(UInt16 wValue, UInt16 port);
  IOReturn ClearRootHubPortFeature(UInt16 wValue, UInt16 port);
  IOReturn GetRootHubPortState(UInt8 *state, UInt16 port);
  IOReturn SetHubAddress(UInt16 wValue);
  UInt32 GetBandwidthAvailable(void);
  UInt64 GetFrameNumber(void);
  UInt32 GetFrameNumber32(void);
  void PollInterrupts(IOUSBCompletionAction safeAction = 0);
  IOReturn GetRootHubStringDescriptor(UInt8 index, OSData *desc);
};

#endif