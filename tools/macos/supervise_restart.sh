#!/usr/bin/env bash
# One lightweight parent, no overlapping game processes. run.sh still owns all
# preparation and per-launch mod-view cleanup, including community translations.
set -uo pipefail
cd -- "$(dirname -- "$0")/../.."
export BB_RESTART_SUPERVISED=1
unset BB_QUICKLOAD_PENDING
child=
stopping=0
stop() {
    stopping=$1
    [[ -z $child ]] || kill -TERM "$child" 2>/dev/null || true
}
trap 'stop 143' TERM
trap 'stop 130' INT
quickload=0
while (( stopping == 0 )); do
    if (( quickload )); then export BB_QUICKLOAD_PENDING=1; else unset BB_QUICKLOAD_PENDING; fi
    bash run.sh "$@" &
    child=$!
    status=0
    wait "$child" || status=$?
    if (( stopping )); then
        # A signal interrupts wait; do not return until the child really exits.
        wait "$child" 2>/dev/null || true
        exit "$stopping"
    fi
    child=
    unset BB_QUICKLOAD_PENDING
    case $status in
        75) quickload=0 ;;
        76) quickload=1 ;;
        *) exit "$status" ;;
    esac
    echo 'Runtime: previous game exited; preparing supervised restart'
done
exit "$stopping"
