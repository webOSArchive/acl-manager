#!/bin/sh
#
# acl-watchd - ACL watch daemon
#
# Monitors /media/internal/.acl-control and calls acl-helper when the
# app writes a command.  Runs as root outside the WebOS PDK jail so it
# can execute the setuid acl-helper.
#
# The control file is consumed immediately after reading to avoid
# re-executing the same command after a daemon restart or reboot.
#
# Managed by upstart via /etc/event.d/acl-watchd.
#

CONTROL_FILE="/media/internal/.acl-control"
HELPER="/usr/bin/acl-helper"
LOGFILE="/tmp/acl-watchd.log"

log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') acl-watchd: $*" | tee -a "$LOGFILE" >&2
}

log "started (pid $$)"

while true; do
    if [ -f "$CONTROL_FILE" ]; then
        # Read and immediately remove the control file so we don't
        # re-execute it on daemon restart or after a reboot.
        # NOTE: Use explicit whitespace chars, NOT [:space:] — the TouchPad's
        # busybox tr does not support POSIX character classes, and would treat
        # [:space:] as a literal set of characters, deleting 's', 'p', etc.
        cmd=$(tr -d ' \t\r\n' < "$CONTROL_FILE" 2>/dev/null)
        rm -f "$CONTROL_FILE"

        case "$cmd" in
            stop|start)
                log "executing: acl-helper $cmd"
                "$HELPER" "$cmd" >> "$LOGFILE" 2>&1
                log "acl-helper $cmd exited $?"
                ;;
            "")
                # Empty file, ignore
                ;;
            *)
                log "unknown command: $cmd"
                ;;
        esac
    fi

    sleep 1
done
