//
//  WiiOHCI_Interrupts.cpp
//  Wii OHCI USB controller interface
//
//  Copyright © 2025 John Davis. All rights reserved.
//

#include "WiiOHCI.hpp"

void WiiOHCI::queueDoneHeadTransfers(IOPhysicalAddress doneHeadPhysAddr, bool *signalSecondaryInt) {
  OHCITransferData  *newDoneHeadTransfer;
  OHCITransferData  *newIsoInHeadTransfer;
  AbsoluteTime      timeStamp;
  UInt16            frameCount;
  UInt16            pktOffStatus;
  OHCITransferData  *tailTransfer;
  OHCITransferData  *tailIsoInTransfer;
  OHCITransferData  *currTransfer;

  if (doneHeadPhysAddr == 0) {
    return;
  }

#if defined(WII_TIGER_SDK)
  UInt64 timeStampValue;
  clock_get_uptime(&timeStampValue);
  timeStamp.hi = static_cast<UInt32>(timeStampValue >> 32);
  timeStamp.lo = static_cast<UInt32>(timeStampValue & 0xFFFFFFFFULL);
#else
  clock_get_uptime(&timeStamp);
#endif

  newDoneHeadTransfer   = NULL;
  newIsoInHeadTransfer  = NULL;
  tailTransfer          = NULL;
  tailIsoInTransfer     = NULL;
  currTransfer          = getTransferFromPhys(doneHeadPhysAddr);
  while (currTransfer != NULL) {
    if (currTransfer->type == kOHCITransferTypeIsochronousLowLatency) {
      frameCount = ((USBToHostLong(currTransfer->isoTD->flags) & kOHCIIsoTDFlagsFrameCountMask) >> kOHCIIsoTDFlagsFrameCountShift) + 1;
      for (UInt16 i = 0; i < frameCount; i++) {
        pktOffStatus = USBToHostWord(currTransfer->isoTD->packetOffsetStatus[i]);

        currTransfer->isoLowFrames[currTransfer->isoFrameIndex + i].frTimeStamp = timeStamp;
        if (((pktOffStatus & kOHCIIsoTDPktOffsetConditionCodeMask) >> kOHCIIsoTDPktOffsetConditionCodeShift) == kOHCITDConditionCodeNotAccessedPSW) {
          currTransfer->isoLowFrames[currTransfer->isoFrameIndex + i].frStatus   = convertTDStatus(kOHCITDConditionCodeNotAccessed);
          currTransfer->isoLowFrames[currTransfer->isoFrameIndex + i].frActCount = 0;
        } else {
          currTransfer->isoLowFrames[currTransfer->isoFrameIndex + i].frStatus = convertTDStatus((pktOffStatus & kOHCIIsoTDPktStatusConditionCodeMask) >> kOHCIIsoTDPktStatusConditionCodeShift);
          if ((currTransfer->isoLowFrames[currTransfer->isoFrameIndex + i].frStatus == kIOReturnSuccess) && (currTransfer->direction == kUSBOut)) {
            currTransfer->isoLowFrames[currTransfer->isoFrameIndex + i].frActCount = currTransfer->isoLowFrames[currTransfer->isoFrameIndex + i].frReqCount;
          } else {
            currTransfer->isoLowFrames[currTransfer->isoFrameIndex + i].frActCount = pktOffStatus & kOHCIIsoTDPktStatusSizeMask;
          }
        }
      }
    }

    if (((currTransfer->type == kOHCITransferTypeIsochronous) || (currTransfer->type == kOHCITransferTypeIsochronousLowLatency)) && (currTransfer->direction == kUSBIn)) {
      currTransfer->nextTransfer = newIsoInHeadTransfer;
      newIsoInHeadTransfer       = currTransfer;

      if (tailIsoInTransfer == NULL) {
        tailIsoInTransfer = currTransfer;
      }
    } else {
      currTransfer->nextTransfer = newDoneHeadTransfer;
      newDoneHeadTransfer        = currTransfer;

      if (tailTransfer == NULL) {
        tailTransfer = currTransfer;
      }
    }

    currTransfer = getTransferFromPhys(USBToHostLong(currTransfer->genTD->nextTDPhysAddr));
  }

  if (newDoneHeadTransfer != NULL) {
    IOSimpleLockLock(_writeDoneHeadLock);

    tailTransfer->nextTransfer = (OHCITransferData*) _writeDoneHeadPtr;
    _writeDoneHeadPtr = newDoneHeadTransfer;
    _intWriteDoneHead = true;

    IOSimpleLockUnlock(_writeDoneHeadLock);

    if (signalSecondaryInt != NULL) {
      *signalSecondaryInt = true;
    }
  }

  if (newIsoInHeadTransfer != NULL) {
    IOSimpleLockLock(_isoInHeadLock);

    tailIsoInTransfer->nextTransfer = (OHCITransferData*) _isoInHeadPtr;
    _isoInHeadPtr = newIsoInHeadTransfer;

    IOSimpleLockUnlock(_isoInHeadLock);
  }
}
//
// Interrupt handler filter function.
//
// This function runs in the primary interrupt handler context and should be as simple as possible.
// This may run concurrently with the secondary handler and any workloop functions.
//
bool WiiOHCI::filterInterrupt(IOFilterInterruptEventSource *filterIntEventSource) {
  UInt32            intEnable;
  UInt32            intStatus;

  IOInterruptState  intState;
  IOPhysicalAddress newWriteDoneHeadPhysAddr;
  bool              signalSecondaryInt;

  intEnable = readReg32(kOHCIRegIntEnable);
  intStatus = intEnable & readReg32(kOHCIRegIntStatus);

  //
  // Only handle interrupts if they are enabled, and if it was one that was enabled. TODO: Handle the error ones.
  //
  if (((intEnable & kOHCIRegIntEnableMasterInterruptEnable) == 0) || (intStatus == 0)) {
    return false;
  }
  signalSecondaryInt = false;

  //
  // Scheduling overrun.
  // Clear and move on.
  //
  if (intStatus & kOHCIRegIntStatusSchedulingOverrun) {
    writeReg32(kOHCIRegIntStatus, kOHCIRegIntStatusSchedulingOverrun);
    OSSynchronizeIO();
  }

  //
  // Done queue head written.
  // Process the done queue.
  //
  if (intStatus & kOHCIRegIntStatusWritebackDoneHead) {
    if (_invalidateCacheFunc != NULL) {
      _invalidateCacheFunc((vm_offset_t) _hccaPtr, sizeof (*_hccaPtr), false);
    }
    OSSynchronizeIO();
    newWriteDoneHeadPhysAddr = USBToHostLong(_hccaPtr->doneHeadPhysAddr) & kOHCIRegDoneHeadMask;
    _hccaPtr->doneHeadPhysAddr = 0;
    writeReg32(kOHCIRegIntStatus, kOHCIRegIntStatusWritebackDoneHead);
    OSSynchronizeIO();

    //
    // Reverse the queue and get the pointers to transfer data.
    // The host controller links the newest descriptors to the head of the queue.
    //
    queueDoneHeadTransfers(newWriteDoneHeadPhysAddr, &signalSecondaryInt);
  }

  //
  // Start of frame.
  // Clear/disable and move on.
  //
  if (intStatus & kOHCIRegIntStatusStartOfFrame) {
    writeReg32(kOHCIRegIntStatus, kOHCIRegIntStatusStartOfFrame);
    OSSynchronizeIO();

    writeReg32(kOHCIRegIntDisable, kOHCIRegIntDisableStartOfFrame);
    OSSynchronizeIO();
  }

  //
  // Resume detected.
  //
  if (intStatus & kOHCIRegIntStatusResumeDetected) {

    writeReg32(kOHCIRegIntStatus, kOHCIRegIntStatusResumeDetected);
    OSSynchronizeIO();

    intState = IOSimpleLockLockDisableInterrupt(_intRootHubStatusLock);
    _intResumeDetected = true;
    IOSimpleLockUnlockEnableInterrupt(_intRootHubStatusLock, intState);
    signalSecondaryInt = true;
  }

  //
  // Unrecoverable error.
  //
  if (intStatus & kOHCIRegIntStatusUnrecoverableError) {
    writeReg32(kOHCIRegIntStatus, kOHCIRegIntStatusUnrecoverableError);
    OSSynchronizeIO();

    intState = IOSimpleLockLockDisableInterrupt(_intRootHubStatusLock);
    _intUnrecoverableError = true;
    IOSimpleLockUnlockEnableInterrupt(_intRootHubStatusLock, intState);
    signalSecondaryInt     = true;
  }

  //
  // Frame number overflow.
  // Increment frame number counter.
  //
  if (intStatus & kOHCIRegIntEnableFrameNumberOverflow) {
    if (USBToHostWord(_hccaPtr->frameNumber) < BIT15) {
      _frameNumber += BIT16;
    }
    writeReg32(kOHCIRegIntStatus, kOHCIRegIntEnableFrameNumberOverflow);
    OSSynchronizeIO();
  }

  //
  // Root hub status change.
  //
  if (intStatus & kOHCIRegIntStatusRootHubStatusChange) {
    writeReg32(kOHCIRegIntDisable, kOHCIRegIntDisableRootHubStatusChange);
    writeReg32(kOHCIRegIntStatus, kOHCIRegIntStatusRootHubStatusChange);
    OSSynchronizeIO();

    intState = IOSimpleLockLockDisableInterrupt(_intRootHubStatusLock);
    _intRootHubStatus = true;
    IOSimpleLockUnlockEnableInterrupt(_intRootHubStatusLock, intState);

    signalSecondaryInt = true;
  }

  //
  // Ownership change.
  // Should never occur.
  //
  if (intStatus & kOHCIRegIntStatusOwnershipChange) {
    writeReg32(kOHCIRegIntStatus, kOHCIRegIntStatusOwnershipChange);
    OSSynchronizeIO();
  }

  //
  // Signal the secondary handler manually so the primary is never disabled.
  // Need to keep it moving for various low latency operations.
  //
  if (signalSecondaryInt) {
    _interruptEventSource->signalInterrupt();
  }
  return false;
}

