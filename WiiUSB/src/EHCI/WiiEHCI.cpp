//
//  WiiEHCI.cpp
//  Wii U EHCI USB controller scaffold
//
//  Copyright © 2025 John Davis. All rights reserved.
//

#include "WiiEHCI.hpp"

#include <IOKit/IOPlatformExpert.h>
#include <IOKit/usb/USBHub.h>

#ifndef kAppleVendorID
#define kAppleVendorID 0x05AC
#endif

#ifndef kPrdRootHubAppleE
#define kPrdRootHubAppleE 0x8006
#endif

#define kWiiUSBBusNumberProperty              "USBBusNumber"
#define kWiiEHCI0BusNumber                    0x40
#define kWiiEHCIConfigFlagSettleDelayUS       10000
#define kWiiEHCIPortPowerSettleDelayUS        20000
#define kWiiEHCIPortResetHoldDelayMS          50
#define kWiiEHCIPortResetClearRetries         1000
#define kWiiEHCIPortResetClearDelayUS         10
#define kWiiEHCILatteControl                  0x00CC
#define kWiiEHCILatteControlInterruptEnable   BIT15
#define kWiiEHCIAsyncEndpointCount            32
#define kWiiEHCIAsyncTransferCount            128
#define kWiiEHCIAsyncTransferChunkSize        0x5000
#define kWiiEHCIAsyncErrorRetryCount          3
#define kWiiEHCIAsyncNAKReloadCount           4
#define kWiiEHCIAsyncHighBandwidthMult        1
#define kWiiEHCIAsyncWaitRetries              10000
#define kWiiEHCIAsyncWaitDelayUS              10

#define kWiiEHCIRootHubProductStringIndex   1
#define kWiiEHCIRootHubVendorStringIndex    2

OSDefineMetaClassAndStructors(WiiEHCI, super);

static inline UInt32 WiiEHCIEncodeQHLink(IOPhysicalAddress physAddr) {
  return ((((UInt32) physAddr) & kEHCIQHLinkPointerMask) | kEHCIQHLinkTypeQH);
}

static inline UInt32 WiiEHCITransferCount(UInt32 bufferSize) {
  if (bufferSize == 0) {
    return 1;
  }
  return ((bufferSize + kWiiEHCIAsyncTransferChunkSize - 1) / kWiiEHCIAsyncTransferChunkSize);
}

static inline UInt32 WiiEHCITransferBytesFromToken(UInt32 token) {
  return ((token & kEHCIqTDTokenBytesMask) >> kEHCIqTDTokenBytesShift);
}

static inline bool WiiEHCIShouldAdvanceToggle(UInt32 transferSize, UInt16 maxPacketSize) {
  if ((transferSize == 0) || (maxPacketSize == 0)) {
    return false;
  }
  return ((transferSize % maxPacketSize) == 0);
}

static inline UInt32 WiiEHCITransferPID(short direction) {
  if (direction == kUSBIn) {
    return kEHCIqTDPIDIn;
  }
  if (direction == kUSBOut) {
    return kEHCIqTDPIDOut;
  }
  return kEHCIqTDPIDSetup;
}

static inline bool WiiEHCIShouldHandPortToCompanion(UInt32 portStatus) {
  UInt32 lineStatus;

  if ((portStatus & kEHCIRegPortStatusControlConnection) == 0) {
    return false;
  }
  if ((portStatus & kEHCIRegPortStatusControlEnable) != 0) {
    return false;
  }

  lineStatus = portStatus & kEHCIRegPortStatusControlLineStatusMask;
  return (lineStatus == kEHCIRegPortStatusControlLineStatusJState)
    || (lineStatus == kEHCIRegPortStatusControlLineStatusKState);
}

bool WiiEHCI::waitForUSBCmdBit(UInt32 mask, bool set, UInt32 retries, UInt32 delayUS) {
  while (retries-- != 0) {
    if (((readOpReg32(kEHCIRegUSBCmd) & mask) != 0) == set) {
      return true;
    }
    IODelay(delayUS);
  }
  return false;
}

bool WiiEHCI::waitForUSBStsBit(UInt32 mask, bool set, UInt32 retries, UInt32 delayUS) {
  while (retries-- != 0) {
    if (((readOpReg32(kEHCIRegUSBSts) & mask) != 0) == set) {
      return true;
    }
    IODelay(delayUS);
  }
  return false;
}

bool WiiEHCI::waitForPortBit(UInt16 port, UInt32 mask, bool set, UInt32 retries, UInt32 delayUS) {
  while (retries-- != 0) {
    if (((readPortReg32(port) & mask) != 0) == set) {
      return true;
    }
    IODelay(delayUS);
  }
  return false;
}

void WiiEHCI::enableLatteInterruptNotification(void) {
  UInt32 latteControl;

  if (!checkPlatformCafe()) {
    return;
  }

  latteControl = readReg32(kWiiEHCILatteControl);
  if ((latteControl & kWiiEHCILatteControlInterruptEnable) != 0) {
    return;
  }

  latteControl |= kWiiEHCILatteControlInterruptEnable;
  writeReg32(kWiiEHCILatteControl, latteControl);
  OSSynchronizeIO();
  WIISYSLOG("Enabled Latte EHCI interrupt notification, ctl=0x%08X", readReg32(kWiiEHCILatteControl));
}

UInt16 WiiEHCI::getPortBit(UInt16 port) const {
  return (1U << (port - 1));
}

void WiiEHCI::routePortsToEHCI(void) {
  UInt32 configFlag;

  configFlag = readOpReg32(kEHCIRegConfigFlag);
  if ((configFlag & kEHCIRegConfigFlagRoutePortsEHCI) != 0) {
    return;
  }

  writeOpReg32(kEHCIRegConfigFlag, kEHCIRegConfigFlagRoutePortsEHCI);
  OSSynchronizeIO();
  IODelay(kWiiEHCIConfigFlagSettleDelayUS);
  WIISYSLOG("Forced EHCI configflag routing, new configflag=0x%08X", readOpReg32(kEHCIRegConfigFlag));
}

void WiiEHCI::powerOnPorts(void) {
  if ((_hcsParams & kEHCIRegHCSParamsPortPowerControl) == 0) {
    return;
  }

  for (UInt16 port = 1; port <= _numPorts; port++) {
    writePortFeature(port, kEHCIRegPortStatusControlPortPower, 0, 0);
  }
  IODelay(kWiiEHCIPortPowerSettleDelayUS);
}

void WiiEHCI::handPortToCompanion(UInt16 port, UInt32 portStatus) {
  UInt16 portBit;

  if ((portStatus & kEHCIRegPortStatusControlPortOwner) != 0) {
    return;
  }

  portBit = getPortBit(port);
  writePortFeature(port, kEHCIRegPortStatusControlPortOwner, 0, 0);
  _portResetActiveBitmap &= ~portBit;
  _portResetChangeBitmap |= portBit;
  WIISYSLOG("Handed EHCI port %u to companion controller, portsc=0x%08X", port, readPortReg32(port));
}

void WiiEHCI::updatePortChangeBits(void) {
  UInt16 resetChangedBitmap;

  resetChangedBitmap = 0;
  for (UInt16 port = 1; port <= _numPorts; port++) {
    UInt16 portBit;
    UInt32 portStatus;

    portBit = getPortBit(port);
    if ((_portResetActiveBitmap & portBit) == 0) {
      continue;
    }

    portStatus = readPortReg32(port);
    if ((portStatus & kEHCIRegPortStatusControlPortReset) == 0) {
      _portResetActiveBitmap &= ~portBit;

      if (WiiEHCIShouldHandPortToCompanion(portStatus)) {
        handPortToCompanion(port, portStatus);
      }

      resetChangedBitmap |= portBit;
    }
  }

  if (resetChangedBitmap != 0) {
    _portResetChangeBitmap |= resetChangedBitmap;
    WIIDBGLOG("Port reset change bitmap updated to 0x%X", _portResetChangeBitmap);
  }
}

bool WiiEHCI::hasPendingPortChange(UInt32 usbSts) {
  if ((usbSts & kEHCIRegUSBStsPortChangeDetect) != 0) {
    return true;
  }
  if (_portResetChangeBitmap != 0) {
    return true;
  }

  for (UInt16 port = 1; port <= _numPorts; port++) {
    if ((readPortReg32(port) & kEHCIRegPortStatusControlChangeMask) != 0) {
      return true;
    }
  }

  return false;
}

IOReturn WiiEHCI::resetRootHubPort(UInt16 port) {
  UInt16 portBit;
  UInt32 portStatus;

  portBit = getPortBit(port);
  _portResetChangeBitmap &= ~portBit;
  _portResetActiveBitmap |= portBit;
  writePortFeature(port, kEHCIRegPortStatusControlPortReset, 0, 0);

  IOSleep(kWiiEHCIPortResetHoldDelayMS);
  writePortFeature(port, 0, kEHCIRegPortStatusControlPortReset, 0);
  if (!waitForPortBit(port, kEHCIRegPortStatusControlPortReset, false,
      kWiiEHCIPortResetClearRetries, kWiiEHCIPortResetClearDelayUS)) {
    _portResetActiveBitmap &= ~portBit;
    WIISYSLOG("Timed out waiting for EHCI port %u reset completion, portsc=0x%08X", port, readPortReg32(port));
    return kIOReturnTimeout;
  }

  _portResetActiveBitmap &= ~portBit;
  portStatus = readPortReg32(port);
  if (WiiEHCIShouldHandPortToCompanion(portStatus)) {
    handPortToCompanion(port, portStatus);
  } else if ((portStatus & kEHCIRegPortStatusControlConnection)
      && ((portStatus & kEHCIRegPortStatusControlEnable) == 0)) {
    WIISYSLOG("EHCI port %u stayed disconnected from reset without FS/LS line state, portsc=0x%08X",
      port, portStatus);
  }

  _portResetChangeBitmap |= portBit;
  WIIDBGLOG("Port %u reset completed, portsc=0x%08X", port, portStatus);
  UIMRootHubStatusChange();
  return kIOReturnSuccess;
}

