# Wii U USB EHCI Investigation

## Current Driver State

- `WiiUSB` now builds both `src/OHCI` and a provisional `src/EHCI` controller.
- `WiiUSB/Info.plist` exposes the existing OHCI personality plus a temporary EHCI personality for bring-up.
- The current EHCI code no longer stops at probe-only logging. It now performs a minimal halt/reset/run sequence, simulates the EHCI root hub, translates hardware port status into `IOUSBHubPortStatus`, forces `CONFIGFLAG` so EHCI can actually own ports, and hands a port back to the companion controller if reset completes without high-speed enable.

## Verified Hardware Map

The WiiUBrew USB host-controller page documents the following register blocks, all with `0x10000` bytes of MMIO space:

- `EHCI-0` at `0x0d040000`
- `OHCI-0:0` at `0x0d050000`
- `OHCI-0:1` at `0x0d060000`
- `EHCI-1` at `0x0d120000`
- `OHCI-1:0` at `0x0d130000`
- `EHCI-2` at `0x0d140000`
- `OHCI-2:0` at `0x0d150000`

These values are now mirrored in `include/WiiUSBHost.hpp` so future probe code does not need to copy raw WiiUBrew constants.

Cross-check against the working Linux Wii U tree on branch `rewrite-6.6` confirms the same published controller shape in `arch/powerpc/boot/dts/wiiu.dts`:

- EHCI nodes use `compatible = "nintendo,latte-ehci"`
- OHCI nodes use `compatible = "nintendo,latte-ohci"`
- each visible USB host node publishes `reg = <... 0x100>` rather than `0x10000`
- all Wii U USB host nodes carry `big-endian-regs`

## Verified Interrupt Topology

The Latte IRQ documentation splits vectors into two 32-bit banks.

- Shared `INT1` vectors:
  - `EHCI0` on bit `4`
  - `OHCI0:0` on bit `5`
  - `OHCI0:1` on bit `6`
- Latte-only `INT2` vectors:
  - `EHCI1` on bit `32 + 2`
  - `OHCI1:0` on bit `32 + 3`
  - `EHCI2` on bit `32 + 4`
  - `OHCI2:0` on bit `32 + 5`

On Wii U, `LatteInterruptController` already routes both banks to Espresso through the Latte interrupt controller and ultimately into Processor Interface IRQ `12`.

For the Wii/Hollywood path, the same shared USB vectors exist on the single 32-bit Hollywood interrupt bank:

- `EHCI` on bit `4`
- `OHCI0:0` on bit `5`
- `OHCI0:1` on bit `6`

These vector numbers are now named in `include/WiiHollywood.hpp` and `WiiPlatform/src/Interrupts/LatteRegs.hpp`.

## Tiger USB Family Constraint

Tiger exposes high-speed USB support through `IOUSBControllerV2`, not just `IOUSBController`.

The `IOUSBControllerV2.h` header in the validated Tiger guest makes three architectural requirements clear:

- EHCI must subclass `IOUSBControllerV2` to support high-speed devices.
- Split transactions for full-speed and low-speed devices behind a high-speed hub are modeled through `highSpeedHub` and `highSpeedPort` parameters on endpoint-creation methods.
- The family expects high-speed hub maintenance hooks such as `AddHSHub`, `RemoveHSHub`, and `UIMHubMaintenance`.

That means a future EHCI driver cannot be a small extension of `WiiOHCI`. It needs its own controller class with the `IOUSBControllerV2` surface.

## Practical Bring-Up Plan

### Phase 1: Provider Discovery

- Confirm the actual device-tree node names and interrupt specifiers used for EHCI on the Wii U boot path.
- Do not guess the `IONameMatch` string for EHCI personalities.
- Verify whether the boot environment exposes one EHCI node or all three.

## Current Tree Findings

The attached plain `ioreg` tree in `doc/iodevicetree.txt` is already enough to narrow the first EHCI target significantly.

Visible USB host-controller nodes under `WiiPE` are:

- `usb@d040000`
- `usb@d050000`
- `usb@d060000`
- `usb@d130000`
- `usb@d150000`

Observed bindings from that same tree:

- `usb@d050000` already binds `WiiOHCI`
- `usb@d130000` already binds `WiiOHCI`
- `usb@d040000` has no child driver and is the first EHCI candidate
- `usb@d060000` and `usb@d150000` are present as platform devices but currently have no matched USB driver child

