# ACL Manager for WebOS

A simple utility to suspend and resume Android Compatibility Layer (ACL) processes on WebOS devices.

## What is ACL?

Android Compatibility Layer (ACL) allows WebOS devices to run Android apps. However, some native apps (especially emulators) may have conflicts with ACL due to memory mapping or resource usage.

## Why Use This App?

Some apps don't work correctly when ACL is running in the background. Common symptoms include:
- Crashes on startup
- Memory allocation failures
- Graphics glitches
- Performance issues

ACL Manager lets you temporarily pause ACL processes while using these apps, then resume them when you're done.

## Usage

1. **Launch ACL Manager** from your app launcher
2. **Tap "Stop ACL"** to suspend all ACL processes
3. **Launch your app** (emulator, game, etc.) that has ACL conflicts
4. **When finished**, launch ACL Manager again
5. **Tap "Start ACL"** to resume ACL processes

Your Android apps will work normally again after starting ACL.

## Installation

### From Package
```bash
palm-install com.starkka.aclmanager_1.0.0_all.ipk
```

### Build from Source

Requirements:
- ARM cross-compiler (e.g., `arm-linux-gnueabi-gcc`)
- PalmPDK installed at `/opt/PalmPDK`
- PalmSDK installed at `/opt/PalmSDK`

```bash
./build.sh
palm-install com.starkka.aclmanager_*.ipk
```

## Technical Details

ACL Manager uses POSIX signals to control processes:
- **Stop ACL**: Sends `SIGSTOP` to pause processes (like pressing Ctrl+Z)
- **Start ACL**: Sends `SIGCONT` to resume paused processes

This is safe and non-destructive - no processes are killed, just paused.

## Compatibility

- Tested on HP TouchPad with WebOS 3.0.5
- Requires ACL (Android Compatibility Layer) to be installed
- Works with LunaCE and stock WebOS

## Apps Known to Benefit

- PCSX ReARMed (PlayStation emulator) - dynamic recompiler conflicts
- Other emulators using JIT/dynarec
- Apps with specific memory mapping requirements

## License

GPL v2

## Credits

Created for the WebOS homebrew community.