IOReturn WiiEHCI::allocateAsyncStructures(void) {
  IOByteCount length;
  IOPhysicalAddress physAddr;
  IOReturn status;

  if (_asyncHeadQH != NULL) {
    return kIOReturnSuccess;
  }

  _asyncQueueHeadDescriptor = IOBufferMemoryDescriptor::withOptions(kIOMemoryPhysicallyContiguous, PAGE_SIZE, PAGE_SIZE);
  if (_asyncQueueHeadDescriptor == NULL) {
    return kIOReturnNoMemory;
  }
  status = _asyncQueueHeadDescriptor->prepare();
  if (status != kIOReturnSuccess) {
    freeAsyncStructures();
    return status;
  }

  _asyncQueueHeads = reinterpret_cast<EHCIQueueHead*>(_asyncQueueHeadDescriptor->getBytesNoCopy());
  if (_asyncQueueHeads == NULL) {
    freeAsyncStructures();
    return kIOReturnNoMemory;
  }
  bzero(_asyncQueueHeads, PAGE_SIZE);
  IOSetProcessorCacheMode(kernel_task, (IOVirtualAddress) _asyncQueueHeads, PAGE_SIZE, kIOInhibitCache);

  physAddr = _asyncQueueHeadDescriptor->getPhysicalSegment(0, &length);
  _asyncHeadQH = &_asyncQueueHeads[0];
  _asyncHeadQHPhysAddr = physAddr;

  _asyncEndpointStates = reinterpret_cast<WiiEHCIEndpointState*>(IOMalloc(sizeof (WiiEHCIEndpointState) * kWiiEHCIAsyncEndpointCount));
  if (_asyncEndpointStates == NULL) {
    freeAsyncStructures();
    return kIOReturnNoMemory;
  }
  bzero(_asyncEndpointStates, sizeof (WiiEHCIEndpointState) * kWiiEHCIAsyncEndpointCount);

  _freeEndpointStateHead = &_asyncEndpointStates[0];
  for (UInt32 i = 0; i < kWiiEHCIAsyncEndpointCount; i++) {
    WiiEHCIEndpointState *endpoint;

    endpoint = &_asyncEndpointStates[i];
    endpoint->queueHead = &_asyncQueueHeads[i + 1];
    endpoint->qhPhysAddr = physAddr + ((i + 1) * sizeof (EHCIQueueHead));
    endpoint->nextEndpoint = (i + 1 < kWiiEHCIAsyncEndpointCount) ? &_asyncEndpointStates[i + 1] : NULL;
    initializeQueueHead(endpoint->queueHead);
  }

  _asyncTransferDescriptor = IOBufferMemoryDescriptor::withOptions(kIOMemoryPhysicallyContiguous, PAGE_SIZE, PAGE_SIZE);
  if (_asyncTransferDescriptor == NULL) {
    freeAsyncStructures();
    return kIOReturnNoMemory;
  }
  status = _asyncTransferDescriptor->prepare();
  if (status != kIOReturnSuccess) {
    freeAsyncStructures();
    return status;
  }

  _asyncTransfers = reinterpret_cast<EHCITransferDescriptor*>(_asyncTransferDescriptor->getBytesNoCopy());
  if (_asyncTransfers == NULL) {
    freeAsyncStructures();
    return kIOReturnNoMemory;
  }
  bzero(_asyncTransfers, PAGE_SIZE);
  IOSetProcessorCacheMode(kernel_task, (IOVirtualAddress) _asyncTransfers, PAGE_SIZE, kIOInhibitCache);

  physAddr = _asyncTransferDescriptor->getPhysicalSegment(0, &length);
  _asyncTransferStates = reinterpret_cast<WiiEHCIAsyncTransfer*>(IOMalloc(sizeof (WiiEHCIAsyncTransfer) * kWiiEHCIAsyncTransferCount));
  if (_asyncTransferStates == NULL) {
    freeAsyncStructures();
    return kIOReturnNoMemory;
  }
  bzero(_asyncTransferStates, sizeof (WiiEHCIAsyncTransfer) * kWiiEHCIAsyncTransferCount);

  _freeAsyncTransferHead = &_asyncTransferStates[0];
  for (UInt32 i = 0; i < kWiiEHCIAsyncTransferCount; i++) {
    WiiEHCIAsyncTransfer *transfer;

    transfer = &_asyncTransferStates[i];
    transfer->qtd = &_asyncTransfers[i];
    transfer->physAddr = physAddr + (i * sizeof (EHCITransferDescriptor));
    transfer->nextFree = (i + 1 < kWiiEHCIAsyncTransferCount) ? &_asyncTransferStates[i + 1] : NULL;
    initializeTransferDescriptor(transfer, true);
  }

  initializeQueueHead(_asyncHeadQH);
  _asyncHeadQH->nextQH = hostToDescriptor(WiiEHCIEncodeQHLink(_asyncHeadQHPhysAddr));
  _asyncHeadQH->info1 = hostToDescriptor(kEHCIQHInfo1HeadOfReclamation);
  _asyncHeadQH->overlayNextQTD = hostToDescriptor(kEHCIListPointerTerminate);
  _asyncHeadQH->overlayAltNextQTD = hostToDescriptor(kEHCIListPointerTerminate);
  _asyncHeadQH->overlayToken = hostToDescriptor(kEHCIqTDTokenHalted);
  _endpointStateHead = NULL;

  return kIOReturnSuccess;
}

void WiiEHCI::freeAsyncStructures(void) {
  if (_endpointStateHead != NULL) {
    WiiEHCIEndpointState *endpoint;

    endpoint = _endpointStateHead;
    while (endpoint != NULL) {
      WiiEHCIEndpointState *nextEndpoint;

      nextEndpoint = endpoint->nextEndpoint;
      while (endpoint->transactionHead != NULL) {
        WiiEHCIAsyncTransaction *transaction;

        transaction = endpoint->transactionHead;
        endpoint->transactionHead = transaction->nextTransaction;
        releaseAsyncTransaction(transaction);
      }
      endpoint = nextEndpoint;
    }
  }

  if (_asyncQueueHeadDescriptor != NULL) {
    _asyncQueueHeadDescriptor->complete();
  }
  if (_asyncTransferDescriptor != NULL) {
    _asyncTransferDescriptor->complete();
  }

  OSSafeReleaseNULL(_asyncQueueHeadDescriptor);
  OSSafeReleaseNULL(_asyncTransferDescriptor);

  if (_asyncEndpointStates != NULL) {
    IOFree(_asyncEndpointStates, sizeof (WiiEHCIEndpointState) * kWiiEHCIAsyncEndpointCount);
  }
  if (_asyncTransferStates != NULL) {
    IOFree(_asyncTransferStates, sizeof (WiiEHCIAsyncTransfer) * kWiiEHCIAsyncTransferCount);
  }

  _asyncQueueHeads = NULL;
  _asyncTransfers = NULL;
  _asyncHeadQH = NULL;
  _asyncHeadQHPhysAddr = 0;
  _asyncEndpointStates = NULL;
  _freeEndpointStateHead = NULL;
  _endpointStateHead = NULL;
  _asyncTransferStates = NULL;
  _freeAsyncTransferHead = NULL;
}

void WiiEHCI::initializeQueueHead(EHCIQueueHead *queueHead) {
  if (queueHead == NULL) {
    return;
  }

  bzero(queueHead, sizeof (*queueHead));
  queueHead->nextQH = hostToDescriptor(WiiEHCIEncodeQHLink(_asyncHeadQHPhysAddr));
  queueHead->overlayNextQTD = hostToDescriptor(kEHCIListPointerTerminate);
  queueHead->overlayAltNextQTD = hostToDescriptor(kEHCIListPointerTerminate);
  queueHead->overlayToken = 0;
}

void WiiEHCI::initializeTransferDescriptor(WiiEHCIAsyncTransfer *transfer, bool terminate) {
  if ((transfer == NULL) || (transfer->qtd == NULL)) {
    return;
  }

  bzero(transfer->qtd, sizeof (*transfer->qtd));
  transfer->qtd->nextQTD = hostToDescriptor(terminate ? kEHCIListPointerTerminate : 0);
  transfer->qtd->altNextQTD = hostToDescriptor(kEHCIListPointerTerminate);
  transfer->qtd->token = 0;
  transfer->nextFree = NULL;
  transfer->nextTransfer = NULL;
  transfer->transaction = NULL;
  transfer->transferLength = 0;
  transfer->pid = kEHCIqTDPIDOut;
}

IOReturn WiiEHCI::setAsyncScheduleEnabled(bool enabled) {
  UInt32 usbCmd;

  if (_baseAddr == NULL) {
    return kIOReturnSuccess;
  }

  usbCmd = readOpReg32(kEHCIRegUSBCmd);
  if (((usbCmd & kEHCIRegUSBCmdAsyncScheduleEnable) != 0) == enabled) {
    return kIOReturnSuccess;
  }

  if (enabled) {
    usbCmd |= kEHCIRegUSBCmdAsyncScheduleEnable;
  } else {
    usbCmd &= ~kEHCIRegUSBCmdAsyncScheduleEnable;
  }
  writeOpReg32(kEHCIRegUSBCmd, usbCmd);
  OSSynchronizeIO();

  if (!waitForUSBStsBit(kEHCIRegUSBStsAsyncScheduleStatus, enabled, kWiiEHCIAsyncWaitRetries, kWiiEHCIAsyncWaitDelayUS)) {
    WIISYSLOG("Timed out waiting for async schedule %s", enabled ? "enable" : "disable");
    return kIOReturnTimeout;
  }
  return kIOReturnSuccess;
}

IOReturn WiiEHCI::rebuildAsyncQueue(WiiEHCIEndpointState *endpoint) {
  UInt32 nextPhysAddr;
  UInt32 overlayToken;
  IOReturn status;

  if (endpoint == NULL) {
    return kIOReturnBadArgument;
  }

  nextPhysAddr = (endpoint->transactionHead != NULL)
    ? static_cast<UInt32>(endpoint->transactionHead->firstTransfer->physAddr)
    : static_cast<UInt32>(endpoint->dummyTransfer->physAddr);

  status = setAsyncScheduleEnabled(false);
  if (status != kIOReturnSuccess) {
    return status;
  }

  endpoint->queueHead->currentQTD = 0;
  endpoint->queueHead->overlayNextQTD = hostToDescriptor(nextPhysAddr);
  endpoint->queueHead->overlayAltNextQTD = hostToDescriptor(kEHCIListPointerTerminate);
  overlayToken = endpoint->isControl
    ? 0
    : (descriptorToHost(endpoint->queueHead->overlayToken) & (kEHCIqTDTokenDataToggle | kEHCIqTDTokenPingState));
  endpoint->queueHead->overlayToken = hostToDescriptor(overlayToken);
  bzero(endpoint->queueHead->overlayBuffer, sizeof (endpoint->queueHead->overlayBuffer));
  OSSynchronizeIO();

  return setAsyncScheduleEnabled(true);
}