//
// Handles interrupts.
//
// This function is gated and called within the workloop context.
//
void WiiOHCI::handleInterrupt(IOInterruptEventSource *intEventSource, int count) {
  IOInterruptState  intState;
  volatile OHCITransferData  *newDoneTransfer;
  bool resumeDetected;
  bool rootHubStatusChanged;
  bool unrecoverableError;
  bool writeDoneHeadPending;

  (void) intEventSource;
  (void) count;

  newDoneTransfer = NULL;
  resumeDetected = false;
  writeDoneHeadPending = false;
  rootHubStatusChanged = false;
  unrecoverableError = false;

  intState = IOSimpleLockLockDisableInterrupt(_writeDoneHeadLock);
  if (_intWriteDoneHead || (_writeDoneHeadPtr != NULL)) {
    writeDoneHeadPending = true;
    _intWriteDoneHead = false;
    newDoneTransfer   = _writeDoneHeadPtr;
    _writeDoneHeadPtr = NULL;
  }
  IOSimpleLockUnlockEnableInterrupt(_writeDoneHeadLock, intState);

  intState = IOSimpleLockLockDisableInterrupt(_intRootHubStatusLock);
  resumeDetected = _intResumeDetected;
  _intResumeDetected = false;
  unrecoverableError = _intUnrecoverableError;
  _intUnrecoverableError = false;
  rootHubStatusChanged = _intRootHubStatus;
  _intRootHubStatus = false;
  IOSimpleLockUnlockEnableInterrupt(_intRootHubStatusLock, intState);

  WIIDBGLOG("Interrupt: WH: %u, RD: %u, UE: %u, RH: %u", writeDoneHeadPending ? 1 : 0,
    resumeDetected ? 1 : 0, unrecoverableError ? 1 : 0, rootHubStatusChanged ? 1 : 0);

  if (unrecoverableError) {
    WIISYSLOG("OHCI unrecoverable error interrupt, disabling master interrupt");
    writeReg32(kOHCIRegIntDisable, kOHCIRegIntDisableMasterInterruptEnable);
    OSSynchronizeIO();
  }

  //
  // Done queue head written.
  //
  if (newDoneTransfer != NULL) {
    completeTransferQueue((OHCITransferData*) newDoneTransfer);
  }

  //
  // Resume-detect should also drive the root hub polling path, even if RHSC was not asserted.
  //
  if (resumeDetected || rootHubStatusChanged) {
    UIMRootHubStatusChange();
  }
}