That unbound-companion state matters for behavior, not just bookkeeping. If EHCI hands a full-speed or low-speed attach back to its companion controller, but the relevant OHCI companion node never bound a driver, the device appears to disappear rather than re-enumerate. On the current Wii U tree, `usb@d060000` is the most likely missing companion for the remaining `EHCI-0` ports.

Important negative result:

- there is no visible `usb@d120000`
- there is no visible `usb@d140000`

So for the current boot path, the correct first EHCI target is not ambiguous anymore:

- start with the published node at `usb@d040000`

This also means the current boot tree does not expose all EHCI blocks documented on WiiUBrew, at least not in the plain registry view you captured.
That is why the first implementation should target only the visible `d040000` controller and treat any later `d120000` or `d140000` support as follow-up work.

One more important limitation from this tree:

- the tree shows the registry entry path, but not the raw property values

That means this file alone tells us the provider path and the MMIO identity, but it does not yet tell us:

- the exact `name` property value,
- the `compatible` list,
- the raw `interrupts` property,
- the `interrupt-parent` property.

So this tree is enough to identify the first EHCI provider node, but not enough by itself to derive a safe `IONameMatch` string.

## Temporary Instrumentation Now In Tree

Two temporary pieces of code are now present to make the next boot materially more useful.

### WiiPE USB Property Dumper

`WiiPE` now dumps the following device-tree nodes during boot on Wii U:

- `/usb@d040000`
- `/usb@d050000`
- `/usb@d060000`
- `/usb@d120000`
- `/usb@d130000`
- `/usb@d140000`
- `/usb@d150000`

For each node, it logs:

- registry entry name,
- registry location,
- raw `name`,
- raw `compatible`,
- raw `reg`,
- raw `interrupts`,
- raw `AAPL,interrupts`,
- raw `interrupt-parent`,
- raw `device_type`,
- raw `assigned-addresses`.

This is now the primary mechanism for discovering the exact EHCI match string and interrupt properties on your boot path.

### Provisional `WiiEHCI` Scaffold

`WiiUSB` now contains a new `WiiEHCI` class derived from `IOUSBControllerV2`.

Its current behavior is intentionally conservative:

- the personality advertises a low-score provisional match on `NTDOY,ehci` and `usb`,
- `probe()` then accepts only providers whose location string is `d040000`,
- `UIMInitialize()` maps the controller, logs the raw capability and operational register values, halts and resets the controller, powers and routes ports to EHCI, and then starts it in a minimal root-hub-only mode,
- root hub interrupt transfers are simulated so the family can observe port change bitmaps,
- both the base `IOUSBController` and `IOUSBControllerV2` root-hub endpoint creation paths are now routed into the EHCI root-hub simulation so `AppleUSBHub` can find its interrupt pipe during hub startup,
- low-speed or full-speed devices that do not enable under EHCI are handed back to the companion controller by setting `PORT_OWNER` after reset settles.

The post-reset handoff rule is now stricter than the first draft: a port is only handed back to a companion controller when reset completes, the device is still connected, and the EHCI line-state bits actually look like a full-speed or low-speed attach. A port that merely fails to enable without a valid FS/LS line state is now left under EHCI and logged for diagnosis instead of being rerouted immediately.

That means the next boot should tell us two useful things without risking EHCI taking over a controller prematurely:

- whether the personality actually matches the visible `usb@d040000` node,
- whether the controller can remain attached long enough for the emulated EHCI root hub to appear and report port state.

## Confirmed Live Probe Results

The latest real-hardware boot log confirmed the following on `usb@d040000`:

- `probe()` matched provider location `d040000`,
- device memory index `0` maps physical `0x0d040000`,
- the published MMIO length for the provider is `0x100`, not `0x10000`,
- `CAPLENGTH` is `0x10`,
- HCI version is `0x0100`,
- `HCSPARAMS` is `0x00002406`, which reports `6` ports,
- `USBSTS` came up as `0x00001000`, meaning the controller is halted at boot.

This is enough to justify moving from the original refusal path into an actual halt/reset/run bring-up.

One additional Tiger-specific behavior is now confirmed from the `IOUSBFamily` binary itself:

- `IOUSBController::CreateRootHubDevice` first looks for a provider property named `USBBusNumber`,
- if that property is missing and the provider location does not parse as a PCI location, the family falls back to bus `0`,
- that produces the warning `Bus 0 already taken` when another controller already owns bus `0`.

