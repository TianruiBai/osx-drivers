//
//  WiiSDHC_Private.cpp
//  Wii SD host controller interface
//
//  Copyright © 2025 John Davis. All rights reserved.
//

#include "WiiSDHC.hpp"

static IOReturn WiiSDHCStatusFromError(UInt16 errorStatus) {
  if (errorStatus & (kSDHCRegErrorIntStatusCommandTimeout | kSDHCRegErrorIntStatusDataTimeout)) {
    return kIOReturnTimeout;
  }

  if (errorStatus != 0) {
    return kIOReturnIOError;
  }

  return kIOReturnSuccess;
}

void WiiSDHC::serviceInterruptStatus(UInt32 rawIntStatus) {
  UInt16 normalIntStatus;
  UInt16 errorIntStatus;

  if (rawIntStatus == 0) {
    return;
  }

  normalIntStatus = (UInt16)(rawIntStatus & 0xFFFF);
  errorIntStatus = (UInt16)(rawIntStatus >> 16);

  WIIDBGLOG("Got interrupt raw=0x%08X normal=0x%04X error=0x%04X", rawIntStatus, normalIntStatus, errorIntStatus);

  writeReg32(kSDHCRegNormalIntStatus, rawIntStatus);
  (void) readReg32(kSDHCRegNormalIntStatus);

  if (_currentCommand != NULL) {
    _commandTimeoutRemainingUS = kSDHCCommandTimeoutMS;
    doAsyncIO(normalIntStatus, errorIntStatus);
  } else {
    WIISYSLOG("Spurious SDHC interrupt with no active command: normal=0x%X error=0x%X", normalIntStatus, errorIntStatus);
  }
}

//
// Handles interrupts.
//
// This function must only be called within the work loop context.
//
void WiiSDHC::handleInterrupt(IOInterruptEventSource *intEventSource, int count) {
  (void) intEventSource;
  (void) count;

  serviceInterruptStatus(readReg32(kSDHCRegNormalIntStatus));
}

void WiiSDHC::commandWatchdogFired(IOTimerEventSource *timerSource) {
  UInt32 rawIntStatus;
  UInt16 errorIntStatus;
  IOReturn status;

  (void) timerSource;

  if (_currentCommand == NULL) {
    return;
  }

  rawIntStatus = readReg32(kSDHCRegNormalIntStatus);
  if (rawIntStatus != 0) {
    serviceInterruptStatus(rawIntStatus);
    if (_currentCommand == NULL) {
      return;
    }
  }

  if (_commandTimeoutRemainingUS <= 1000) {
    rawIntStatus = readReg32(kSDHCRegNormalIntStatus);
    errorIntStatus = (UInt16)(rawIntStatus >> 16);
    status = WiiSDHCStatusFromError(errorIntStatus);
    if (status == kIOReturnSuccess) {
      status = kIOReturnTimeout;
    }

    WIISYSLOG("Command watchdog timed out on CMD%u: ps=0x%X rawInt=0x%08X clock=0x%X power=0x%X hostctl=0x%X arg=0x%X",
      _currentCommand->getCommandIndex(),
      readReg32(kSDHCRegPresentState),
      rawIntStatus,
      readReg16(kSDHCRegClockControl),
      readReg8(kSDHCRegPowerControl),
      readReg8(kSDHCRegHostControl1),
      readReg32(kSDHCRegArgument));

    resetController(kSDHCRegSoftwareResetCmd | kSDHCRegSoftwareResetDat);
    _currentCommand->state = kWiiSDCommandStateComplete;
    completeIO(status);
    return;
  }

  _commandTimeoutRemainingUS -= 1000;
  _commandTimerSource->setTimeoutUS(1000);
}

//
// Handles completion of an async read/write IO operation.
//
void WiiSDHC::handleAsyncReadWriteCompletion(WiiSDCommand *command) {
  IOStorageCompletion completion;
  IOReturn            status;
  UInt64              byteCount;

  status      = command->getStatus();
  byteCount   = command->getBlockCount() * kSDBlockSize;
  completion  = command->getStorageCompletion();

  command->release();

  WIIDBGLOG("Async completion here 0x%llX, status 0x%X", byteCount, status);

  //
  // Call the async completion.
  //
  if (completion.action != NULL) {
    (completion.action)(completion.target, completion.parameter, status, byteCount);
  }
}

//
// Resets the controller.
//
IOReturn WiiSDHC::resetController(UInt8 bits) {
  UInt32 timeout;

  WIIDBGLOG("Resetting host controller with bits 0x%X", bits);
  writeReg8(kSDHCRegSoftwareReset, bits);

  //
  // Wait for bits to clear.
  //
  for (timeout = kSDHCResetTimeoutMS; timeout > 0; timeout--) {
    if ((readReg8(kSDHCRegSoftwareReset) & bits) == 0) {
      break;
    }
    IODelay(1);
  }

  if (timeout == 0) {
    WIISYSLOG("Timed out waiting for reset bits to clear: 0x%X", readReg8(kSDHCRegSoftwareReset));
    return kIOReturnTimeout;
  }

  WIIDBGLOG("Host controller is now reset");
  return kIOReturnSuccess;
}

//
// Initializes the controller.
//
IOReturn WiiSDHC::initController(void) {
  IOReturn status;

  //
  // Reset controller completely.
  //
  status = resetController(kSDHCRegSoftwareResetAll);
  if (status != kIOReturnSuccess) {
    return status;
  }

  //
  // Set controller parameters.
  //
  writeReg8(kSDHCRegTimeoutControl, 0xE);

  //
  // Enable all interrupts, but only signal for command-related interrupts.
  //
  writeReg16(kSDHCRegNormalIntStatusEnable, -1);
  writeReg16(kSDHCRegErrorIntStatusEnable, -1);
  writeReg16(kSDHCRegNormalIntSignalEnable, -1);
  writeReg16(kSDHCRegErrorIntSignalEnable, -1);

  return kIOReturnSuccess;
}

