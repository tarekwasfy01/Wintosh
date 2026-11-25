#!/usr/bin/env bash
set -uo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <darwin-computa-root> <guest-macho>" >&2
    exit 64
fi

COMPUTA_ROOT=${1%/}
GUEST_MACHO=$2
GUEST_BASENAME=${GUEST_MACHO##*/}
RUNTIME="$COMPUTA_ROOT/project/linux/Build/Release/boxedwine"
if [[ ! -x "$RUNTIME" ]]; then
    echo "Darwin_Computa runtime is missing or not executable: $RUNTIME" >&2
    exit 66
fi

LOG=$(mktemp "/tmp/wintosh-darwin.XXXXXX") || exit 73
DSERVER_INIT="$GUEST_MACHO" BW64_DSLOG=1 \
    "$RUNTIME" --darwin-run /usr/bin/darlingserver >"$LOG" 2>&1 &
RUNTIME_PID=$!

cleanup() {
    if kill -0 "$RUNTIME_PID" 2>/dev/null; then
        kill -KILL "$RUNTIME_PID" 2>/dev/null || true
        wait "$RUNTIME_PID" 2>/dev/null || true
    fi
    rm -f "$LOG"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

APP_PID=
EXIT_LINE=
SECONDS=0
while (( SECONDS < 240 )); do
    app_exec=$(grep "sys_execve64: pid=" "$LOG" 2>/dev/null \
        | grep -F 'mldr!' | grep -F "$GUEST_BASENAME'" | tail -n 1 || true)
    if [[ -n "$app_exec" ]]; then
        APP_PID=$(sed -nE 's/.*sys_execve64: pid=([0-9]+).*/\1/p' <<<"$app_exec")
    fi
    if [[ -n "$APP_PID" ]]; then
        EXIT_LINE=$(grep -E "CPU64: exit_group syscall, status=-?[0-9]+  pid=$APP_PID exe=" "$LOG" \
            | tail -n 1 || true)
        [[ -n "$EXIT_LINE" ]] && break
    else
        bootstrap_exit=$(grep -E "CPU64: exit_group syscall, status=-?[0-9]+  pid=[0-9]+ exe='.*/mldr'" "$LOG" \
            | tail -n 1 || true)
        if [[ -n "$bootstrap_exit" ]]; then
            echo "Darling mldr exited before starting the requested guest: $GUEST_MACHO" >&2
            tail -n 80 "$LOG" >&2
            exit 1
        fi
    fi
    if ! kill -0 "$RUNTIME_PID" 2>/dev/null; then
        wait "$RUNTIME_PID"
        runtime_status=$?
        if [[ -n "$APP_PID" ]]; then
            EXIT_LINE=$(grep -E "CPU64: exit_group syscall, status=-?[0-9]+  pid=$APP_PID exe=" "$LOG" \
                | tail -n 1 || true)
            [[ -n "$EXIT_LINE" ]] && break
        fi
        tail -n 80 "$LOG" >&2
        exit "$runtime_status"
    fi
    sleep 1
done

if [[ -z "$EXIT_LINE" ]]; then
    echo "Darling guest did not report an exit within 240 seconds: $GUEST_MACHO" >&2
    tail -n 80 "$LOG" >&2
    exit 124
fi

if [[ -n "$APP_PID" ]]; then
    grep -E "^\[guest fd=[12] pid=$APP_PID " "$LOG" \
        | sed -E 's/^\[guest fd=[12] pid=[0-9]+ [^]]*\] //' || true
fi

guest_status=$(sed -nE 's/.*status=(-?[0-9]+).*/\1/p' <<<"$EXIT_LINE")
if [[ "$guest_status" =~ ^-?[0-9]+$ ]]; then
    printf 'WINTOSH_DARWIN_GUEST_EXIT=%s\n' "$guest_status"
    exit 0
fi
echo "Could not parse Darling guest exit status: $EXIT_LINE" >&2
exit 125