`WiiEHCI::probe()` now assigns a fixed `USBBusNumber` to the `d040000` provider before the family creates the EHCI root hub, so the next boot should no longer need the bus-0 fallback path.

`WiiOHCI` now does the same for the visible companion controllers. Its personality was broadened to match `usb` platform nodes, then `probe()` filters by known OHCI locations and stamps stable bus numbers for `d050000`, `d060000`, `d130000`, and `d150000`. That should remove the bus-0 fallback for OHCI root hubs too and allow the previously unbound `d060000` and `d150000` companions to attach.

The Linux Wii U EHCI platform driver also applies one Latte-specific quirk before registering the controller with the USB core:

- set bit `15` at EHCI base offset `0x00cc` to enable EHCI interrupt notification on Latte

The current `WiiEHCI` kext now mirrors that step after controller reset and before it depends on EHCI port-change interrupts. This is the strongest Linux-confirmed hardware delta from the earlier macOS scaffold, because without it the controller can expose a root hub yet still miss port-change interrupts that drive re-enumeration and companion handoff timing.

## What The Next Boot Log Should Show

The next test log should ideally contain both of these groups of lines:

- `WiiPE` lines dumping `/usb@d040000` property data,
- `WiiEHCI` lines showing the controller reset/run sequence and the root-hub bring-up.

The most important new lines to look for are:

- `Probing OHCI controller on provider location d060000, assigning USBBusNumber=0x11`,
- `Probing OHCI controller on provider location d150000, assigning USBBusNumber=0x13`,
- `Probing EHCI controller on provider location d040000, assigning USBBusNumber=0x40`,
- `Forced EHCI configflag routing, new configflag=...`,
- `EHCI controller running with 6 ports, configflag=...`,
- `Initial port ... status: ...`,
- any EHCI root hub device attach under `usb@d040000`,
- `Port ... status:` lines showing non-zero connection or change bits,
- `Handed EHCI port ... to companion controller, portsc=...` for low-speed or full-speed devices,
- `Port ... stayed disconnected from EHCI reset without FS/LS line state, portsc=...` for devices that fail to enable but also do not look like valid companion candidates,
- any host-system-error or timeout messages from the new reset/run path,
- absence of `USBF: ... Bus 0 already taken` for either EHCI or OHCI root hub creation.

If the `WiiPE` dump appears but `WiiEHCI` never probes, that means the current personality still does not match the real property set and the dumper output will tell us what to change.

If `WiiEHCI` stays attached and the EHCI root hub appears, the next iteration can move to actual control endpoint and transfer support for high-speed enumeration.

## How To Capture EHCI Provider Information

The key point is that the future EHCI driver will not bind to a hardcoded MMIO address by itself.
Like the current OHCI driver, it will be matched by IOKit against an `IOPlatformDevice` node from the boot device tree.

Three properties matter most:

- `name`: this is the most likely value that will become the EHCI personality's `IONameMatch`.
- `reg`: this tells us which EHCI block the node actually refers to, for example `0x0d040000` for `EHCI-0`.
- `interrupts` or `AAPL,interrupts`: this tells us which vector `provider, 0` will deliver to the driver.

Without the correct `name`, the kext never matches.
Without the correct `interrupts` property, the driver may probe and map registers but never receive completion interrupts.

### Preferred Method: Capture From A Running System With `ioreg`

This is the best path even if only OHCI is currently supported.
EHCI does not need a working driver to appear in `IODeviceTree`; it only needs to exist in the boot device tree.

Start by dumping the full device tree once:

```sh
ioreg -p IODeviceTree -lw0 > /tmp/iodevtree.txt
```

Then search the dump for the known EHCI and OHCI base addresses:

```sh
grep -n -i -E '0d040000|0d120000|0d140000|0d050000|0d060000|0d130000|0d150000' /tmp/iodevtree.txt
```

What you are looking for:

- one node whose `reg` property contains `0d040000` and length `00010000`,
- optionally additional nodes for `0d120000` and `0d140000`,
- nearby properties such as `name`, `compatible`, `interrupts`, `AAPL,interrupts`, and `interrupt-parent`.

After you find a match, print a wider slice around it:

```sh
sed -n 'START,ENDp' /tmp/iodevtree.txt
```

Replace `START` and `END` with about 20 to 40 lines around the match.

If the node name is already obvious, rerun `ioreg` directly on that node for a cleaner dump:

```sh
ioreg -p IODeviceTree -lw0 -r -n NODE_NAME
```

