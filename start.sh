#!/bin/bash
# Launch NeuroSwarm — suppresses polkit/GVFS desktop dialogs during startup
DIR="$(cd "$(dirname "$0")" && pwd)"

# Temporarily stop polkit agent to prevent auth dialogs from fork() scope creation
pkill -f polkit-gnome >/dev/null 2>&1
POLKIT_KILLED=$?

# Launch with clean environment (no DBUS/DISPLAY = no desktop integration)
# Redirect all output to log to prevent terminal bell/beep
env -i \
    HOME="$HOME" \
    PATH="$PATH" \
    LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}" \
    LANG="${LANG:-en_US.UTF-8}" \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \
    "$DIR/build/CerebralMatrix" "$@" 2>&1 | tr -d '\007' &

CM_PID=$!
echo "[start.sh] CerebralMatrix PID: $CM_PID"

# Wait for lobes to finish forking, then restore polkit agent
sleep 5
if [ "$POLKIT_KILLED" -eq 0 ]; then
    nohup /usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1 >/dev/null 2>&1 &
    echo "[start.sh] Polkit agent restored."
fi

# Forward signals to CerebralMatrix
trap "kill $CM_PID 2>/dev/null" SIGTERM SIGINT
wait $CM_PID
