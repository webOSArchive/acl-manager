# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ACL Manager is a simple WebOS utility app that allows users to manually suspend and resume Android Compatibility Layer (ACL) processes. This is useful for apps that have memory or resource conflicts with ACL, such as emulators using dynamic recompilers.

## Build Commands

### Build and Package
```bash
./build.sh
# Or manually:
CROSS_COMPILE=arm-linux-gnueabi- make
/opt/PalmSDK/Current/bin/palm-package .
```

### Clean Build
```bash
make clean
```

### Install on Device
```bash
palm-install com.starkka.aclmanager_*.ipk
```

## Architecture

### Files
- `acl-manager.c` - Main application source (single-file app)
- `appinfo.json` - WebOS app manifest
- `icon.png` - App icon (64x64)
- `Makefile` - Cross-compilation makefile
- `build.sh` - Build and package script

### Dependencies
- PalmPDK (`/opt/PalmPDK`) - SDL 1.2 and PDL headers/libraries
- PalmSDK (`/opt/PalmSDK`) - `palm-package` tool
- ARM cross-compiler (`arm-linux-gnueabi-gcc`)

### How ACL Suspension Works

ACL (Android Compatibility Layer) runs several processes:
1. **Android init** - Main Android runtime, PID stored in `/media/omww/android/tmp/init_pid.txt`
2. **omww-service-mngr** - ACL service manager
3. **vfb-agent/vfb-client** - Virtual framebuffer for graphics interception

The app uses POSIX signals to suspend/resume:
- `SIGSTOP` (via `pkill -STOP`) - Suspends processes (like Ctrl+Z)
- `SIGCONT` (via `pkill -CONT`) - Resumes suspended processes

Processes are suspended by session ID to catch all children:
```c
// Get session ID for a process and stop all processes in that session
system("SID=$(ps -p $PID -o sess=); pkill -STOP -s $SID");
```

### UI Structure

Simple SDL-based interface:
- 1024x768 resolution (HP TouchPad landscape)
- Two main buttons: "Stop ACL" (red) and "Start ACL" (green)
- Exit button at bottom
- Status message showing current state
- Custom bitmap font rendering (no external font dependencies)

### Key Functions

- `suspend_acl()` - Sends SIGSTOP to all ACL processes
- `resume_acl()` - Sends SIGCONT to all ACL processes
- `acl_is_running()` - Checks if ACL is currently active
- `draw_button()` / `draw_text()` - Simple SDL rendering helpers

## Development Notes

### Adding New Features
- Keep the app simple and focused on ACL management
- The bitmap font only supports A-Z, a-z, 0-9, and basic punctuation
- Touch input uses SDL mouse events (webOS maps touch to mouse)

### Testing
- Test on device with ACL installed to verify stop/start works
- Test on device without ACL to verify graceful handling
- After stopping ACL, verify Android apps are paused
- After starting ACL, verify Android apps resume normally

### Known Limitations
- Icon is currently a placeholder (copied from PCSX project)
- No confirmation dialogs before stop/start
- No auto-detection of apps that need ACL stopped
