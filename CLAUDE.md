# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ACL Manager is a simple WebOS utility app that allows users to manually suspend and resume Android Compatibility Layer (ACL) processes. This is useful for apps that have memory or resource conflicts with ACL, such as emulators using dynamic recompilers.

## Build Commands

### Requirements (Linux build machine)
- Linaro GCC 4.9.4 at `/opt/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabi/`
  — required for glibc 2.8 compatibility with the TouchPad
  — download: https://releases.linaro.org/components/toolchain/binaries/4.9-2017.01/arm-linux-gnueabi/
- PalmPDK at `/opt/PalmPDK` (SDL 1.2 + PDL headers/libs)
- PalmSDK at `/opt/PalmSDK` (`palm-package` tool)
- Standard `ar` (from binutils) and `tar` — present on any Linux distro

### Build and Package
```bash
./build.sh
```
This does everything: `make clean && make` (builds `acl-manager` and `acl-helper`),
runs `palm-package`, then repacks the `.ipk` to inject `postinst`/`prerm` control
scripts. Output is `com.palm.acl-manager_1.0.0_all.ipk`.

If the Linaro toolchain is missing, `make` falls back to the system
`arm-linux-gnueabi-gcc` with a warning — the binary may not run on the TouchPad
due to glibc version mismatch, but is useful for build verification.

### Install on Device
Install via **Preware** or **WebOS Quick Install** — NOT `palm-install`.

The `postinst` script runs as root during a Preware install and automatically:
- Copies `acl-helper` → `/usr/bin/acl-helper` (chmod 4755, setuid root)
- Copies `acl-watchd.sh` → `/usr/bin/acl-watchd`
- Copies `acl-watchd.conf` → `/etc/event.d/acl-watchd`
- Runs `start acl-watchd` to start the daemon immediately

Using `palm-install` skips the postinst (runs as non-root), so the daemon
won't be set up and the app won't be able to stop ACL.

### Manual Device Setup (for debugging or if Preware is unavailable)
```bash
make clean && make

novacom put file:///usr/bin/acl-helper < acl-helper
novacom run -- file://bin/chmod 4755 /usr/bin/acl-helper

novacom put file:///usr/bin/acl-watchd < acl-watchd.sh
novacom run -- file://bin/chmod 755 /usr/bin/acl-watchd

novacom put file:///etc/event.d/acl-watchd < acl-watchd.conf
novacom run -- file://sbin/start acl-watchd
```

### Inspect the .ipk before installing
```bash
# Verify postinst/prerm were injected correctly:
ar t com.palm.acl-manager_1.0.0_all.ipk        # should list debian-binary, control.tar.gz, data.tar.gz
tar -tzf <(ar p com.palm.acl-manager_1.0.0_all.ipk control.tar.gz)  # should include postinst and prerm
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
2. `acl-watchd` daemon (running as root outside jail) reads and removes the file
3. Daemon calls `acl-helper` which does **both**:
   - Finds the ACL upstart job (scans `/etc/event.d/` for files mentioning ACL process names)
     and runs `stop <job>` — marks the job intentionally stopped so upstart won't respawn it
   - **Always** follows up with SIGSTOP to all ACL sessions (using session IDs from
     `/proc/<pid>/stat` and `/media/omww/android/tmp/init_pid.txt`) to catch orphaned
     child processes that upstart didn't kill directly
   - On "start": runs `start <job>` via upstart; only sends SIGCONT if upstart was unavailable

**Why upstart instead of SIGSTOP:**
SIGSTOP pauses processes but leaves them "running" from upstart's perspective.
The Luna service bus monitor (or ACL's internal watchdog) detects that ACL services
are unresponsive and triggers a restart.  Using `stop <job>` via upstart marks the
job as intentionally stopped, preventing any respawn.  `start <job>` properly
re-initialises the service chain.

**Detection:** The app scans `/proc` directly (accessible from jail) and checks:
- `/proc/<pid>/cmdline` for ACL process names
- `/proc/<pid>/stat` for process state ('T' = stopped, 'S'/'R' = running)
- `/media/internal/.acl-stopped` — state file written by acl-helper so "ACL Stopped"
  can be shown even after processes are fully dead (distinguishes from "not installed")

### UI Structure

Simple SDL-based interface:
- 1024x768 resolution (HP TouchPad landscape)
- Two main buttons: "Stop ACL" (red) and "Start ACL" (green)
- Exit button at bottom
- Status message showing current state (ACL Running / ACL Stopped / ACL Not Detected)
- Custom bitmap font rendering (no external font dependencies)

### Key Functions

- `acl_is_running()` - Scans /proc for ACL processes and checks if they're running (not stopped)
- `acl_was_stopped_by_us()` - Checks for state file so UI shows "ACL Stopped" vs "ACL Not Detected"
- `process_is_running()` - Checks /proc/<pid>/stat for process state
- `suspend_acl()` - Writes "stop" to control file, polls until processes are gone (10 s timeout)
- `resume_acl()` - Writes "start" to control file, polls until ACL is back (15 s timeout)
- `draw_button()` / `draw_text()` - Simple SDL rendering helpers

### Device Components

- `acl-helper.c` - Source for the setuid root binary; build with `make acl-helper`
- `acl-watchd.sh` - Shell daemon that polls the control file every 1 s
- `acl-watchd.conf` - Upstart job config (`/etc/event.d/acl-watchd`)

## Current Status

**Not yet tested on device** (as of 2026-05-25). All code was written and reviewed
on a Mac; build and device testing are pending on a Linux machine with a TouchPad.
Follow the Testing section below in order when picking this up.

## Development Notes

### Adding New Features
- Keep the app simple and focused on ACL management
- The bitmap font only supports A-Z, a-z, 0-9, and basic punctuation
- Touch input uses SDL mouse events (webOS maps touch to mouse)

### Testing

#### 1. Verify postinst ran correctly (right after Preware install)
```bash
# All three should exist:
novacom run -- file://bin/ls -la /usr/bin/acl-helper /usr/bin/acl-watchd /etc/event.d/acl-watchd

