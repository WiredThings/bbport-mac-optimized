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
        76)
            quickload=1
            # Child environments do not propagate back to this parent. Every F3
            # restart must explicitly stay offline, including later menu restarts.
            export BB_ONLINE=0 BB_SKIP_NETWORK_CHOICE=1
            unset BB_NET_MODULE BB_NET_BACKEND BB_THD_TOKEN BB_THD_ONLINE_ID
            unset BB_THD_SERVER BB_THD_AUTH BB_THD_HTTP_ORIGIN BB_THD_HTTP_ROUTE
            unset BB_THD_STUN BB_THD_RULESET BB_P2P_PORT BB_COMMUNITY_SERVER
            unset BB_SHADNET_SERVER BB_SHADNET_WEBAPI BB_SHADNET_NPID BB_SHADNET_PASSWORD BB_SHADNET_TOKEN
            unset SHADPS4_HTTP_HOST_OVERRIDES_JSON
            echo 'Runtime: quickload switched to offline'
            ;;
        *) exit "$status" ;;
    esac
    echo 'Runtime: previous game exited; preparing supervised restart'
done
exit "$stopping"
