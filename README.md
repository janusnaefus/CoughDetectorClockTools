# CoughDetector clock tools

Set the CoughDetector's clock from your computer. Connecting USB alone does not set it.

[Download the Windows and Linux package](https://github.com/janusnaefus/CoughDetectorClockTools/releases/latest)

1. Extract the whole package to a folder on your computer.
2. Finish recording and file transfers, close the detector's file-browser window, and connect USB.
3. On Windows 10/11, open `detector-clock.exe`. On Linux x86_64, run `sh ./detector-clock.sh` in the extracted folder.
4. Type `SET`, then check that the tool reports success and a clock-offset interval.
5. Reconnect USB if needed to browse recordings again.

No Python or firmware development tools are required. This repository contains only clock-setting tools and their source, not device firmware or recordings.

## Requirements

The Windows executable is currently unsigned. Linux requires glibc 2.34 or newer and permission to access the detector's serial port. If access is denied, the tool checks the actual device node's permissions and group. When group membership would resolve access, it prints an administrator command for that group and your username; ask your administrator to approve it, then fully log out and log in again. Otherwise it asks for a permissions/ACL/udev review. It never assumes `dialout`, changes permissions or runs `sudo` automatically. Do not run it from the detector's file-browser mount.

The tool supports the CoughDetector clock protocol on USB identity 1209:0001. `--status` reads the clock; `--self-test` checks the local protocol implementation without accessing a device. For unusual multi-device setups, `--serial` accepts the detector's USB serial number (its MCU identifier).

## Time and accuracy

The device stores UTC. The tool sets it from the computer's clock, then checks a fresh device reading. The reported offset interval describes that exchange; it does not measure long-term oscillator drift or guarantee synchronization with a camera. Check the computer's time before setting the device. The tool makes one setting attempt and reports errors rather than retrying silently.

## Source and build

Source is in `source/`. Compile `detector_clock.c` and `sha256.c`, with that directory on the include path. Linux uses a C11 compiler. A Windows GNU C build additionally links `setupapi`, `cfgmgr32` and `advapi32`. The shell launcher belongs beside the Linux executable.

The initial release used the binaries tested with the detector on 25 November 2026. The 26 November update improves Linux permission diagnostics without changing the clock protocol. Both native protocol test suites and self-tests pass; eight additional Linux tests use real pseudo-terminals with restricted child credentials. This update does not constitute a new hardware clock-setting test. See release checksums for exact file identity.
