# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ACL Manager is a simple WebOS utility app that allows users to manually suspend and resume Android Compatibility Layer (ACL) processes. This is useful for apps that have memory or resource conflicts with ACL, such as emulators using dynamic recompilers.

## Build Commands

### Build and Package
```bash
make clean && make
/opt/PalmSDK/Current/bin/palm-package .
```

### Install on Device
```bash
palm-install com.palm.acl-manager_*.ipk
```

### First-Time Device Setup
The app requires helper components installed on the device (done once):
```bash
# Install the setuid helper binary
novacom put file:///usr/bin/acl-helper < acl-helper
novacom run -- file://bin/chmod 4755 /usr/bin/acl-helper

# Install the watch daemon
novacom put file:///usr/bin/acl-watchd < acl-watchd.sh
novacom run -- file://bin/chmod 755 /usr/bin/acl-watchd

# Install upstart job for auto-start on boot
novacom put file:///etc/event.d/acl-watchd < acl-watchd.conf

# Start the daemon
novacom run -- file://sbin/start acl-watchd
```

## Architecture

### App Files
- `acl-manager.c` - Main application source (single-file app)
- `appinfo.json` - WebOS app manifest (id: com.palm.acl-manager)
- `icon.png` - App icon (64x64)
- `Makefile` - Cross-compilation makefile using Linaro GCC 4.9.4

### Device Components (installed separately)
- `/usr/bin/acl-helper` - Setuid root binary that signals ACL processes
- `/usr/bin/acl-watchd` - Daemon that monitors control file and executes helper
- `/etc/event.d/acl-watchd` - Upstart job for auto-starting daemon on boot

### Dependencies
- PalmPDK (`/opt/PalmPDK`) - SDL 1.2 and PDL headers/libraries
- PalmSDK (`/opt/PalmSDK`) - `palm-package` tool
- Linaro GCC 4.9.4 (`/opt/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabi`) - Required for glibc 2.8 compatibility

### How ACL Suspension Works

ACL (Android Compatibility Layer) runs several processes:
1. **omww-service-mngr** - ACL service manager
2. **omww-proxy, omww-mapd, omww-netd, etc.** - Various ACL services
3. **vfb-agent** - Virtual framebuffer for graphics interception
4. **Android init, zygote** - Core Android runtime processes

**The Challenge:** WebOS PDK apps run inside a jail with:
- Non-root user privileges
- `nosuid` mount on `/usr/bin` (setuid binaries don't work)
- Separate `/tmp` filesystem from the system

**The Solution:** A daemon-based architecture:
1. App writes "stop" or "start" to `/media/internal/.acl-control` (shared with system)
2. `acl-watchd` daemon (running as root outside jail) monitors this file
3. Daemon calls `acl-helper` which scans `/proc` and sends SIGSTOP/SIGCONT

**Detection:** The app scans `/proc` directly (accessible from jail) and checks:
- `/proc/<pid>/cmdline` for ACL process names
- `/proc/<pid>/stat` for process state ('T' = stopped, 'S'/'R' = running)

### UI Structure

Simple SDL-based interface:
- 1024x768 resolution (HP TouchPad landscape)
- Two main buttons: "Stop ACL" (red) and "Start ACL" (green)
- Exit button at bottom
- Status message showing current state (ACL Running / ACL Stopped / ACL Not Detected)
- Custom bitmap font rendering (no external font dependencies)

### Key Functions

- `acl_is_running()` - Scans /proc for ACL processes and checks if they're running (not stopped)
- `process_is_running()` - Checks /proc/<pid>/stat for process state
- `suspend_acl()` - Writes "stop" to control file for daemon
- `resume_acl()` - Writes "start" to control file for daemon
- `draw_button()` / `draw_text()` - Simple SDL rendering helpers

## Development Notes

### Adding New Features
- Keep the app simple and focused on ACL management
- The bitmap font only supports A-Z, a-z, 0-9, and basic punctuation
- Touch input uses SDL mouse events (webOS maps touch to mouse)

### Testing
- Test on device with ACL installed to verify stop/start works
- Test on device without ACL to verify graceful handling
- After stopping ACL, verify Android apps are paused (won't launch)
- After starting ACL, verify Android apps resume normally
- Test after reboot to verify daemon auto-starts

### Toolchain Notes
- Must use Linaro GCC 4.9.4 - modern GCC links against glibc 2.34+ but TouchPad only has glibc 2.8
- Makefile automatically uses Linaro toolchain if found at expected path
- Use `-std=gnu99` for C99 features (Linaro GCC defaults to C89)

### Known Limitations
- No confirmation dialogs before stop/start
- No auto-detection of apps that need ACL stopped
- Daemon must be manually installed on device (not part of ipk package)