void WiiOHCI::handleWatchdog(IOTimerEventSource *sender) {
  IOPhysicalAddress doneHeadPhysAddr;
  IOInterruptState intState;
  UInt32 intStatus;
  bool doneHeadPending;
  bool signalSecondaryInt;
  bool rootHubPending;

  (void) sender;

  if (_baseAddr == NULL) {
    return;
  }

  signalSecondaryInt = false;
  intStatus = readReg32(kOHCIRegIntStatus);
  intState = IOSimpleLockLockDisableInterrupt(_writeDoneHeadLock);
  doneHeadPending = _intWriteDoneHead || (_writeDoneHeadPtr != NULL);
  IOSimpleLockUnlockEnableInterrupt(_writeDoneHeadLock, intState);
  if (((intStatus & kOHCIRegIntStatusWritebackDoneHead) != 0) && !doneHeadPending) {
    OSSynchronizeIO();
    doneHeadPhysAddr = USBToHostLong(_hccaPtr->doneHeadPhysAddr) & kOHCIRegDoneHeadMask;
    if (doneHeadPhysAddr != 0) {
      _hccaPtr->doneHeadPhysAddr = 0;
      OSSynchronizeIO();
      writeReg32(kOHCIRegIntStatus, kOHCIRegIntStatusWritebackDoneHead);
      OSSynchronizeIO();
      queueDoneHeadTransfers(doneHeadPhysAddr, &signalSecondaryInt);
    }
  }

  IOLockLock(_rootHubInterruptTransLock);
  rootHubPending = (_rootHubInterruptTransactions[0].completion.action != NULL);
  IOLockUnlock(_rootHubInterruptTransLock);
  if (rootHubPending) {
    completeRootHubInterruptTransfer(false);
  }

  if (signalSecondaryInt && (_interruptEventSource != NULL)) {
    _interruptEventSource->signalInterrupt();
  }
  if (_watchdogEventSource != NULL) {
    _watchdogEventSource->setTimeoutUS(kWiiOHCIWatchdogRefreshUS);
  }
}