WiiEHCI::WiiEHCIEndpointState *WiiEHCI::allocateEndpointState(void) {
  WiiEHCIEndpointState *endpoint;

  endpoint = _freeEndpointStateHead;
  if (endpoint == NULL) {
    return NULL;
  }

  _freeEndpointStateHead = endpoint->nextEndpoint;
  initializeQueueHead(endpoint->queueHead);
  endpoint->dummyTransfer = NULL;
  endpoint->transactionHead = NULL;
  endpoint->transactionTail = NULL;
  endpoint->nextEndpoint = NULL;
  endpoint->functionNumber = 0;
  endpoint->endpointNumber = 0;
  endpoint->direction = kUSBAnyDirn;
  endpoint->speed = 0;
  endpoint->maxPacketSize = 0;
  endpoint->isControl = false;
  return endpoint;
}

void WiiEHCI::releaseEndpointState(WiiEHCIEndpointState *endpoint) {
  if (endpoint == NULL) {
    return;
  }

  while (endpoint->transactionHead != NULL) {
    WiiEHCIAsyncTransaction *transaction;

    transaction = endpoint->transactionHead;
    endpoint->transactionHead = transaction->nextTransaction;
    releaseAsyncTransaction(transaction);
  }
  endpoint->transactionTail = NULL;

  if (endpoint->dummyTransfer != NULL) {
    releaseAsyncTransfer(endpoint->dummyTransfer);
    endpoint->dummyTransfer = NULL;
  }

  initializeQueueHead(endpoint->queueHead);
  endpoint->functionNumber = 0;
  endpoint->endpointNumber = 0;
  endpoint->direction = kUSBAnyDirn;
  endpoint->speed = 0;
  endpoint->maxPacketSize = 0;
  endpoint->isControl = false;
  endpoint->nextEndpoint = _freeEndpointStateHead;
  _freeEndpointStateHead = endpoint;
}

WiiEHCI::WiiEHCIAsyncTransfer *WiiEHCI::allocateAsyncTransfer(void) {
  WiiEHCIAsyncTransfer *transfer;

  transfer = _freeAsyncTransferHead;
  if (transfer == NULL) {
    return NULL;
  }

  _freeAsyncTransferHead = transfer->nextFree;
  initializeTransferDescriptor(transfer, true);
  return transfer;
}

void WiiEHCI::releaseAsyncTransfer(WiiEHCIAsyncTransfer *transfer) {
  if (transfer == NULL) {
    return;
  }

  initializeTransferDescriptor(transfer, true);
  transfer->nextFree = _freeAsyncTransferHead;
  _freeAsyncTransferHead = transfer;
}

WiiEHCI::WiiEHCIEndpointState *WiiEHCI::findEndpointState(UInt8 functionNumber, UInt8 endpointNumber, UInt8 direction) {
  WiiEHCIEndpointState *endpoint;

  endpoint = _endpointStateHead;
  while (endpoint != NULL) {
    if ((endpoint->functionNumber == functionNumber) && (endpoint->endpointNumber == endpointNumber)) {
      if (endpoint->isControl || (endpoint->direction == direction)) {
        return endpoint;
      }
    }
    endpoint = endpoint->nextEndpoint;
  }
  return NULL;
}

IOReturn WiiEHCI::createAsyncEndpoint(UInt8 functionNumber, UInt8 endpointNumber, UInt8 direction, UInt8 speed,
                                      UInt16 maxPacketSize, bool isControl, USBDeviceAddress highSpeedHub, int highSpeedPort) {
  WiiEHCIEndpointState *endpoint;
  UInt32 info1;
  UInt32 info2;
  IOReturn status;

  if (maxPacketSize == 0) {
    return kIOReturnBadArgument;
  }

  if (speed != kUSBDeviceSpeedHigh) {
    WIISYSLOG("EHCI TT/split support is not implemented for device %u ep %u", functionNumber, endpointNumber);
    return kIOReturnUnsupported;
  }
  if (!isControl && (direction != kUSBIn) && (direction != kUSBOut)) {
    return kIOReturnBadArgument;
  }

  endpoint = findEndpointState(functionNumber, endpointNumber, isControl ? kUSBAnyDirn : direction);
  if (endpoint != NULL) {
    return kIOReturnSuccess;
  }

  endpoint = allocateEndpointState();
  if (endpoint == NULL) {
    return kIOReturnNoMemory;
  }

  endpoint->dummyTransfer = allocateAsyncTransfer();
  if (endpoint->dummyTransfer == NULL) {
    releaseEndpointState(endpoint);
    return kIOReturnNoMemory;
  }

  endpoint->functionNumber = functionNumber;
  endpoint->endpointNumber = endpointNumber;
  endpoint->direction = isControl ? kUSBAnyDirn : direction;
  endpoint->speed = speed;
  endpoint->maxPacketSize = maxPacketSize;
  endpoint->isControl = isControl;

  info1 = functionNumber
    | (static_cast<UInt32>(endpointNumber) << kEHCIQHInfo1EndpointShift)
    | kEHCIQHInfo1HighSpeed
    | (static_cast<UInt32>(maxPacketSize) << kEHCIQHInfo1MaxPacketShift)
    | (static_cast<UInt32>(kWiiEHCIAsyncNAKReloadCount) << kEHCIQHInfo1NAKReloadShift);
  if (isControl) {
    info1 |= kEHCIQHInfo1ToggleControl;
  }

  info2 = (static_cast<UInt32>(kWiiEHCIAsyncHighBandwidthMult) << kEHCIQHInfo2MultShift);
  if ((speed != kUSBDeviceSpeedHigh) && (highSpeedHub != 0) && (highSpeedPort > 0)) {
    info2 |= (static_cast<UInt32>(highSpeedHub) << kEHCIQHInfo2HubAddrShift);
    info2 |= (static_cast<UInt32>(highSpeedPort) << kEHCIQHInfo2PortShift);
  }

  endpoint->queueHead->info1 = hostToDescriptor(info1);
  endpoint->queueHead->info2 = hostToDescriptor(info2);
  endpoint->queueHead->overlayNextQTD = hostToDescriptor(static_cast<UInt32>(endpoint->dummyTransfer->physAddr));
  endpoint->queueHead->overlayAltNextQTD = hostToDescriptor(kEHCIListPointerTerminate);
  endpoint->queueHead->overlayToken = 0;

  status = setAsyncScheduleEnabled(false);
  if (status != kIOReturnSuccess) {
    releaseEndpointState(endpoint);
    return status;
  }

  endpoint->nextEndpoint = _endpointStateHead;
  endpoint->queueHead->nextQH = hostToDescriptor(WiiEHCIEncodeQHLink(
    (endpoint->nextEndpoint != NULL) ? endpoint->nextEndpoint->qhPhysAddr : _asyncHeadQHPhysAddr));
  _asyncHeadQH->nextQH = hostToDescriptor(WiiEHCIEncodeQHLink(endpoint->qhPhysAddr));
  _endpointStateHead = endpoint;
  OSSynchronizeIO();

  status = setAsyncScheduleEnabled(true);
  if (status != kIOReturnSuccess) {
    _endpointStateHead = endpoint->nextEndpoint;
    _asyncHeadQH->nextQH = hostToDescriptor(WiiEHCIEncodeQHLink(
      (_endpointStateHead != NULL) ? _endpointStateHead->qhPhysAddr : _asyncHeadQHPhysAddr));
    releaseEndpointState(endpoint);
    return status;
  }

  WIIDBGLOG("Created async %s endpoint F=%u EP=%u dir=%u mps=%u hub=%u port=%d", isControl ? "control" : "bulk",
    functionNumber, endpointNumber, endpoint->direction, maxPacketSize, highSpeedHub, highSpeedPort);
  return kIOReturnSuccess;
}

IOReturn WiiEHCI::allocateBounceBuffer(UInt32 length, IOBufferMemoryDescriptor **descriptor, void **buffer, IOPhysicalAddress *physAddr) {
  IOReturn status;
  IOByteCount segmentLength;

  if ((descriptor == NULL) || (buffer == NULL) || (physAddr == NULL)) {
    return kIOReturnBadArgument;
  }

  *descriptor = NULL;
  *buffer = NULL;
  *physAddr = 0;

  if (length == 0) {
    return kIOReturnSuccess;
  }

  *descriptor = IOBufferMemoryDescriptor::withOptions(kIOMemoryPhysicallyContiguous, length, PAGE_SIZE);
  if (*descriptor == NULL) {
    return kIOReturnNoMemory;
  }

  status = (*descriptor)->prepare();
  if (status != kIOReturnSuccess) {
    OSSafeReleaseNULL(*descriptor);
    return status;
  }

  *buffer = (*descriptor)->getBytesNoCopy();
  *physAddr = (*descriptor)->getPhysicalSegment(0, &segmentLength);
  if ((*buffer == NULL) || (segmentLength < length)) {
    (*descriptor)->complete();
    OSSafeReleaseNULL(*descriptor);
    *buffer = NULL;
    *physAddr = 0;
    return kIOReturnDMAError;
  }

  bzero(*buffer, length);
  return kIOReturnSuccess;
}

IOReturn WiiEHCI::convertAsyncTokenStatus(UInt32 token) const {
  if ((token & kEHCIqTDTokenDataBufferError) != 0) {
    return kIOReturnDMAError;
  }
  if ((token & kEHCIqTDTokenBabbleDetected) != 0) {
    return kIOReturnOverrun;
  }
  if ((token & (kEHCIqTDTokenTransactionError | kEHCIqTDTokenMissedMicroFrame | kEHCIqTDTokenSplitState)) != 0) {
    return kIOReturnNotResponding;
  }
  if ((token & kEHCIqTDTokenHalted) != 0) {
    return kIOUSBPipeStalled;
  }
  return kIOReturnSuccess;
}

