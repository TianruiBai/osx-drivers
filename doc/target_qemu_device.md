# QEMU Tiger Build Host

## Connection

- Host IP: `192.168.64.6`
- SSH user: `admin`
- Password: `admin`
- Root password login is not the validated path for this guest.

## Verified Guest Environment

- OS: Mac OS X 10.4.11 (`8S165`)
- Dev tools: `xcodebuild` reports `DevToolsCore-798.0` and `DevToolsSupport-794.0`
- C compiler: `/usr/bin/gcc` -> `powerpc-apple-darwin8-gcc-4.0.1`
- C++ compiler: `/usr/bin/g++` -> `powerpc-apple-darwin8-g++-4.0.1`
- Make: `GNU Make 3.80`
- SDKs present:
	- `/Developer/SDKs/MacOSX10.3.9.sdk`
	- `/Developer/SDKs/MacOSX10.4u.sdk`
- Headers present:
	- `/System/Library/Frameworks/Kernel.framework/Headers`
	- `/Developer/Headers/FlatCarbon`
- Toolchain utilities present:
	- `/usr/bin/ld`
	- `/usr/bin/libtool`
	- `/usr/bin/nm`

A remote compile smoke test succeeded by building a trivial executable inside the guest.

## Required SSH Options

Mac OS X 10.4 SSH requires legacy `ssh-rsa` support to be enabled from the host.

Recommended options:

```sh
-o HostKeyAlgorithms=+ssh-rsa \
-o PubkeyAcceptedAlgorithms=+ssh-rsa \
-o PreferredAuthentications=password \
-o StrictHostKeyChecking=no
```

## Build Workflow

Use this guest for new kext builds related to the Wii U graphics driver.

1. Copy sources or patches to the guest if needed.
2. Run the build inside the QEMU Tiger environment.
3. Copy build products back out if needed.
4. Deploy to real Wii U manually.
5. Use manual runtime feedback from the Wii U to drive the next driver iteration.

Deployment to Wii U is intentionally manual. Do not assume automated on-device install or test execution from this host.

## Example Commands

Open a shell:

```sh
ssh \
	-o HostKeyAlgorithms=+ssh-rsa \
	-o PubkeyAcceptedAlgorithms=+ssh-rsa \
	-o PreferredAuthentications=password \
	-o StrictHostKeyChecking=no \
	admin@192.168.64.6
```

Copy a file to the guest:

```sh
scp \
	-O \
	-o HostKeyAlgorithms=+ssh-rsa \
	-o PubkeyAcceptedAlgorithms=+ssh-rsa \
	-o PreferredAuthentications=password \
	-o StrictHostKeyChecking=no \
	local-file \
	admin@192.168.64.6:/tmp/
```

Run a remote build command:

```sh
ssh \
	-o HostKeyAlgorithms=+ssh-rsa \
	-o PubkeyAcceptedAlgorithms=+ssh-rsa \
	-o PreferredAuthentications=password \
	-o StrictHostKeyChecking=no \
	admin@192.168.64.6 'cd /path/to/osx-drivers && make'
```

Build just the graphics kext with the repo helper:

```sh
tools/qemu-build-wiigraphics.sh
```

The helper only syncs `WiiGraphics`, `common`, and `include` into the guest so graphics-driver iteration does not require copying the full repo each time.