//
// Handles isochronous inbound timer events.
//
// This function is not called within the regular workloop context.
// This timer is running when isochronous endpoints are present.
//
void WiiOHCI::handleIsoInTimer(IOTimerEventSource *sender) {
  IOInterruptState  intState;
  OHCITransferData  *currTransfer;
  OHCITransferData  *prevTransfer;
  OHCITransferData  *headIsoInTransfer;

  //
  // Get the current list of the inbound isochronous transfers.
  //
  intState = IOSimpleLockLockDisableInterrupt(_isoInHeadLock);

  headIsoInTransfer = (OHCITransferData*) _isoInHeadPtr;
  _isoInHeadPtr     = NULL;

  IOSimpleLockUnlockEnableInterrupt(_isoInHeadLock, intState);

  //
  // Iterate through the chain and copy data back to the source buffers.
  //
  prevTransfer = NULL;
  currTransfer = headIsoInTransfer;
  while (currTransfer != NULL) {
    if (currTransfer->srcBuffer != NULL) {
      _invalidateCacheFunc((vm_offset_t) currTransfer->bounceBuffer->buf, currTransfer->actualBufferSize, false);
      currTransfer->srcBuffer->writeBytes(0, currTransfer->bounceBuffer->buf, currTransfer->isoFrames[currTransfer->isoFrameIndex].frActCount);
    }

    prevTransfer = currTransfer;
    currTransfer = currTransfer->nextTransfer;
  }

  //
  // Link to the main completed transfer chain.
  //
  if (headIsoInTransfer != NULL) {
    intState = IOSimpleLockLockDisableInterrupt(_writeDoneHeadLock);

    prevTransfer->nextTransfer = (OHCITransferData*) _writeDoneHeadPtr;
    _writeDoneHeadPtr          = (OHCITransferData*)  headIsoInTransfer;
    _intWriteDoneHead          = true;

    IOSimpleLockUnlockEnableInterrupt(_writeDoneHeadLock, intState);

    //
    // Signal the main handler there are new completed transfers.
    //
    _interruptEventSource->signalInterrupt();
  }

  _isoInTimerEventSource->setTimeoutUS(kWiiOHCIIsoTimerRefreshUS);
}

//
// Handles isochronous outbound timer events.
//
// This function is not called within the regular workloop context.
// This timer is running when isochronous endpoints are present.
// The timer is stopped/started when the endpoint list has items removed.
//
void WiiOHCI::handleIsoOutTimer(IOTimerEventSource *sender) {
  UInt16            hcFrameNumber;
  OHCIEndpointData  *currEndpoint;
  OHCITransferData  *currTransfer;

  //
  // Iterate through each outbound isochronous endpoint and check for transfer descriptors that are about to be sent.
  //
  currEndpoint = _isoEndpointHeadPtr;
  while (currEndpoint != _isoEndpointTailPtr) {
    currTransfer = getTransferFromPhys(USBToHostLong(currEndpoint->ed->headTDPhysAddr) & kOHCIEDTDHeadMask);
    if (currTransfer == NULL) {
      currEndpoint = currEndpoint->nextEndpoint;
      continue;
    }

    //
    // Iterate through each transfer descriptor.
    //
    while (currTransfer != currEndpoint->transferTail) {
      //
      // Check if transfer hasn't already been copied, and is going to be transferred shortly if outbound.
      //
      hcFrameNumber = USBToHostWord(_hccaPtr->frameNumber);
      if ((currTransfer->direction == kUSBOut) && !currTransfer->isoBufferCopied && (currTransfer->isoFrameStart > hcFrameNumber)) { // TODO: Better calculation here
        if ((currTransfer->isoFrameStart - hcFrameNumber) < 3) {
          if (currTransfer->srcBuffer != NULL) {
            currTransfer->srcBuffer->readBytes(0, currTransfer->bounceBuffer->buf, currTransfer->actualBufferSize);
            flushDataCache(currTransfer->bounceBuffer->buf, currTransfer->actualBufferSize);
          }
          currTransfer->isoBufferCopied = true;
        }
      }

      currTransfer = currTransfer->nextTransfer;
    }

    currEndpoint = currEndpoint->nextEndpoint;
  }

  _isoOutTimerEventSource->setTimeoutUS(kWiiOHCIIsoTimerRefreshUS);
}

//
// Overrides IOUSBController::PollInterrupts().
//
void WiiOHCI::PollInterrupts(IOUSBCompletionAction safeAction) {
  (void) safeAction;
  handleWatchdog(NULL);
}