//
// Sets the controller clock rate.
//
IOReturn WiiSDHC::setControllerClock(UInt32 speedHz) {
  UInt32 timeout;

  //
  // Clear existing clock register.
  //
  writeReg16(kSDHCRegClockControl, 0);
  if (speedHz == 0) {
    return kIOReturnSuccess;
  }

  //
  // Get base clock speed.
  //
  UInt32 hcCaps    = readReg32(kSDHCRegCapabilities);
  UInt32 baseClock = getControllerVersion() >= kSDHCVersion3_00 ?
    (hcCaps & kSDHCRegCapabilitiesBaseClockMaskVer3) >> kSDHCRegCapabilitiesBaseClockShift :
    (hcCaps & kSDHCRegCapabilitiesBaseClockMaskVer1) >> kSDHCRegCapabilitiesBaseClockShift;
  baseClock *= MHz;
  WIIDBGLOG("Caps: 0x%X", hcCaps);
  WIIDBGLOG("Base clock is %u MHz", baseClock / MHz);

  //
  // Calculate clock divisor.
  //
  UInt32 clockDiv;
  for (clockDiv = 1; (baseClock / clockDiv) > speedHz; clockDiv <<= 1);
  WIIDBGLOG("Clock will be set to %u %s using divisor %u",
            speedHz >= MHz ? (baseClock / clockDiv) / MHz : (baseClock / clockDiv) / kHz,
            speedHz >= MHz ? "MHz" : "kHz", clockDiv);

  //
  // Set clock divisor and enable internal clock.
  //
  UInt16 newClockDiv = ((clockDiv << kSDHCRegClockControlFreqSelectLowShift) & kSDHCRegClockControlFreqSelectLowMask)
    | ((clockDiv >> kSDHCRegClockControlFreqSelectHighRhShift) & kSDHCRegClockControlFreqSelectHighMask);
  writeReg16(kSDHCRegClockControl, readReg16(kSDHCRegClockControl) | newClockDiv | kSDHCRegClockControlIntClockEnable);

  //
  // Wait for clock to be stable.
  //
  for (timeout = kSDHCClockTimeoutMS; timeout > 0; timeout--) {
    if ((readReg16(kSDHCRegClockControl) & kSDHCRegClockControlIntClockStable) != 0) {
      break;
    }
    IODelay(1);
  }

  if (timeout == 0) {
    WIISYSLOG("Timed out waiting for clock to become stable");
    return kIOReturnTimeout;
  }
  WIIDBGLOG("Clock is now stable");

  //
  // Enable clock to card.
  //
  writeReg16(kSDHCRegClockControl, readReg16(kSDHCRegClockControl) | kSDHCRegClockControlSDClockEnable);
  WIIDBGLOG("Clock control register is now 0x%X", readReg16(kSDHCRegClockControl));
  IOSleep(50);

  return kIOReturnSuccess;
}

//
// Sets the card voltage used by the controller.
//
void WiiSDHC::setControllerPower(bool enabled) {
  //
  // Clear power register.
  //
  writeReg16(kSDHCRegPowerControl, 0);
  if (!enabled) {
    return;
  }

  //
  // Get highest supported card voltage and enable it.
  //
  UInt32 hcCaps      = readReg32(kSDHCRegCapabilities);
  UInt8  powerControl = readReg8(kSDHCRegPowerControl);
  if (hcCaps & kSDHCRegCapabilitiesVoltage3_3Supported) {
    powerControl |= kSDHCRegPowerControlVDD1_3_3;
    WIIDBGLOG("Card voltage: 3.3V");
  } else if (hcCaps & kSDHCRegCapabilitiesVoltage3_0Supported) {
    powerControl |= kSDHCRegPowerControlVDD1_3_0;
    WIIDBGLOG("Card voltage: 3.0V");
  } else if (hcCaps & kSDHCRegCapabilitiesVoltage1_8Supported) {
    powerControl |= kSDHCRegPowerControlVDD1_1_8;
    WIIDBGLOG("Card voltage: 1.8V");
  }
  writeReg8(kSDHCRegPowerControl, powerControl);

  //
  // Turn power on to card.
  //
  writeReg8(kSDHCRegPowerControl, readReg8(kSDHCRegPowerControl) | kSDHCRegPowerControlVDD1On);
  WIIDBGLOG("Card power control register is now 0x%X", readReg8(kSDHCRegPowerControl));
  IOSleep(50);
}

//
// Sets the controller bus width bits.
//
void WiiSDHC::setControllerBusWidth(SDBusWidth busWidth) {
  UInt8 hcControl = readReg8(kSDHCRegHostControl1) & ~kSDHCRegHostControl1DataWidthMask;
  if (busWidth == kSDBusWidth4) {
    hcControl |= kSDHCRegHostControl1DataWidth4Bit;
    WIIDBGLOG("Setting controller bus width to 4-bit mode");
  } else if (busWidth == kSDBusWidth8) {
    hcControl |= kSDHCRegHostControl1DataWidth8Bit;
    WIIDBGLOG("Setting controller bus width to 8-bit mode");
  } else {
    WIIDBGLOG("Setting controller bus width to 1-bit mode");
  }
  writeReg8(kSDHCRegHostControl1, hcControl);
}