### Useful Cross-Check: Compare Against The Working OHCI Node

Because `WiiOHCI` already loads, the OHCI node gives a reference for how the platform names USB host controllers on your boot path.

Search for the OHCI MMIO blocks too:

```sh
grep -n -i -E '0d050000|0d060000|0d130000|0d150000' /tmp/iodevtree.txt
```

This helps answer questions like:

- does the tree use names like `NTDOY,ohci` and `NTDOY,ehci`,
- does it instead use address-based names such as `usb@0d040000`,
- are EHCI and OHCI siblings under the same parent,
- do they share the same interrupt controller naming style.

### What To Save From The Matching EHCI Node

For each EHCI node you find, save the following exact values:

- the node path,
- the `name` property,
- the `compatible` property if present,
- the full `reg` property bytes,
- the full `interrupts` or `AAPL,interrupts` property bytes,
- the `interrupt-parent` property if present,
- the `device_type` and `assigned-addresses` properties if present.

The raw bytes matter.
For example, an interrupt property that looks like `00000004` means something different from a compound specifier that contains multiple cells.

### If You Can Only Provide A Verbose Boot Log

A boot log is the fallback, not the preferred method.
The limitation is that a stock verbose boot usually does not print every device-tree property automatically.

Still, a verbose boot can help if it includes:

- the device-tree node path for the USB host controller,
- the provider location string,
- any `IONameMatch` or `IOProbeScore` matching attempts,
- any interrupt-controller or interrupt-specifier diagnostics,
- any debug line showing the physical address that a USB host node maps.

If you need to capture this from logs, the most useful addition is a temporary platform-side dump of candidate USB nodes during early boot.
That dump should print the node `name`, `reg`, and `interrupts` properties for anything whose `reg` matches `0x0d040000`, `0x0d120000`, or `0x0d140000`.

### Why This Is Needed Before Writing `WiiEHCI`

The future EHCI driver will likely follow the same provider pattern as `WiiOHCI`:

- `IONameMatch` selects an `IOPlatformDevice`,
- `provider->mapDeviceMemoryWithIndex(0)` maps the first `reg` range,
- `IOFilterInterruptEventSource(..., provider, 0)` attaches to the first advertised interrupt.

So before scaffolding `WiiEHCI`, we need to know three concrete facts from the live device tree:

- what the EHCI provider is called,
- which MMIO range is published as memory index `0`,
- which interrupt specifier is exposed as interrupt index `0`.

Once those are captured, the first EHCI scaffold becomes mechanical instead of speculative.

### Phase 2: First EHCI Controller

- Add `WiiUSB/src/EHCI` and implement a new `WiiEHCI` subclass of `IOUSBControllerV2`.
- Target `EHCI-0` first because it aligns with the already-working OHCI-0 companion pair.
- Bring up controller reset, capability parsing, async schedule, periodic schedule, and a synthesized root hub.

### Phase 3: Companion Coordination

- Wire the existing OHCI companions into the EHCI path for full-speed and low-speed devices.
- Implement split-transaction aware endpoint creation using the `IOUSBControllerV2` APIs.
- Decide whether companion ownership stays implicit in device tree or must be managed explicitly in software.

### Phase 4: Additional Wii U Controllers

- Extend the driver to `EHCI-1` and `EHCI-2` once their platform-device publication and reset behavior are confirmed.
- Only do this after one controller enumerates reliably.

## Open Questions

- The in-tree codebase does not yet reveal the EHCI device-tree node names.
- Reset, clock, and ownership sequencing for Latte-only `EHCI-1` and `EHCI-2` are not yet documented in this repo.
- WiiUBrew marks the host controllers as Starbuck-accessible, but the current PPC OHCI driver works on real hardware, so access policy clearly depends on runtime setup and must be validated empirically rather than inferred from that access column alone.

## References

- `WiiUSB/Makefile`
- `WiiUSB/Info.plist`
- `WiiUSB/src/OHCI/WiiOHCI.hpp`
- `WiiPlatform/src/Interrupts/LatteInterruptController.cpp`
- `WiiPlatform/src/Interrupts/LatteRegs.hpp`
- `include/WiiHollywood.hpp`
- WiiUBrew: `Hardware/USB_Host_Controller`
- WiiUBrew: `Hardware/Latte_IRQs`
- Tiger guest header: `/System/Library/Frameworks/Kernel.framework/Headers/IOKit/usb/IOUSBControllerV2.h`