bool WiiEHCI::evaluateAsyncTransaction(WiiEHCIEndpointState *endpoint, WiiEHCIAsyncTransaction *transaction,
                                       IOReturn *status, UInt32 *bufferRemainder, bool *repairQueue) {
  WiiEHCIAsyncTransfer *transfer;
  UInt32 token;
  UInt32 remainder;
  bool lastTransferShortRead;

  if ((endpoint == NULL) || (transaction == NULL) || (status == NULL) || (bufferRemainder == NULL) || (repairQueue == NULL)) {
    return false;
  }

  remainder = 0;
  lastTransferShortRead = false;
  transfer = transaction->firstTransfer;
  while (transfer != NULL) {
    UInt32 transferRemainder;

    if (_invalidateCacheFunc != NULL) {
      _invalidateCacheFunc((vm_offset_t) transfer->qtd, sizeof (*transfer->qtd), false);
    }
    token = descriptorToHost(transfer->qtd->token);
    if ((token & kEHCIqTDTokenActive) != 0) {
      if (lastTransferShortRead) {
        WiiEHCIAsyncTransfer *remainingTransfer;

        remainingTransfer = transfer;
        while (remainingTransfer != NULL) {
          remainder += remainingTransfer->transferLength;
          remainingTransfer = remainingTransfer->nextTransfer;
        }

        *status = kIOReturnSuccess;
        *bufferRemainder = remainder;
        *repairQueue = true;
        return true;
      }
      return false;
    }

    transferRemainder = WiiEHCITransferBytesFromToken(token);
    remainder += transferRemainder;
    if ((token & (kEHCIqTDTokenHalted | kEHCIqTDTokenDataBufferError | kEHCIqTDTokenBabbleDetected
        | kEHCIqTDTokenTransactionError | kEHCIqTDTokenMissedMicroFrame | kEHCIqTDTokenSplitState)) != 0) {
      WiiEHCIAsyncTransfer *remainingTransfer;

      remainingTransfer = transfer->nextTransfer;
      while (remainingTransfer != NULL) {
        remainder += remainingTransfer->transferLength;
        remainingTransfer = remainingTransfer->nextTransfer;
      }

      *status = convertAsyncTokenStatus(token);
      *bufferRemainder = remainder;
      *repairQueue = true;
      return true;
    }

    lastTransferShortRead = ((transfer->pid == kEHCIqTDPIDIn) && (transferRemainder != 0));

    if (transfer == transaction->lastTransfer) {
      *status = kIOReturnSuccess;
      *bufferRemainder = remainder;
      *repairQueue = lastTransferShortRead;
      return true;
    }

    transfer = transfer->nextTransfer;
  }

  return false;
}

void WiiEHCI::releaseAsyncTransaction(WiiEHCIAsyncTransaction *transaction) {
  WiiEHCIAsyncTransfer *transfer;

  if (transaction == NULL) {
    return;
  }

  transfer = transaction->firstTransfer;
  while (transfer != NULL) {
    WiiEHCIAsyncTransfer *nextTransfer;

    nextTransfer = transfer->nextTransfer;
    releaseAsyncTransfer(transfer);
    transfer = nextTransfer;
  }

  if (transaction->bounceBufferDescriptor != NULL) {
    transaction->bounceBufferDescriptor->complete();
  }
  OSSafeReleaseNULL(transaction->bounceBufferDescriptor);
  OSSafeReleaseNULL(transaction->sourceBuffer);
  IOFree(transaction, sizeof (*transaction));
}

void WiiEHCI::abortEndpointTransactions(WiiEHCIEndpointState *endpoint, IOReturn status) {
  WiiEHCIAsyncTransaction *transaction;

  if ((endpoint == NULL) || (endpoint->transactionHead == NULL)) {
    return;
  }

  transaction = endpoint->transactionHead;
  endpoint->transactionHead = NULL;
  endpoint->transactionTail = NULL;
  (void) rebuildAsyncQueue(endpoint);

  while (transaction != NULL) {
    WiiEHCIAsyncTransaction *nextTransaction;

    nextTransaction = transaction->nextTransaction;
    if (transaction->completion.action != NULL) {
      Complete(transaction->completion, status, transaction->totalLength);
    }
    releaseAsyncTransaction(transaction);
    transaction = nextTransaction;
  }
}

void WiiEHCI::completeAsyncTransactions(void) {
  bool restartScan;

  do {
    WiiEHCIEndpointState *endpoint;

    restartScan = false;
    endpoint = _endpointStateHead;
    while (endpoint != NULL) {
      WiiEHCIAsyncTransaction *transaction;
      WiiEHCIEndpointState *nextEndpoint;

      transaction = endpoint->transactionHead;
      nextEndpoint = endpoint->nextEndpoint;
      if (transaction != NULL) {
        IOReturn transferStatus;
        UInt32 bufferRemainder;
        bool repairQueue;

        if (evaluateAsyncTransaction(endpoint, transaction, &transferStatus, &bufferRemainder, &repairQueue)) {
          UInt32 bytesTransferred;

          bytesTransferred = transaction->totalLength - bufferRemainder;
          endpoint->transactionHead = transaction->nextTransaction;
          if (endpoint->transactionHead == NULL) {
            endpoint->transactionTail = NULL;
          }

          if (repairQueue || (endpoint->transactionHead == NULL)) {
            (void) rebuildAsyncQueue(endpoint);
          }

          if ((transaction->sourceBuffer != NULL)
              && ((transaction->sourceBuffer->getDirection() & kIODirectionIn) != 0)
              && (transaction->bounceBuffer != NULL)
              && (bytesTransferred != 0)) {
            if (_invalidateCacheFunc != NULL) {
              _invalidateCacheFunc((vm_offset_t) transaction->bounceBuffer, transaction->totalLength, false);
            }
            transaction->sourceBuffer->writeBytes(0, transaction->bounceBuffer, bytesTransferred);
          }

          if (transaction->completion.action != NULL) {
            Complete(transaction->completion, transferStatus, bufferRemainder);
          }
          releaseAsyncTransaction(transaction);
          restartScan = true;
          break;
        }
      }

      endpoint = nextEndpoint;
    }
  } while (restartScan);
}

bool WiiEHCI::init(OSDictionary *dictionary) {
  WiiCheckDebugArgs();

  _memoryMap = NULL;
  _interruptEventSource = NULL;
  _baseAddr = NULL;
  _hcsParams = 0;
  _capLength = 0;
  _numPorts = 0;
  _rootHubAddress = 1;
  _portResetChangeBitmap = 0;
  _portResetActiveBitmap = 0;
  _invalidateCacheFunc = NULL;
  _asyncQueueHeadDescriptor = NULL;
  _asyncTransferDescriptor = NULL;
  _asyncQueueHeads = NULL;
  _asyncTransfers = NULL;
  _asyncHeadQH = NULL;
  _asyncHeadQHPhysAddr = 0;
  _asyncEndpointStates = NULL;
  _freeEndpointStateHead = NULL;
  _endpointStateHead = NULL;
  _asyncTransferStates = NULL;
  _freeAsyncTransferHead = NULL;
  _rootHubInterruptTransLock = IOLockAlloc();
  if (_rootHubInterruptTransLock == NULL) {
    return false;
  }
  bzero(_rootHubInterruptTransactions, sizeof (_rootHubInterruptTransactions));

  return super::init(dictionary);
}

void WiiEHCI::free(void) {
  if ((_interruptEventSource != NULL) && (_workLoop != NULL)) {
    _interruptEventSource->disable();
    _workLoop->removeEventSource(_interruptEventSource);
  }

  OSSafeReleaseNULL(_interruptEventSource);
  OSSafeReleaseNULL(_memoryMap);
  freeAsyncStructures();

  if (_rootHubInterruptTransLock != NULL) {
    IOLockFree(_rootHubInterruptTransLock);
    _rootHubInterruptTransLock = NULL;
  }

  super::free();
}

IOService *WiiEHCI::probe(IOService *provider, SInt32 *score) {
  UInt32 busNumber;
  const char *location = provider->getLocation();
  if ((location == NULL) || (strcmp(location, kWiiEHCI0Location) != 0)) {
    return NULL;
  }

  busNumber = kWiiEHCI0BusNumber;
  provider->setProperty(kWiiUSBBusNumberProperty, busNumber, 32);
  WIISYSLOG("Probing EHCI controller on provider location %s, assigning %s=0x%X", location,
    kWiiUSBBusNumberProperty, busNumber);
  return super::probe(provider, score);
}

