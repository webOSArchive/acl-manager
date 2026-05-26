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
3. Daemon calls `acl-helper` which:
   - Finds the ACL upstart job (scans `/etc/event.d/` for files containing `"webos-services"`
     or `"start-acl.sh"`) and runs `stop/start <job>` — best-effort, because the jobs are
     one-shot and may already be in "waiting" state
   - On "stop": **SIGKILL**s every `omww-*`, `vfb-agent`, `vfb-client` process found in `/proc`
     by name — this permanently terminates them regardless of session
   - On "start": relies on upstart `start start-acl-services` to re-launch `webos-services.sh`,
     which spawns fresh ACL processes; no SIGCONT needed

**Why SIGKILL by name (not SIGSTOP, not session-based):**
- SIGSTOP leaves processes alive in /proc; Luna bus monitor sees "unresponsive" services
  and triggers a restart — exactly the bug we're fixing
- Session-based kill (the old approach) fails because `webos-services.sh` is a compiled
  binary launcher that forks ACL processes into its own session; that session ID changes on
  every restart and is not recorded anywhere predictable
- SIGKILL by cmdline name reliably finds and terminates all ACL processes regardless of
  how they were launched or what session they're in; dead processes cannot be revived

**ACL upstart job structure on device (observed):**
- `start-acl` — one-shot, runs `start-acl.sh` on boot to set up the environment
- `start-acl-services` — one-shot, runs `webos-services.sh` which spawns all ACL daemons
  in background and exits; upstart job returns to "waiting" state immediately
- `start-acl-icon` — copies notification icon after services are up
- All three are in "waiting" state by the time the user opens ACL Manager

**Detection:** The app scans `/proc` directly (accessible from jail) and checks:
- `/proc/<pid>/cmdline` for ACL process names (`omww-`, `vfb-agent`)
- `/proc/<pid>/stat` for process state ('T' = stopped, 'S'/'R' = running)
- `/media/internal/.acl-stopped` — state file written by acl-helper so "ACL Stopped"
  can be shown even after processes are fully dead (distinguishes from "not installed")

**Known device quirks:**
- TouchPad busybox `tr` does NOT support POSIX character classes (`[:space:]`).
  `tr -d '[:space:]'` treats the argument as a literal set of characters, deleting
  's', 'p', 'a', 'c', 'e', '[', ']', ':' from the input.  Always use explicit
  whitespace chars: `tr -d ' \t\r\n'`

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

**v1.1.0 — tested and working on device** (as of 2026-05-26).

Stop and start are confirmed working: ACL processes are fully terminated on stop
and do not restart (verified with a 60-second hold), and `start` correctly
relaunches ACL via upstart. Three bugs were found and fixed during device testing:

1. **`tr -d '[:space:]'` bug in `acl-watchd.sh`** — TouchPad busybox `tr` treats
   `[:space:]` as a literal character set, deleting 's' and 'p' from commands.
   `"stop"` became `"to"`, which the daemon logged as unknown and ignored.
   Fixed by using `tr -d ' \t\r\n'`.

2. **Wrong process kill strategy in `acl-helper.c`** — The old session-based kill
   read the session ID from the Android init PID file and `omww-service-mngr`'s
   stat entry. Unreliable because `webos-services.sh` (the ACL launcher) is a
   compiled binary that forks services into its own session, which changes on
   every restart. Replaced with a direct name-based kill: scans `/proc` and
   SIGKILLs every process whose cmdline matches `omww-`, `vfb-agent`, or
   `vfb-client`.

3. **Upstart job search matched wrong job** — The search looked for process names
   (`omww-service-mngr`) inside upstart job files, but those files only reference
   shell script paths (`webos-services.sh`, `start-acl.sh`). Now searches for
   `"webos-services"` which uniquely identifies `start-acl-services`.

The app binary (`acl-manager`) has not been installed from the `.ipk` during
testing — the device binaries were pushed directly via `novacom put`. A fresh
Preware install from `com.palm.acl-manager_1.1.0_all.ipk` should be done to
validate the full install path before wider distribution.

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