# acl-helper must show setuid bit (-rwsr-xr-x), owner root:
novacom run -- file://bin/ls -la /usr/bin/acl-helper
# Expected: -rwsr-xr-x 1 root root ...

# Daemon should be running:
novacom run -- file://sbin/status acl-watchd
# Expected: acl-watchd start/running, process <pid>
```

#### 2. Verify the daemon can find the ACL upstart job
```bash
# Trigger a stop and immediately read the log:
novacom run -- file://bin/cat /tmp/acl-watchd.log
# Look for a line like:
#   acl-helper: found ACL upstart job 'omww'
#   acl-helper: upstart stop omww OK
# If you see "no ACL upstart job found", the signal fallback is being used.
# That's not fatal but means we can't guarantee ACL stays stopped.
```

#### 3. Verify ACL is actually fully stopped (not just paused)
```bash
# After pressing "Stop ACL" in the app:
novacom run -- file://bin/sh -c "ps aux | grep omww"
# Expected: NO omww-* processes at all (they should be dead, not in state T).

# State file should exist:
novacom run -- file://bin/ls -la /media/internal/.acl-stopped
# Expected: file exists

# Wait 60 seconds, then check again — this is the key persistence test:
novacom run -- file://bin/sh -c "ps aux | grep omww"
# Expected: still no omww-* processes (WebOS has NOT restarted ACL)
```

#### 4. Verify ACL restarts cleanly
```bash
# After pressing "Start ACL" in the app:
novacom run -- file://bin/sh -c "ps aux | grep omww"
# Expected: omww-service-mngr and child processes visible again

# State file should be gone:
novacom run -- file://bin/ls /media/internal/.acl-stopped
# Expected: No such file or directory

# Launch an Android app to confirm ACL is functional.
```

#### 5. Verify persistence across app restart
```bash
# 1. Press "Stop ACL", verify stopped (step 3 above)
# 2. Exit ACL Manager app
# 3. Relaunch ACL Manager
# Expected: status shows "ACL Stopped" (not "ACL Not Detected")
#           This confirms the state file is working correctly.
```

#### 6. Verify daemon survives reboot
```bash
# After rebooting the device:
novacom run -- file://sbin/status acl-watchd
# Expected: acl-watchd start/running
# Note: ACL itself will start normally on reboot (the stop doesn't persist
# across reboots by design — the control file is consumed immediately).
```

#### 7. Test without ACL installed
```bash
# App should show "ACL Not Detected" on startup (no omww processes found,
# no state file present). Stop/Start buttons should still be pressable
# without crashing (acl-helper will exit cleanly finding nothing to stop).
```

#### Diagnosing failures
If ACL restarts after being stopped:
```bash
# Check what restarted it:
novacom run -- file://bin/cat /tmp/acl-watchd.log
# Look for: did upstart stop succeed? Did a second watchdog job restart it?

# List all upstart jobs that reference ACL:
novacom run -- file://bin/grep -rl "omww\|vfb-agent" /etc/event.d/
# If more than one file appears, there may be a separate watchdog job
# that our helper isn't stopping. Note the filenames and report them.
```

### Toolchain Notes
- Must use Linaro GCC 4.9.4 - modern GCC links against glibc 2.34+ but TouchPad only has glibc 2.8
- Makefile automatically uses Linaro toolchain if found at expected path
- Use `-std=gnu99` for C99 features (Linaro GCC defaults to C89)

### Known Limitations
- No confirmation dialogs before stop/start
- No auto-detection of apps that need ACL stopped
- If multiple upstart jobs reference ACL processes, only the first one found
  alphabetically is stopped; a separate watchdog job could restart ACL
- ACL stop does not persist across device reboots (by design — control file
  is consumed immediately so the device boots into a clean state)