IOReturn WiiEHCI::UIMInitialize(IOService *provider) {
  const OSSymbol *functionSymbol;
  UInt32 capReg;
  UInt32 usbIntr;
  UInt32 usbCmd;
  UInt32 usbSts;
  IOReturn status;

  WiiSetDebugLocation(provider->getLocation());

  _memoryMap = provider->mapDeviceMemoryWithIndex(0);
  if (_memoryMap == NULL) {
    WIISYSLOG("Failed to map EHCI memory");
    return kIOReturnNoResources;
  }
  _baseAddr = reinterpret_cast<volatile void*>(_memoryMap->getVirtualAddress());

  functionSymbol = OSSymbol::withCString(kWiiFuncPlatformGetInvalidateCache);
  if (functionSymbol == NULL) {
    return kIOReturnNoResources;
  }
  status = getPlatform()->callPlatformFunction(functionSymbol, false, &_invalidateCacheFunc, 0, 0, 0);
  functionSymbol->release();
  if ((status != kIOReturnSuccess) || (_invalidateCacheFunc == NULL)) {
    WIISYSLOG("Failed to get cache invalidation function");
    return (status == kIOReturnSuccess) ? kIOReturnUnsupported : status;
  }

  _interruptEventSource = IOInterruptEventSource::interruptEventSource(this,
#if __MAC_OS_X_VERSION_MIN_REQUIRED >= __MAC_10_2
    OSMemberFunctionCast(IOInterruptEventSource::Action, this, &WiiEHCI::handleInterrupt),
#else
    (IOInterruptEventSource::Action) &WiiEHCI::handleInterrupt,
#endif
    provider, 0);
  if (_interruptEventSource != NULL) {
    _workLoop->addEventSource(_interruptEventSource);
    _interruptEventSource->enable();
  } else {
    WIISYSLOG("Failed to create EHCI interrupt source, falling back to PollInterrupts");
  }

  capReg = readReg32(kEHCIRegCapLengthVersion);
  _capLength = capReg & kEHCIRegCapLengthMask;
  _hcsParams = readReg32(kEHCIRegHCSParams);
  _numPorts = _hcsParams & kEHCIRegHCSParamsNumPortsMask;
  usbCmd = (_capLength != 0) ? readOpReg32(kEHCIRegUSBCmd) : 0;
  usbSts = (_capLength != 0) ? readOpReg32(kEHCIRegUSBSts) : 0;

  WIISYSLOG("Mapped EHCI registers to %p (physical 0x%X), length 0x%X", _baseAddr,
    _memoryMap->getPhysicalAddress(), _memoryMap->getLength());
  WIISYSLOG("EHCI caps raw=0x%08X, capLength=0x%X, hciVersion=0x%X, hcsParams=0x%08X, usbcmd=0x%08X, usbsts=0x%08X",
    capReg, _capLength, (capReg & kEHCIRegHCIVersionMask) >> kEHCIRegHCIVersionShift, _hcsParams, usbCmd, usbSts);

  if ((_capLength < 0x10) || ((_capLength + kEHCIRegPortStatusControlBase + (_numPorts * sizeof (UInt32))) > _memoryMap->getLength())) {
    WIISYSLOG("EHCI register layout is invalid for mapped length 0x%X", _memoryMap->getLength());
    return kIOReturnUnsupported;
  }

  if ((usbSts & kEHCIRegUSBStsHCHalted) == 0) {
    writeOpReg32(kEHCIRegUSBCmd, usbCmd & ~kEHCIRegUSBCmdRunStop);
    if (!waitForUSBStsBit(kEHCIRegUSBStsHCHalted, true, 10000, 10)) {
      WIISYSLOG("Timed out waiting for EHCI controller halt");
      return kIOReturnTimeout;
    }
  }

  writeOpReg32(kEHCIRegUSBIntr, 0);
  writeOpReg32(kEHCIRegUSBCmd, kEHCIRegUSBCmdHostControllerReset);
  if (!waitForUSBCmdBit(kEHCIRegUSBCmdHostControllerReset, false, 10000, 10)) {
    WIISYSLOG("Timed out waiting for EHCI host controller reset");
    return kIOReturnTimeout;
  }
  if (!waitForUSBStsBit(kEHCIRegUSBStsHCHalted, true, 10000, 10)) {
    WIISYSLOG("EHCI controller did not halt after reset");
    return kIOReturnTimeout;
  }

  status = allocateAsyncStructures();
  if (status != kIOReturnSuccess) {
    WIISYSLOG("Failed to allocate EHCI async structures: 0x%X", status);
    return status;
  }

  writeOpReg32(kEHCIRegCtrlDSegment, 0);
  writeOpReg32(kEHCIRegPeriodicListBase, 0);
  writeOpReg32(kEHCIRegAsyncListAddr, static_cast<UInt32>(_asyncHeadQHPhysAddr));
  acknowledgeInterruptStatus(~0U);
  enableLatteInterruptNotification();

  usbCmd = readOpReg32(kEHCIRegUSBCmd) & ~(kEHCIRegUSBCmdHostControllerReset | kEHCIRegUSBCmdInterruptOnAsyncAdvance);
  usbCmd |= (kEHCIRegUSBCmdRunStop | kEHCIRegUSBCmdAsyncScheduleEnable);
  writeOpReg32(kEHCIRegUSBCmd, usbCmd);
  OSSynchronizeIO();
  if (!waitForUSBStsBit(kEHCIRegUSBStsHCHalted, false, 10000, 10)) {
    WIISYSLOG("Timed out waiting for EHCI controller run state");
    return kIOReturnTimeout;
  }
  if (!waitForUSBStsBit(kEHCIRegUSBStsAsyncScheduleStatus, true, kWiiEHCIAsyncWaitRetries, kWiiEHCIAsyncWaitDelayUS)) {
    WIISYSLOG("Timed out waiting for EHCI async schedule enable");
    return kIOReturnTimeout;
  }

  if (_interruptEventSource != NULL) {
    usbIntr = kEHCIRegUSBIntrUSBInterruptEnable | kEHCIRegUSBIntrUSBErrorInterruptEnable
      | kEHCIRegUSBIntrPortChangeEnable | kEHCIRegUSBIntrHostSystemErrorEnable;
    writeOpReg32(kEHCIRegUSBIntr, usbIntr);
    OSSynchronizeIO();
  }

  powerOnPorts();
  routePortsToEHCI();

  _rootHubAddress = 1;
  WIISYSLOG("EHCI controller running with %u ports, configflag=0x%08X", _numPorts, readOpReg32(kEHCIRegConfigFlag));
  for (UInt16 port = 1; port <= _numPorts; port++) {
    WIIDBGLOG("Initial port %u status: 0x%08X", port, readPortReg32(port));
  }

  return kIOReturnSuccess;
}

IOReturn WiiEHCI::UIMFinalize(void) {
  WiiEHCIEndpointState *endpoint;

  if (_baseAddr != NULL) {
    writeOpReg32(kEHCIRegUSBIntr, 0);
    (void) setAsyncScheduleEnabled(false);
    endpoint = _endpointStateHead;
    while (endpoint != NULL) {
      while (endpoint->transactionHead != NULL) {
        WiiEHCIAsyncTransaction *transaction;

        transaction = endpoint->transactionHead;
        endpoint->transactionHead = transaction->nextTransaction;
        releaseAsyncTransaction(transaction);
      }
      endpoint->transactionTail = NULL;
      endpoint = endpoint->nextEndpoint;
    }
    writeOpReg32(kEHCIRegUSBCmd, readOpReg32(kEHCIRegUSBCmd)
      & ~(kEHCIRegUSBCmdAsyncScheduleEnable | kEHCIRegUSBCmdRunStop));
  }
  return kIOReturnSuccess;
}

IOReturn WiiEHCI::submitAsyncTransfer(UInt8 functionNumber, UInt8 endpointNumber, UInt8 direction, IOUSBCompletion completion,
                                      IOMemoryDescriptor *buffer, UInt32 bufferSize, bool bufferRounding, bool controlTransfer) {
  WiiEHCIEndpointState *endpoint;
  WiiEHCIAsyncTransaction *transaction;
  WiiEHCIAsyncTransfer *firstTransfer;
  WiiEHCIAsyncTransfer *currentTransfer;
  WiiEHCIAsyncTransfer *extraHead;
  WiiEHCIAsyncTransfer *extraTail;
  WiiEHCIAsyncTransfer *newDummy;
  UInt32 transferCount;
  UInt32 remaining;
  UInt32 offset;
  UInt32 pid;
  UInt32 tokenToggle;
  bool queueWasEmpty;
  IOReturn status;

  (void) bufferRounding;

  endpoint = findEndpointState(functionNumber, endpointNumber, controlTransfer ? kUSBAnyDirn : direction);
  if (endpoint == NULL) {
    return kIOUSBEndpointNotFound;
  }
  if ((bufferSize != 0) && (buffer == NULL)) {
    return kIOReturnBadArgument;
  }

  transaction = reinterpret_cast<WiiEHCIAsyncTransaction*>(IOMalloc(sizeof (*transaction)));
  if (transaction == NULL) {
    return kIOReturnNoMemory;
  }
  bzero(transaction, sizeof (*transaction));

  transaction->completion = completion;
  transaction->totalLength = bufferSize;
  transaction->direction = direction;
  if (buffer != NULL) {
    buffer->retain();
    transaction->sourceBuffer = buffer;
  }

  status = allocateBounceBuffer(bufferSize, &transaction->bounceBufferDescriptor, &transaction->bounceBuffer, &transaction->bounceBufferPhysAddr);
  if (status != kIOReturnSuccess) {
    releaseAsyncTransaction(transaction);
    return status;
  }

  if ((bufferSize != 0) && (direction != kUSBIn)) {
    if (buffer->readBytes(0, transaction->bounceBuffer, bufferSize) != bufferSize) {
      releaseAsyncTransaction(transaction);
      return kIOReturnDMAError;
    }
    flushDataCache(transaction->bounceBuffer, bufferSize);
  }

  transferCount = WiiEHCITransferCount(bufferSize);
  firstTransfer = endpoint->dummyTransfer;
  extraHead = NULL;
  extraTail = NULL;
  newDummy = NULL;

  for (UInt32 i = 1; i < transferCount; i++) {
    WiiEHCIAsyncTransfer *transfer;

    transfer = allocateAsyncTransfer();
    if (transfer == NULL) {
      while (extraHead != NULL) {
        WiiEHCIAsyncTransfer *nextTransfer;

        nextTransfer = extraHead->nextTransfer;
        releaseAsyncTransfer(extraHead);
        extraHead = nextTransfer;
      }
      releaseAsyncTransaction(transaction);
      return kIOReturnNoMemory;
    }

    if (extraHead == NULL) {
      extraHead = transfer;
    } else {
      extraTail->nextTransfer = transfer;
    }
    extraTail = transfer;
  }

  newDummy = allocateAsyncTransfer();
  if (newDummy == NULL) {
    while (extraHead != NULL) {
      WiiEHCIAsyncTransfer *nextTransfer;

      nextTransfer = extraHead->nextTransfer;
      releaseAsyncTransfer(extraHead);
      extraHead = nextTransfer;
    }
    releaseAsyncTransaction(transaction);
    return kIOReturnNoMemory;
  }
  initializeTransferDescriptor(newDummy, true);

  firstTransfer->nextTransfer = extraHead;
  transaction->firstTransfer = firstTransfer;
  transaction->lastTransfer = (extraTail != NULL) ? extraTail : firstTransfer;

  currentTransfer = firstTransfer;
  remaining = bufferSize;
  offset = 0;
  pid = WiiEHCITransferPID(direction);
  tokenToggle = (controlTransfer && (direction != kUSBAnyDirn)) ? kEHCIqTDTokenDataToggle : 0;

  for (UInt32 index = 0; index < transferCount; index++) {
    WiiEHCIAsyncTransfer *nextTransfer;
    UInt32 transferSize;
    UInt32 nextPhysAddr;
    UInt32 token;

    transferSize = (remaining > kWiiEHCIAsyncTransferChunkSize) ? kWiiEHCIAsyncTransferChunkSize : remaining;
    if (index + 1 < transferCount) {
      nextTransfer = (index == 0) ? extraHead : currentTransfer->nextTransfer;
      nextPhysAddr = static_cast<UInt32>(nextTransfer->physAddr);
    } else {
      nextTransfer = NULL;
      nextPhysAddr = static_cast<UInt32>(newDummy->physAddr);
    }

    initializeTransferDescriptor(currentTransfer, false);
    currentTransfer->qtd->nextQTD = hostToDescriptor(nextPhysAddr);
    currentTransfer->qtd->altNextQTD = hostToDescriptor(kEHCIListPointerTerminate);

    token = kEHCIqTDTokenActive
      | (static_cast<UInt32>(kWiiEHCIAsyncErrorRetryCount) << kEHCIqTDTokenErrorCounterShift)
      | (pid << kEHCIqTDTokenPIDShift)
      | (transferSize << kEHCIqTDTokenBytesShift);
    if (index + 1 == transferCount) {
      token |= kEHCIqTDTokenInterruptOnComplete;
    }
    if (controlTransfer && (tokenToggle != 0)) {
      token |= kEHCIqTDTokenDataToggle;
    }
    currentTransfer->qtd->token = hostToDescriptor(token);

    if (transferSize != 0) {
      UInt32 bufferPhysAddr;
      UInt32 pageBase;

      bufferPhysAddr = static_cast<UInt32>(transaction->bounceBufferPhysAddr + offset);
      pageBase = (bufferPhysAddr & ~(PAGE_MASK));
      currentTransfer->qtd->buffer[0] = hostToDescriptor(bufferPhysAddr);
      for (UInt32 page = 1; page < ARRSIZE(currentTransfer->qtd->buffer); page++) {
        currentTransfer->qtd->buffer[page] = hostToDescriptor(pageBase + (page * PAGE_SIZE));
      }
    }

    currentTransfer->transaction = transaction;
    currentTransfer->transferLength = transferSize;
    currentTransfer->pid = pid;
    currentTransfer->nextTransfer = nextTransfer;

    if (controlTransfer && WiiEHCIShouldAdvanceToggle(transferSize, endpoint->maxPacketSize)) {
      tokenToggle ^= kEHCIqTDTokenDataToggle;
    }

    offset += transferSize;
    remaining -= transferSize;
    currentTransfer = nextTransfer;
  }

  queueWasEmpty = (endpoint->transactionHead == NULL);
  endpoint->dummyTransfer = newDummy;
  if (queueWasEmpty) {
    endpoint->transactionHead = transaction;
  } else {
    endpoint->transactionTail->nextTransaction = transaction;
  }
  endpoint->transactionTail = transaction;
  OSSynchronizeIO();

  if (queueWasEmpty) {
    status = rebuildAsyncQueue(endpoint);
    if (status != kIOReturnSuccess) {
      endpoint->transactionHead = NULL;
      endpoint->transactionTail = NULL;
      endpoint->dummyTransfer = firstTransfer;
      initializeTransferDescriptor(firstTransfer, true);

      while (extraHead != NULL) {
        WiiEHCIAsyncTransfer *nextTransfer;

        nextTransfer = extraHead->nextTransfer;
        releaseAsyncTransfer(extraHead);
        extraHead = nextTransfer;
      }
      releaseAsyncTransfer(newDummy);
      if (transaction->bounceBufferDescriptor != NULL) {
        transaction->bounceBufferDescriptor->complete();
      }
      OSSafeReleaseNULL(transaction->bounceBufferDescriptor);
      OSSafeReleaseNULL(transaction->sourceBuffer);
      IOFree(transaction, sizeof (*transaction));
      return status;
    }
  }

  return kIOReturnSuccess;
}

