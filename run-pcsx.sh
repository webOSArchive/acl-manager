#!/bin/sh
#
# PCSX Launcher Script
# Suspends ACL processes while running PCSX to avoid memory/address space conflicts
#

OMWW_ROOT=/media/omww
APP_DIR=/media/cryptofs/apps/usr/palm/applications/com.starkka.pcsxrearmed

log() {
    logger -t PCSX "$@"
    echo "$@" >&2
}

suspend_acl() {
    log "Suspending ACL processes..."

    # Stop Android init and all its children
    INIT_PID=$(cat $OMWW_ROOT/android/tmp/init_pid.txt 2>/dev/null)
    if [ -n "$INIT_PID" ]; then
        INIT_SID=$(ps -p $INIT_PID -o sess= 2>/dev/null)
        if [ -n "$INIT_SID" ]; then
            pkill -STOP -s $INIT_SID 2>/dev/null
            log "Stopped Android init session $INIT_SID"
        fi
    fi

    # Stop ACL service manager and its children
    SVC_MNGR_SID=$(ps -C omww-service-mngr -o sess= 2>/dev/null)
    if [ -n "$SVC_MNGR_SID" ]; then
        pkill -STOP -s $SVC_MNGR_SID 2>/dev/null
        log "Stopped service manager session $SVC_MNGR_SID"
    fi

    # Stop VFB agent (graphics interception)
    pkill -STOP -f vfb-agent 2>/dev/null
    pkill -STOP -f vfb-client 2>/dev/null

    # Give processes time to stop
    sleep 1
}

resume_acl() {
    log "Resuming ACL processes..."

    # Resume Android init and all its children
    INIT_PID=$(cat $OMWW_ROOT/android/tmp/init_pid.txt 2>/dev/null)
    if [ -n "$INIT_PID" ]; then
        INIT_SID=$(ps -p $INIT_PID -o sess= 2>/dev/null)
        if [ -n "$INIT_SID" ]; then
            pkill -CONT -s $INIT_SID 2>/dev/null
            log "Resumed Android init session $INIT_SID"
        fi
    fi

    # Resume ACL service manager and its children
    SVC_MNGR_SID=$(ps -C omww-service-mngr -o sess= 2>/dev/null)
    if [ -n "$SVC_MNGR_SID" ]; then
        pkill -CONT -s $SVC_MNGR_SID 2>/dev/null
        log "Resumed service manager session $SVC_MNGR_SID"
    fi

    # Resume VFB agent
    pkill -CONT -f vfb-agent 2>/dev/null
    pkill -CONT -f vfb-client 2>/dev/null
}

# Check if ACL is installed/running
acl_is_running() {
    [ -f "$OMWW_ROOT/android/tmp/init_pid.txt" ] && \
    [ -d "$OMWW_ROOT" ] && \
    pgrep -f "omww-service-mngr" >/dev/null 2>&1
}

# Main
cd $APP_DIR

if acl_is_running; then
    log "ACL detected, suspending for PCSX..."
    suspend_acl
    ACL_WAS_RUNNING=1
else
    log "ACL not detected, starting PCSX normally"
    ACL_WAS_RUNNING=0
fi

# Run PCSX with all arguments
log "Starting PCSX..."
$APP_DIR/pcsx "$@"
PCSX_EXIT=$?
log "PCSX exited with code $PCSX_EXIT"

# Resume ACL if it was running
if [ "$ACL_WAS_RUNNING" = "1" ]; then
    resume_acl
fi

exit $PCSX_EXIT