IOReturn WiiEHCI::UIMCreateControlEndpoint(UInt8 functionNumber, UInt8 endpointNumber, UInt16 maxPacketSize, UInt8 speed) {
  if (functionNumber == _rootHubAddress) {
    return simulateRootHubControlEDCreate(endpointNumber, maxPacketSize, speed);
  }
  return UIMCreateControlEndpoint(functionNumber, endpointNumber, maxPacketSize, speed, 0, 0);
}

IOReturn WiiEHCI::UIMCreateControlEndpoint(UInt8 functionNumber, UInt8 endpointNumber, UInt16 maxPacketSize, UInt8 speed,
                                           USBDeviceAddress highSpeedHub, int highSpeedPort) {
  if (functionNumber == _rootHubAddress) {
    return simulateRootHubControlEDCreate(endpointNumber, maxPacketSize, speed);
  }
  return createAsyncEndpoint(functionNumber, endpointNumber, kUSBAnyDirn, speed, maxPacketSize, true, highSpeedHub, highSpeedPort);
}

IOReturn WiiEHCI::UIMCreateControlTransfer(short functionNumber, short endpointNumber, IOUSBCompletion completion, void *CBP,
                                           bool bufferRounding, UInt32 bufferSize, short direction) {
  IOMemoryDescriptor *desc = NULL;
  IODirection        descDirection;
  IOReturn           status;

  if (direction == kUSBOut) {
    descDirection = kIODirectionOut;
  } else if (direction == kUSBIn) {
    descDirection = kIODirectionIn;
  } else {
    descDirection = kIODirectionOut;
  }

  if (bufferSize != 0) {
    desc = IOMemoryDescriptor::withAddress(CBP, bufferSize, descDirection);
    if (desc == NULL) {
      return kIOReturnNoMemory;
    }
  }

  status = UIMCreateControlTransfer(functionNumber, endpointNumber, completion, desc, bufferRounding, bufferSize, direction);
  OSSafeReleaseNULL(desc);
  return status;
}

IOReturn WiiEHCI::UIMCreateControlTransfer(short functionNumber, short endpointNumber, IOUSBCompletion completion,
                                           IOMemoryDescriptor *CBP, bool bufferRounding, UInt32 bufferSize, short direction) {
  return submitAsyncTransfer(functionNumber, endpointNumber, direction, completion, CBP, bufferSize, bufferRounding, true);
}

IOReturn WiiEHCI::UIMCreateBulkEndpoint(UInt8 functionNumber, UInt8 endpointNumber, UInt8 direction, UInt8 speed, UInt8 maxPacketSize) {
  return UIMCreateBulkEndpoint(functionNumber, endpointNumber, direction, speed, maxPacketSize, 0, 0);
}

IOReturn WiiEHCI::UIMCreateBulkEndpoint(UInt8 functionNumber, UInt8 endpointNumber, UInt8 direction, UInt8 speed,
                                        UInt16 maxPacketSize, USBDeviceAddress highSpeedHub, int highSpeedPort) {
  return createAsyncEndpoint(functionNumber, endpointNumber, direction, speed, maxPacketSize, false, highSpeedHub, highSpeedPort);
}

IOReturn WiiEHCI::UIMCreateBulkTransfer(short functionNumber, short endpointNumber, IOUSBCompletion completion,
                                        IOMemoryDescriptor *CBP, bool bufferRounding, UInt32 bufferSize, short direction) {
  return submitAsyncTransfer(functionNumber, endpointNumber, direction, completion, CBP, bufferSize, bufferRounding, false);
}

IOReturn WiiEHCI::UIMCreateInterruptEndpoint(short functionAddress, short endpointNumber, UInt8 direction,
                                             short speed, UInt16 maxPacketSize, short pollingRate) {
  if (functionAddress == _rootHubAddress) {
    return simulateRootHubInterruptEDCreate(endpointNumber, direction, speed, maxPacketSize);
  }
  return UIMCreateInterruptEndpoint(functionAddress, endpointNumber, direction, speed, maxPacketSize, pollingRate, 0, 0);
}

IOReturn WiiEHCI::UIMCreateInterruptEndpoint(short functionAddress, short endpointNumber, UInt8 direction,
                                             short speed, UInt16 maxPacketSize, short pollingRate,
                                             USBDeviceAddress highSpeedHub, int highSpeedPort) {
  if (functionAddress == _rootHubAddress) {
    return simulateRootHubInterruptEDCreate(endpointNumber, direction, speed, maxPacketSize);
  }
  return kIOReturnUnsupported;
}

IOReturn WiiEHCI::UIMCreateInterruptTransfer(short functionNumber, short endpointNumber, IOUSBCompletion completion,
                                             IOMemoryDescriptor *CBP, bool bufferRounding, UInt32 bufferSize, short direction) {
  if (functionNumber == _rootHubAddress) {
    simulateRootHubInterruptTransfer(endpointNumber, completion, CBP, bufferRounding, bufferSize, direction);
    return kIOReturnSuccess;
  }
  return kIOReturnUnsupported;
}

IOReturn WiiEHCI::UIMCreateIsochEndpoint(short functionAddress, short endpointNumber, UInt32 maxPacketSize, UInt8 direction) {
  return UIMCreateIsochEndpoint(functionAddress, endpointNumber, maxPacketSize, direction, 0, 0);
}

IOReturn WiiEHCI::UIMCreateIsochEndpoint(short functionAddress, short endpointNumber, UInt32 maxPacketSize, UInt8 direction,
                                         USBDeviceAddress highSpeedHub, int highSpeedPort) {
  return kIOReturnUnsupported;
}

IOReturn WiiEHCI::UIMCreateIsochTransfer(short functionAddress, short endpointNumber, IOUSBIsocCompletion completion, UInt8 direction,
                                         UInt64 frameStart, IOMemoryDescriptor *pBuffer, UInt32 frameCount, IOUSBIsocFrame *pFrames) {
  return kIOReturnUnsupported;
}

IOReturn WiiEHCI::UIMAbortEndpoint(short functionNumber, short endpointNumber, short direction) {
  WiiEHCIEndpointState *endpoint;

  if (functionNumber == _rootHubAddress) {
    UIMRootHubStatusChange(true);
    return kIOReturnSuccess;
  }

  endpoint = findEndpointState(functionNumber, endpointNumber, direction);
  if (endpoint == NULL) {
    return kIOUSBEndpointNotFound;
  }

  abortEndpointTransactions(endpoint, kIOReturnAborted);
  return kIOReturnSuccess;
}

IOReturn WiiEHCI::UIMDeleteEndpoint(short functionNumber, short endpointNumber, short direction) {
  WiiEHCIEndpointState *endpoint;
  WiiEHCIEndpointState *prevEndpoint;
  IOReturn status;

  if (functionNumber == _rootHubAddress) {
    UIMRootHubStatusChange(true);
    return kIOReturnSuccess;
  }

  endpoint = findEndpointState(functionNumber, endpointNumber, direction);
  if (endpoint == NULL) {
    return kIOUSBEndpointNotFound;
  }

  abortEndpointTransactions(endpoint, kIOReturnAborted);

  status = setAsyncScheduleEnabled(false);
  if (status != kIOReturnSuccess) {
    return status;
  }

  prevEndpoint = NULL;
  if (_endpointStateHead == endpoint) {
    _endpointStateHead = endpoint->nextEndpoint;
    _asyncHeadQH->nextQH = hostToDescriptor(WiiEHCIEncodeQHLink(
      (_endpointStateHead != NULL) ? _endpointStateHead->qhPhysAddr : _asyncHeadQHPhysAddr));
  } else {
    prevEndpoint = _endpointStateHead;
    while ((prevEndpoint != NULL) && (prevEndpoint->nextEndpoint != endpoint)) {
      prevEndpoint = prevEndpoint->nextEndpoint;
    }
    if (prevEndpoint != NULL) {
      prevEndpoint->nextEndpoint = endpoint->nextEndpoint;
      prevEndpoint->queueHead->nextQH = endpoint->queueHead->nextQH;
    }
  }
  OSSynchronizeIO();
  releaseEndpointState(endpoint);

  return setAsyncScheduleEnabled(true);
}

IOReturn WiiEHCI::UIMClearEndpointStall(short functionNumber, short endpointNumber, short direction) {
  WiiEHCIEndpointState *endpoint;
  UInt32 overlayToken;

  if (functionNumber == _rootHubAddress) {
    return kIOReturnSuccess;
  }

  endpoint = findEndpointState(functionNumber, endpointNumber, direction);
  if (endpoint == NULL) {
    return kIOUSBEndpointNotFound;
  }
  if (endpoint->transactionHead != NULL) {
    return kIOReturnBusy;
  }
  if (endpoint->isControl) {
    return kIOReturnSuccess;
  }

  overlayToken = descriptorToHost(endpoint->queueHead->overlayToken);
  overlayToken &= ~(kEHCIqTDTokenDataToggle | kEHCIqTDTokenHalted | kEHCIqTDTokenPingState);
  endpoint->queueHead->overlayToken = hostToDescriptor(overlayToken);
  return rebuildAsyncQueue(endpoint);
}

void WiiEHCI::UIMRootHubStatusChange(void) {
  completeRootHubInterruptTransfer(false);
}

void WiiEHCI::UIMRootHubStatusChange(bool abort) {
  completeRootHubInterruptTransfer(abort);
}

IOReturn WiiEHCI::GetRootHubDeviceDescriptor(IOUSBDeviceDescriptor *desc) {
  if (desc == NULL) {
    return kIOReturnNoMemory;
  }

  desc->bLength = sizeof (*desc);
  desc->bDescriptorType = kUSBDeviceDesc;
  desc->bcdUSB = USB_CONSTANT16(kUSBRel20);
  desc->bDeviceClass = kUSBHubClass;
  desc->bDeviceSubClass = kUSBHubSubClass;
  desc->bDeviceProtocol = 1;
  desc->bMaxPacketSize0 = 64;
  desc->idVendor = USB_CONSTANT16(kAppleVendorID);
  desc->idProduct = USB_CONSTANT16(kPrdRootHubAppleE);
  desc->bcdDevice = USB_CONSTANT16(0x0200);
  desc->iManufacturer = kWiiEHCIRootHubVendorStringIndex;
  desc->iProduct = kWiiEHCIRootHubProductStringIndex;
  desc->iSerialNumber = 0;
  desc->bNumConfigurations = 1;

  return kIOReturnSuccess;
}

IOReturn WiiEHCI::GetRootHubDescriptor(IOUSBHubDescriptor *desc) {
  if (desc == NULL) {
    return kIOReturnNoMemory;
  }

  bzero(desc, sizeof (*desc));
  desc->length = sizeof (*desc);
  desc->hubType = kUSBHubDescriptorType;
  desc->numPorts = _numPorts;
  desc->characteristics = 0;
  if ((_hcsParams & kEHCIRegHCSParamsPortPowerControl) != 0) {
    desc->characteristics |= HostToUSBWord(kPerPortSwitchingBit);
  } else {
    desc->characteristics |= HostToUSBWord(kNoPowerSwitchingBit);
  }
  desc->powerOnToGood = 10;
  desc->hubCurrent = 0;
  return kIOReturnSuccess;
}

IOReturn WiiEHCI::SetRootHubDescriptor(OSData *buffer) {
  return kIOReturnSuccess;
}

IOReturn WiiEHCI::GetRootHubConfDescriptor(OSData *desc) {
  IOUSBConfigurationDescriptor confDescriptor = {
    sizeof (IOUSBConfigurationDescriptor),
    kUSBConfDesc,
    USB_CONSTANT16(sizeof (IOUSBConfigurationDescriptor) + sizeof (IOUSBInterfaceDescriptor) + sizeof (IOUSBEndpointDescriptor)),
    1,
    1,
    0,
    0x60,
    0
  };
  IOUSBInterfaceDescriptor interfaceDescriptor = {
    sizeof (IOUSBInterfaceDescriptor),
    kUSBInterfaceDesc,
    0,
    0,
    1,
    kUSBHubClass,
    kUSBHubSubClass,
    1,
    0
  };
  IOUSBEndpointDescriptor endpointDescriptor = {
    sizeof (IOUSBEndpointDescriptor),
    kUSBEndpointDesc,
    0x81,
    kUSBInterrupt,
    HostToUSBWord(8),
    12
  };

  if (desc == NULL) {
    return kIOReturnNoMemory;
  }
  if (!desc->appendBytes(&confDescriptor, confDescriptor.bLength)) {
    return kIOReturnNoMemory;
  }
  if (!desc->appendBytes(&interfaceDescriptor, interfaceDescriptor.bLength)) {
    return kIOReturnNoMemory;
  }
  if (!desc->appendBytes(&endpointDescriptor, endpointDescriptor.bLength)) {
    return kIOReturnNoMemory;
  }
  return kIOReturnSuccess;
}

IOReturn WiiEHCI::GetRootHubStatus(IOUSBHubStatus *status) {
  if (status == NULL) {
    return kIOReturnNoMemory;
  }
  status->statusFlags = 0;
  status->changeFlags = 0;
  return kIOReturnSuccess;
}

IOReturn WiiEHCI::SetRootHubFeature(UInt16 wValue) {
  return kIOReturnSuccess;
}

IOReturn WiiEHCI::ClearRootHubFeature(UInt16 wValue) {
  return kIOReturnSuccess;
}

IOReturn WiiEHCI::GetRootHubPortStatus(IOUSBHubPortStatus *status, UInt16 port) {
  UInt16 portBit;
  UInt32 portStatus;
  UInt16 statusFlags;
  UInt16 changeFlags;

  if (status == NULL) {
    return kIOReturnNoMemory;
  }

  if ((port < 1) || (port > _numPorts)) {
    return kIOReturnBadArgument;
  }

  updatePortChangeBits();

  portBit = getPortBit(port);
  portStatus = readPortReg32(port);
  statusFlags = 0;
  changeFlags = 0;

  if (portStatus & kEHCIRegPortStatusControlConnection) {
    statusFlags |= kHubPortConnection;
  }
  if (portStatus & kEHCIRegPortStatusControlEnable) {
    statusFlags |= kHubPortEnabled;
  }
  if (portStatus & kEHCIRegPortStatusControlSuspend) {
    statusFlags |= kHubPortSuspend;
  }
  if (portStatus & kEHCIRegPortStatusControlOverCurrentActive) {
    statusFlags |= kHubPortOverCurrent;
  }
  if (portStatus & kEHCIRegPortStatusControlPortReset) {
    statusFlags |= kHubPortBeingReset;
  }
  if ((portStatus & kEHCIRegPortStatusControlPortPower) || ((_hcsParams & kEHCIRegHCSParamsPortPowerControl) == 0)) {
    statusFlags |= kHubPortPower;
  }
  if ((portStatus & kEHCIRegPortStatusControlPortOwner) == 0) {
    if ((portStatus & kEHCIRegPortStatusControlConnection) && (portStatus & kEHCIRegPortStatusControlEnable)) {
      statusFlags |= kHubPortHighSpeed;
    }
  } else if ((portStatus & kEHCIRegPortStatusControlLineStatusMask) == kEHCIRegPortStatusControlLineStatusKState) {
    statusFlags |= kHubPortLowSpeed;
  }

  if (portStatus & kEHCIRegPortStatusControlConnectionChange) {
    changeFlags |= kHubPortConnection;
  }
  if (portStatus & kEHCIRegPortStatusControlEnableChange) {
    changeFlags |= kHubPortEnabled;
  }
  if (portStatus & kEHCIRegPortStatusControlOverCurrentChange) {
    changeFlags |= kHubPortOverCurrent;
  }
  if ((_portResetChangeBitmap & portBit) != 0) {
    changeFlags |= kHubPortBeingReset;
  }

  status->statusFlags = HostToUSBWord(statusFlags);
  status->changeFlags = HostToUSBWord(changeFlags);

  WIIDBGLOG("Port %u status: portsc=0x%08X, status=0x%04X, change=0x%04X", port, portStatus, statusFlags, changeFlags);
  return kIOReturnSuccess;
}

IOReturn WiiEHCI::SetRootHubPortFeature(UInt16 wValue, UInt16 port) {
  if ((port < 1) || (port > _numPorts)) {
    return kIOReturnBadArgument;
  }

  switch (wValue) {
    case kUSBHubPortEnableFeature:
      writePortFeature(port, kEHCIRegPortStatusControlEnable, 0, 0);
      break;

    case kUSBHubPortSuspendFeature:
      writePortFeature(port, kEHCIRegPortStatusControlSuspend, 0, 0);
      break;

    case kUSBHubPortResetFeature:
      return resetRootHubPort(port);

    case kUSBHubPortPowerFeature:
      writePortFeature(port, kEHCIRegPortStatusControlPortPower, 0, 0);
      break;

    default:
      WIISYSLOG("Unsupported EHCI port %u feature set: 0x%X", port, wValue);
      return kIOReturnUnsupported;
  }

  return kIOReturnSuccess;
}

IOReturn WiiEHCI::ClearRootHubPortFeature(UInt16 wValue, UInt16 port) {
  if ((port < 1) || (port > _numPorts)) {
    return kIOReturnBadArgument;
  }

  switch (wValue) {
    case kUSBHubPortEnableFeature:
      writePortFeature(port, 0, kEHCIRegPortStatusControlEnable, 0);
      break;

    case kUSBHubPortSuspendFeature:
      writePortFeature(port, kEHCIRegPortStatusControlForcePortResume, 0, 0);
      break;

    case kUSBHubPortPowerFeature:
      writePortFeature(port, 0, kEHCIRegPortStatusControlPortPower, 0);
      break;

    case kUSBHubPortConnectionChangeFeature:
      writePortFeature(port, 0, 0, kEHCIRegPortStatusControlConnectionChange);
      break;

    case kUSBHubPortEnableChangeFeature:
      writePortFeature(port, 0, 0, kEHCIRegPortStatusControlEnableChange);
      break;

    case kUSBHubPortOverCurrentChangeFeature:
      writePortFeature(port, 0, 0, kEHCIRegPortStatusControlOverCurrentChange);
      break;

    case kUSBHubPortResetChangeFeature:
      _portResetChangeBitmap &= ~getPortBit(port);
      break;

    default:
      WIISYSLOG("Unsupported EHCI port %u feature clear: 0x%X", port, wValue);
      return kIOReturnUnsupported;
  }

  return kIOReturnSuccess;
}

IOReturn WiiEHCI::GetRootHubPortState(UInt8 *state, UInt16 port) {
  if (state == NULL) {
    return kIOReturnNoMemory;
  }
  *state = 0;
  return kIOReturnSuccess;
}

IOReturn WiiEHCI::SetHubAddress(UInt16 wValue) {
  _rootHubAddress = wValue;
  return kIOReturnSuccess;
}

UInt32 WiiEHCI::GetBandwidthAvailable(void) {
  return 0;
}

UInt64 WiiEHCI::GetFrameNumber(void) {
  return GetFrameNumber32();
}

UInt32 WiiEHCI::GetFrameNumber32(void) {
  if (_baseAddr == NULL) {
    return 0;
  }
  return (readOpReg32(kEHCIRegFrameIndex) >> 3);
}

void WiiEHCI::PollInterrupts(IOUSBCompletionAction safeAction) {
  UInt32 usbSts;
  bool   rootHubChanged;

  (void) safeAction;

  if (_baseAddr == NULL) {
    return;
  }

  updatePortChangeBits();
  usbSts = readOpReg32(kEHCIRegUSBSts);
  rootHubChanged = hasPendingPortChange(usbSts);

  if ((usbSts & (kEHCIRegUSBStsUSBInterrupt | kEHCIRegUSBStsUSBErrorInterrupt
      | kEHCIRegUSBStsPortChangeDetect | kEHCIRegUSBStsHostSystemError
      | kEHCIRegUSBStsFrameListRollover | kEHCIRegUSBStsInterruptOnAsyncAdvance)) != 0) {
    acknowledgeInterruptStatus(usbSts);
  }

  if ((usbSts & (kEHCIRegUSBStsUSBInterrupt | kEHCIRegUSBStsUSBErrorInterrupt)) != 0) {
    completeAsyncTransactions();
  }
  if (usbSts & kEHCIRegUSBStsHostSystemError) {
    WIISYSLOG("EHCI host system error while polling, status 0x%08X", usbSts);
  }
  if ((usbSts & kEHCIRegUSBStsHostSystemError) != 0) {
    UIMRootHubStatusChange();
  } else if (rootHubChanged) {
    UIMRootHubStatusChange();
  }
}

IOReturn WiiEHCI::GetRootHubStringDescriptor(UInt8 index, OSData *desc) {
  UInt8 productName[] = {
    0,
    kUSBStringDesc,
    0x45, 0x00,
    0x48, 0x00,
    0x43, 0x00,
    0x49, 0x00,
    0x20, 0x00,
    0x52, 0x00,
    0x6F, 0x00,
    0x6F, 0x00,
    0x74, 0x00,
    0x20, 0x00,
    0x48, 0x00,
    0x75, 0x00,
    0x62, 0x00,
    0x20, 0x00,
    0x53, 0x00,
    0x69, 0x00,
    0x6D, 0x00,
    0x75, 0x00,
    0x6C, 0x00,
    0x61, 0x00,
    0x74, 0x00,
    0x69, 0x00,
    0x6F, 0x00,
    0x6E, 0x00,
  };
  UInt8 vendorName[] = {
    0,
    kUSBStringDesc,
    0x41, 0x00,
    0x70, 0x00,
    0x70, 0x00,
    0x6C, 0x00,
    0x65, 0x00,
    0x20, 0x00,
    0x43, 0x00,
    0x6F, 0x00,
    0x6D, 0x00,
    0x70, 0x00,
    0x75, 0x00,
    0x74, 0x00,
    0x65, 0x00,
    0x72, 0x00,
    0x2C, 0x00,
    0x20, 0x00,
    0x49, 0x00,
    0x6E, 0x00,
    0x63, 0x00,
    0x2E, 0x00,
  };

  if (index > kWiiEHCIRootHubVendorStringIndex) {
    return kIOReturnBadArgument;
  }

  productName[0] = sizeof (productName);
  vendorName[0] = sizeof (vendorName);

  if (desc == NULL) {
    return kIOReturnNoMemory;
  }

  if (index == kWiiEHCIRootHubProductStringIndex) {
    if (!desc->appendBytes(&productName, productName[0])) {
      return kIOReturnNoMemory;
    }
  }

  if (index == kWiiEHCIRootHubVendorStringIndex) {
    if (!desc->appendBytes(&vendorName, vendorName[0])) {
      return kIOReturnNoMemory;
    }
  }

  return kIOReturnSuccess;
}

void WiiEHCI::handleInterrupt(IOInterruptEventSource *intEventSource, int count) {
  UInt32 usbSts;
  bool   rootHubChanged;

  (void) intEventSource;
  (void) count;

  if (_baseAddr == NULL) {
    return;
  }

  updatePortChangeBits();
  usbSts = readOpReg32(kEHCIRegUSBSts);
  rootHubChanged = hasPendingPortChange(usbSts);
  acknowledgeInterruptStatus(usbSts);
  if ((usbSts & (kEHCIRegUSBStsUSBInterrupt | kEHCIRegUSBStsUSBErrorInterrupt)) != 0) {
    completeAsyncTransactions();
  }
  if (usbSts & kEHCIRegUSBStsHostSystemError) {
    WIISYSLOG("EHCI host system error interrupt, status 0x%08X", usbSts);
  }
  if (rootHubChanged || (usbSts & kEHCIRegUSBStsHostSystemError) != 0) {
    UIMRootHubStatusChange();
  }
}

void WiiEHCI::acknowledgeInterruptStatus(UInt32 usbSts) {
  UInt32 ackMask;

  ackMask = usbSts & (kEHCIRegUSBStsUSBInterrupt | kEHCIRegUSBStsUSBErrorInterrupt
    | kEHCIRegUSBStsPortChangeDetect | kEHCIRegUSBStsFrameListRollover
    | kEHCIRegUSBStsHostSystemError | kEHCIRegUSBStsInterruptOnAsyncAdvance);
  if (ackMask != 0) {
    writeOpReg32(kEHCIRegUSBSts, ackMask);
    OSSynchronizeIO();
  }
}

void WiiEHCI::writePortFeature(UInt16 port, UInt32 setBits, UInt32 clearBits, UInt32 clearChangeBits) {
  UInt32 portStatus;
  UInt32 writeValue;

  portStatus = readPortReg32(port);
  writeValue = portStatus & kEHCIRegPortStatusControlPreserveMask;
  writeValue &= ~clearBits;
  writeValue |= setBits;
  writeValue |= clearChangeBits;
  writePortReg32(port, writeValue);
  (void) readPortReg32(port);
  OSSynchronizeIO();
}

IOReturn WiiEHCI::simulateRootHubControlEDCreate(UInt8 endpointNumber, UInt16 maxPacketSize, UInt8 speed) {
  if (endpointNumber != 0) {
    return kIOReturnBadArgument;
  }
  return kIOReturnSuccess;
}

IOReturn WiiEHCI::simulateRootHubInterruptEDCreate(short endpointNumber, UInt8 direction, short speed, UInt16 maxPacketSize) {
  if ((endpointNumber != 1) || (direction != kUSBIn)) {
    return kIOReturnBadArgument;
  }
  return kIOReturnSuccess;
}

void WiiEHCI::simulateRootHubInterruptTransfer(short endpointNumber, IOUSBCompletion completion,
                                               IOMemoryDescriptor *CBP, bool bufferRounding, UInt32 bufferSize, short direction) {
  if ((endpointNumber != 1) || (direction != kUSBIn)) {
    Complete(completion, kIOReturnBadArgument, bufferSize);
    return;
  }

  IOLockLock(_rootHubInterruptTransLock);
  for (unsigned int i = 0; i < ARRSIZE(_rootHubInterruptTransactions); i++) {
    if (_rootHubInterruptTransactions[i].completion.action == NULL) {
      _rootHubInterruptTransactions[i].buffer = CBP;
      _rootHubInterruptTransactions[i].bufferLength = bufferSize;
      _rootHubInterruptTransactions[i].completion = completion;
      IOLockUnlock(_rootHubInterruptTransLock);
      UIMRootHubStatusChange();
      return;
    }
  }
  IOLockUnlock(_rootHubInterruptTransLock);
  Complete(completion, kIOReturnNoMemory, bufferSize);
}

void WiiEHCI::completeRootHubInterruptTransfer(bool abort) {
  struct WiiEHCIRootHubIntTransaction lastTransaction;
  IOUSBHubStatus rootHubStatus;
  IOUSBHubPortStatus portStatus;
  bool haveTransaction;
  UInt16 statusChangedBitmap;
  UInt32 bufferLengthDelta;

  bzero(&lastTransaction, sizeof (lastTransaction));
  haveTransaction = false;
  statusChangedBitmap = 0;
  if (!abort) {
    if (GetRootHubStatus(&rootHubStatus) != kIOReturnSuccess) {
      return;
    }

    rootHubStatus.changeFlags = USBToHostWord(rootHubStatus.changeFlags);
    if (rootHubStatus.changeFlags != 0) {
      statusChangedBitmap |= 1;
    }

    for (UInt16 port = 1; port <= _numPorts; port++) {
      if (GetRootHubPortStatus(&portStatus, port) != kIOReturnSuccess) {
        continue;
      }
      portStatus.changeFlags = USBToHostWord(portStatus.changeFlags);
      if (portStatus.changeFlags != 0) {
        statusChangedBitmap |= (1 << port);
      }
    }

    statusChangedBitmap = HostToUSBWord(statusChangedBitmap);
  }

  if (abort || (statusChangedBitmap != 0)) {
    IOLockLock(_rootHubInterruptTransLock);
    if (_rootHubInterruptTransactions[0].completion.action != NULL) {
      lastTransaction = _rootHubInterruptTransactions[0];
      for (unsigned int i = 1; i < ARRSIZE(_rootHubInterruptTransactions); i++) {
        _rootHubInterruptTransactions[i - 1] = _rootHubInterruptTransactions[i];
      }
      bzero(&_rootHubInterruptTransactions[ARRSIZE(_rootHubInterruptTransactions) - 1],
        sizeof (_rootHubInterruptTransactions[0]));
      haveTransaction = true;
    }
    IOLockUnlock(_rootHubInterruptTransLock);
  }

  if (haveTransaction) {
    bufferLengthDelta = lastTransaction.bufferLength;
    if (bufferLengthDelta > sizeof (statusChangedBitmap)) {
      bufferLengthDelta = sizeof (statusChangedBitmap);
    }
    if (_numPorts < 8) {
      bufferLengthDelta = 1;
    }

    lastTransaction.buffer->writeBytes(0, &statusChangedBitmap, bufferLengthDelta);
    Complete(lastTransaction.completion, abort ? kIOReturnAborted : kIOReturnSuccess, lastTransaction.bufferLength - bufferLengthDelta);
  }
}