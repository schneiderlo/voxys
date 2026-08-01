#!/usr/bin/env bash

set -euo pipefail

workspace="${BUILD_WORKSPACE_DIRECTORY:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"

# Keep Nix evaluation and toolchain startup outside the five-process proof.
# Every proof process below is one already-built Bazel binary.
if [[ -z "${IN_NIX_SHELL:-}" ]]; then
    if [[ "${WRECKWATER_PROOF_NIX_ENTERED:-0}" == "1" ]]; then
        echo "nix develop did not provide a development shell." >&2
        exit 2
    fi
    exec nix develop "$workspace" -c env \
        WRECKWATER_PROOF_NIX_ENTERED=1 \
        "$workspace/scripts/run_wreckwater_2v2.sh"
fi

cd "$workspace"

server_ticks="${WRECKWATER_PROOF_SERVER_TICKS:-360}"
timeout_seconds="${WRECKWATER_PROOF_TIMEOUT_SECONDS:-60}"
connect_timeout_seconds="${WRECKWATER_PROOF_CONNECT_TIMEOUT_SECONDS:-20}"
chaos_seed="${WRECKWATER_PROOF_CHAOS_SEED:-424242}"
if [[ ! "$server_ticks" =~ ^[0-9]+$ ]] || ((server_ticks < 60)); then
    echo "WRECKWATER_PROOF_SERVER_TICKS must be an integer >= 60." >&2
    exit 2
fi
if [[ ! "$timeout_seconds" =~ ^[0-9]+$ ]] || ((timeout_seconds < 10)); then
    echo "WRECKWATER_PROOF_TIMEOUT_SECONDS must be an integer >= 10." >&2
    exit 2
fi
if [[ ! "$connect_timeout_seconds" =~ ^[0-9]+$ ]] \
    || ((connect_timeout_seconds < 1 || connect_timeout_seconds >= timeout_seconds)); then
    echo "WRECKWATER_PROOF_CONNECT_TIMEOUT_SECONDS must be an integer >= 1 and less than the process timeout." >&2
    exit 2
fi
if [[ ! "$chaos_seed" =~ ^(0|[1-9][0-9]*)$ ]] \
    || ((chaos_seed > 2000000000)); then
    echo "WRECKWATER_PROOF_CHAOS_SEED must be an integer from 0 through 2000000000." >&2
    exit 2
fi

log_dir="${WRECKWATER_PROOF_LOG_DIR:-}"
if [[ -z "$log_dir" ]]; then
    log_dir="$(mktemp -d "${TMPDIR:-/tmp}/voxys-wreckwater-2v2.XXXXXX")"
else
    if [[ -e "$log_dir" && ! -d "$log_dir" ]]; then
        echo "WRECKWATER_PROOF_LOG_DIR is not a directory." >&2
        exit 2
    fi
    mkdir -p "$log_dir"
    first_log_entry="$(
        find "$log_dir" -mindepth 1 -maxdepth 1 -print -quit
    )"
    if [[ -n "$first_log_entry" ]]; then
        log_dir="$(mktemp -d "$log_dir/wreckwater-2v2.XXXXXX")"
    fi
fi

server_pid=""
declare -a client_pids=("" "" "" "")

cleanup() {
    local pid
    if [[ -n "$server_pid" ]] && kill -0 "$server_pid" 2>/dev/null; then
        kill -TERM "$server_pid" 2>/dev/null || true
    fi
    for pid in "${client_pids[@]}"; do
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            kill -TERM "$pid" 2>/dev/null || true
        fi
    done
    if [[ -n "$server_pid" ]]; then
        wait "$server_pid" 2>/dev/null || true
    fi
    for pid in "${client_pids[@]}"; do
        if [[ -n "$pid" ]]; then
            wait "$pid" 2>/dev/null || true
        fi
    done
}
trap cleanup EXIT INT TERM

dump_lifecycle_diagnostics() {
    local peer
    local pid
    echo "Server lifecycle diagnostics:" >&2
    grep -E \
        '^(wreckwater_server |WRECKWATER_SERVER_(PEER|LIFECYCLE|RUNTIME|FINAL)|AUTHORITY FAIL-STOP)' \
        "$server_log" >&2 || tail -n 40 "$server_log" >&2 || true
    for peer in 1 2 3 4; do
        pid="${client_pids[peer - 1]}"
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            echo "client peer=$peer process=alive" >&2
        else
            echo "client peer=$peer process=exited" >&2
        fi
        grep -E \
            '^(WRECKWATER_CLIENT_(CONNECTED|RECONNECTING|RECONNECTED|CHARACTER_FINAL|FINAL|CHAOS)|Client |Server disconnected)' \
            "$log_dir/client${peer}.log" >&2 || true
    done
}

echo "Building native WRECKWATER server and client probe..." >&2
bazel build //:wreckwater_server //:wreckwater_client_probe

server_binary="$workspace/bazel-bin/wreckwater_server"
client_binary="$workspace/bazel-bin/wreckwater_client_probe"
if [[ ! -x "$server_binary" ]] || [[ ! -x "$client_binary" ]]; then
    echo "Expected native proof binaries were not produced." >&2
    exit 2
fi

generate_key() {
    local key
    while true; do
        key="$(od -An -N32 -tx1 /dev/urandom | tr -d ' \n')"
        if [[ "$key" =~ ^[0-9a-f]{64}$ ]] \
            && [[ "$key" != "0000000000000000000000000000000000000000000000000000000000000000" ]]; then
            printf '%s' "$key"
            return
        fi
    done
}

declare -a keys
for _peer in 1 2 3 4; do
    keys+=("$(generate_key)")
done

server_log="$log_dir/server.log"
timeout --signal=TERM --kill-after=5s "${timeout_seconds}s" \
    "$server_binary" \
    --bind 127.0.0.1 \
    --port 0 \
    --key1 "${keys[0]}" \
    --key2 "${keys[1]}" \
    --key3 "${keys[2]}" \
    --key4 "${keys[3]}" \
    --max-ticks "$server_ticks" \
    >"$server_log" 2>&1 &
server_pid=$!

ready_line=""
for _attempt in $(seq 1 300); do
    if [[ -f "$server_log" ]]; then
        ready_line="$(awk '/^wreckwater_server listening=/{print; exit}' "$server_log")"
    fi
    if [[ -n "$ready_line" ]]; then
        break
    fi
    if ! kill -0 "$server_pid" 2>/dev/null; then
        echo "WRECKWATER server exited before becoming ready. Logs: $log_dir" >&2
        tail -n 40 "$server_log" >&2 || true
        exit 3
    fi
    sleep 0.1
done
if [[ -z "$ready_line" ]]; then
    echo "Timed out waiting for WRECKWATER server readiness. Logs: $log_dir" >&2
    exit 3
fi

endpoint="${ready_line#*listening=}"
endpoint="${endpoint%% *}"
port="${endpoint##*:}"
if [[ ! "$port" =~ ^[0-9]+$ ]] || ((port == 0 || port > 65535)); then
    echo "Could not parse server listening port. Logs: $log_dir" >&2
    exit 3
fi

client_max_ticks=$((server_ticks + 600))
reconnect_tick=$((server_ticks / 2))
for peer in 1 2 3 4; do
    client_log="$log_dir/client${peer}.log"
    declare -a reconnect_args=()
    declare -a chaos_args=()
    if ((peer == 1)); then
        reconnect_args=(--reconnect-at "$reconnect_tick")
    fi
    if ((chaos_seed != 0)); then
        chaos_args=(--chaos-seed "$((chaos_seed + peer))")
    fi
    timeout --signal=TERM --kill-after=5s "${timeout_seconds}s" \
        "$client_binary" \
        --server 127.0.0.1 \
        --port "$port" \
        --peer "$peer" \
        --key "${keys[peer - 1]}" \
        --session 1 \
        --match 1 \
        --world 1 \
        --world-epoch 1 \
        --authority-epoch 1 \
        --max-ticks "$client_max_ticks" \
        "${chaos_args[@]}" \
        "${reconnect_args[@]}" \
        >"$client_log" 2>&1 &
    client_pids[peer - 1]=$!
done
keys=()

all_connected=0
connect_attempts=$((connect_timeout_seconds * 20))
for _attempt in $(seq 1 "$connect_attempts"); do
    connected_count=0
    for peer in 1 2 3 4; do
        if grep -q "^WRECKWATER_CLIENT_CONNECTED peer=${peer} " \
            "$log_dir/client${peer}.log" 2>/dev/null; then
            connected_count=$((connected_count + 1))
        fi
    done
    if ((connected_count == 4)); then
        all_connected=1
        break
    fi
    process_exited=0
    if ! kill -0 "$server_pid" 2>/dev/null; then
        process_exited=1
    fi
    for pid in "${client_pids[@]}"; do
        if [[ -n "$pid" ]] && ! kill -0 "$pid" 2>/dev/null; then
            process_exited=1
        fi
    done
    if ((process_exited != 0)); then
        break
    fi
    sleep 0.05
done
if ((all_connected == 0)); then
    echo "Four-client connection barrier failed before simulation. Logs: $log_dir" >&2
    cleanup
    dump_lifecycle_diagnostics
    trap - EXIT INT TERM
    exit 4
fi
echo "WRECKWATER_2V2_CONNECTED peers=1,2,3,4 port=$port" >&2

set +e
wait "$server_pid"
server_status=$?
set -e
server_pid=""
if ((server_status != 0)); then
    echo "WRECKWATER server failed with status $server_status. Logs: $log_dir" >&2
    dump_lifecycle_diagnostics
    tail -n 60 "$server_log" >&2 || true
    exit 4
fi

client_failed=0
for index in 0 1 2 3; do
    pid="${client_pids[index]}"
    set +e
    wait "$pid"
    status=$?
    set -e
    client_pids[index]=""
    if ((status != 0)); then
        peer=$((index + 1))
        echo "WRECKWATER client $peer failed with status $status." >&2
        tail -n 60 "$log_dir/client${peer}.log" >&2 || true
        client_failed=1
    fi
done
if ((client_failed != 0)); then
    echo "Process proof failed. Logs: $log_dir" >&2
    exit 5
fi

server_final="$(awk '/^WRECKWATER_SERVER_FINAL /{line=$0} END{print line}' "$server_log")"
if [[ -z "$server_final" ]]; then
    echo "Server produced no final convergence record. Logs: $log_dir" >&2
    exit 6
fi
if [[ "$server_final" == *" tick=0 "* ]]; then
    echo "Server never advanced beyond tick zero. Logs: $log_dir" >&2
    dump_lifecycle_diagnostics
    exit 6
fi

expected_tuple=""
for peer in 1 2 3 4; do
    client_log="$log_dir/client${peer}.log"
    final_line="$(awk '/^WRECKWATER_CLIENT_FINAL /{line=$0} END{print line}' "$client_log")"
    if [[ -z "$final_line" ]]; then
        echo "Client $peer produced no final convergence record. Logs: $log_dir" >&2
        exit 6
    fi
    tuple="${final_line#* sequence=}"
    if [[ -z "$expected_tuple" ]]; then
        expected_tuple="$tuple"
    elif [[ "$tuple" != "$expected_tuple" ]]; then
        echo "Client final snapshot tuples diverged. Logs: $log_dir" >&2
        exit 7
    fi
done

server_tuple="${server_final#* sequence=}"
server_phase="$(
    sed -n 's/.* phase=\([^ ]*\).*/\1/p' <<<"$server_tuple"
)"
server_outcome="$(
    sed -n 's/.* outcome=\([^ ]*\).*/\1/p' <<<"$server_tuple"
)"
server_winner="$(
    sed -n 's/.* winner=\([^ ]*\).*/\1/p' <<<"$server_tuple"
)"
case "$server_phase" in
    0) server_phase_name="warmup" ;;
    1) server_phase_name="live" ;;
    2) server_phase_name="overtime" ;;
    3) server_phase_name="finished" ;;
    *)
        echo "Server final record has an unknown phase. Logs: $log_dir" >&2
        exit 7
        ;;
esac
case "$server_outcome" in
    0) server_outcome_name="undecided" ;;
    1) server_outcome_name="crew_victory" ;;
    2) server_outcome_name="tie" ;;
    *)
        echo "Server final record has an unknown outcome. Logs: $log_dir" >&2
        exit 7
        ;;
esac
case "$server_winner" in
    0) server_winner_name="none" ;;
    1) server_winner_name="crew1" ;;
    2) server_winner_name="crew2" ;;
    *)
        echo "Server final record has an unknown winner. Logs: $log_dir" >&2
        exit 7
        ;;
esac
server_tuple="${server_tuple/ phase=${server_phase} / phase=${server_phase_name} }"
server_tuple="${server_tuple/ outcome=${server_outcome} / outcome=${server_outcome_name} }"
server_tuple="${server_tuple/ winner=${server_winner} / winner=${server_winner_name} }"
if [[ "$server_tuple" != "$expected_tuple" ]]; then
    echo "Server and clients disagree on the final certified snapshot tuple. Logs: $log_dir" >&2
    echo "server: $server_tuple" >&2
    echo "client: $expected_tuple" >&2
    exit 7
fi
if [[ "$expected_tuple" != *" score1=1000 score2=0 "* ]]; then
    echo "The exact Crew One bank score was not certified. Logs: $log_dir" >&2
    echo "tuple: $expected_tuple" >&2
    exit 7
fi

expected_roster_hash=""
declare -a character_handles=()
for peer in 1 2 3 4; do
    client_log="$log_dir/client${peer}.log"
    character_line_count="$(
        grep -c '^WRECKWATER_CLIENT_CHARACTER_FINAL ' "$client_log" \
            || true
    )"
    character_line="$(
        awk '/^WRECKWATER_CLIENT_CHARACTER_FINAL /{line=$0} END{print line}' \
            "$client_log"
    )"
    if [[ "$character_line_count" != "1" || -z "$character_line" ]]; then
        echo "Client $peer did not produce exactly one character proof record. Logs: $log_dir" >&2
        exit 8
    fi
    roster="$(
        sed -n 's/.* roster=\([^ ]*\).*/\1/p' <<<"$character_line"
    )"
    roster_hash="$(
        sed -n 's/.* roster_hash=\([^ ]*\).*/\1/p' <<<"$character_line"
    )"
    handle="$(
        sed -n 's/.* handle=\([^ ]*\).*/\1/p' <<<"$character_line"
    )"
    stable="$(
        sed -n 's/.* stable=\([^ ]*\).*/\1/p' <<<"$character_line"
    )"
    generation="$(
        sed -n 's/.* generation=\([^ ]*\).*/\1/p' <<<"$character_line"
    )"
    sent="$(
        sed -n 's/.* sent=\([^ ]*\).*/\1/p' <<<"$character_line"
    )"
    generation_sent="$(
        sed -n 's/.* generation_sent=\([^ ]*\).*/\1/p' \
            <<<"$character_line"
    )"
    ack="$(
        sed -n 's/.* ack=\([^ ]*\).*/\1/p' <<<"$character_line"
    )"
    certified_feet_mm="$(
        sed -n 's/.* certified_feet_mm=\([^ ]*\).*/\1/p' \
            <<<"$character_line"
    )"
    minimum_deck_displacement_mm="$(
        sed -n 's/.* minimum_deck_displacement_mm=\([^ ]*\).*/\1/p' \
            <<<"$character_line"
    )"
    post_reconnect_sent="$(
        sed -n 's/.* post_reconnect_sent=\([^ ]*\).*/\1/p' \
            <<<"$character_line"
    )"
    post_reconnect_ack="$(
        sed -n 's/.* post_reconnect_ack=\([^ ]*\).*/\1/p' \
            <<<"$character_line"
    )"
    old_generation_blocked="$(
        sed -n 's/.* old_generation_blocked=\([^ ]*\).*/\1/p' \
            <<<"$character_line"
    )"
    target_lead="$(
        sed -n 's/.* target_lead=\([^ ]*\).*/\1/p' \
            <<<"$character_line"
    )"
    redundancy="$(
        sed -n 's/.* redundancy=\([^ ]*\).*/\1/p' \
            <<<"$character_line"
    )"
    redundancy_window="$(
        sed -n 's/.* redundancy_window=\([^ ]*\).*/\1/p' \
            <<<"$character_line"
    )"
    retransmitted="$(
        sed -n 's/.* retransmitted=\([^ ]*\).*/\1/p' \
            <<<"$character_line"
    )"
    acknowledged="$(
        sed -n 's/.* acknowledged=\([^ ]*\).*/\1/p' \
            <<<"$character_line"
    )"
    retired="$(
        sed -n 's/.* retired=\([^ ]*\).*/\1/p' \
            <<<"$character_line"
    )"
    history_purges="$(
        sed -n 's/.* history_purges=\([^ ]*\).*/\1/p' \
            <<<"$character_line"
    )"
    max_redundancy="$(
        sed -n 's/.* max_redundancy=\([^ ]*\).*/\1/p' \
            <<<"$character_line"
    )"
    for value in \
        "$roster" "$roster_hash" "$handle" "$stable" "$generation" \
        "$sent" "$generation_sent" "$ack" "$certified_feet_mm" \
        "$minimum_deck_displacement_mm" \
        "$post_reconnect_sent" "$post_reconnect_ack" \
        "$old_generation_blocked" "$target_lead" \
        "$redundancy_window" "$retransmitted" "$acknowledged" \
        "$retired" "$history_purges" "$max_redundancy"; do
        if [[ ! "$value" =~ ^[0-9]+$ ]]; then
            echo "Client $peer character proof record was malformed. Logs: $log_dir" >&2
            echo "character: $character_line" >&2
            exit 8
        fi
    done
    expected_generation=1
    if ((peer == 1)); then
        expected_generation=2
    fi
    if ((roster != 4 || roster_hash == 0 || handle == 0 \
        || stable != 1 || generation != expected_generation \
        || sent == 0 || generation_sent == 0 || ack == 0 \
        || ack > generation_sent \
        || minimum_deck_displacement_mm != 50 \
        || certified_feet_mm < minimum_deck_displacement_mm \
        || target_lead != 24 || redundancy_window != 3 \
        || retransmitted == 0 || retired == 0 \
        || history_purges == 0 || max_redundancy != 3)) \
        || [[ "$redundancy" != "fixed_window" ]]; then
        echo "Client $peer character proof was incomplete. Logs: $log_dir" >&2
        echo "character: $character_line" >&2
        exit 8
    fi
    if ((peer == 1)); then
        if ((post_reconnect_sent == 0 || post_reconnect_ack == 0 \
            || post_reconnect_ack > post_reconnect_sent \
            || old_generation_blocked != 1)); then
            echo "Peer 1 did not prove generation-2 movement and stale-history rejection. Logs: $log_dir" >&2
            echo "character: $character_line" >&2
            exit 8
        fi
    elif ((post_reconnect_sent != 0 || post_reconnect_ack != 0 \
        || old_generation_blocked != 0)); then
        echo "A non-reconnecting peer reported reconnect-only evidence. Logs: $log_dir" >&2
        echo "character: $character_line" >&2
        exit 8
    fi
    if [[ -z "$expected_roster_hash" ]]; then
        expected_roster_hash="$roster_hash"
    elif [[ "$roster_hash" != "$expected_roster_hash" ]]; then
        echo "Final certified character rosters diverged. Logs: $log_dir" >&2
        exit 8
    fi
    for prior_handle in "${character_handles[@]}"; do
        if [[ "$handle" == "$prior_handle" ]]; then
            echo "Two peers reported the same local character handle. Logs: $log_dir" >&2
            exit 8
        fi
    done
    character_handles+=("$handle")
done

runtime_line="$(
    awk '/^WRECKWATER_SERVER_RUNTIME /{line=$0} END{print line}' "$server_log"
)"
if [[ -z "$runtime_line" \
    || "$runtime_line" != *" fail_reason=none "* \
    || "$runtime_line" != *" match_fault=0 "* \
    || "$runtime_line" != *" transaction_aborts=0 "* \
    || "$runtime_line" != *" live_apply_failure=none "* ]]; then
    echo "Server runtime did not finish transaction-clean. Logs: $log_dir" >&2
    echo "runtime: $runtime_line" >&2
    exit 7
fi
server_character_packets="$(
    sed -n 's/.* character_packets=\([^ ]*\).*/\1/p' \
        <<<"$runtime_line"
)"
server_character_samples="$(
    sed -n 's/.* character_samples=\([^ ]*\).*/\1/p' \
        <<<"$runtime_line"
)"
server_character_replayed="$(
    sed -n 's/.* character_replayed=\([^ ]*\).*/\1/p' \
        <<<"$runtime_line"
)"
server_character_rejected="$(
    sed -n 's/.* character_rejected=\([^ ]*\).*/\1/p' \
        <<<"$runtime_line"
)"
for value in \
    "$server_character_packets" "$server_character_samples" \
    "$server_character_replayed" "$server_character_rejected"; do
    if [[ ! "$value" =~ ^[0-9]+$ ]]; then
        echo "Server character redundancy telemetry was malformed. Logs: $log_dir" >&2
        echo "runtime: $runtime_line" >&2
        exit 8
    fi
done
# A sample total above the accepted-packet total proves that at least one
# later bundle recovered multiple unique logical samples. Replayed samples
# prove that repeated history was benignly deduplicated at authority.
if ((server_character_packets == 0 \
    || server_character_samples <= server_character_packets \
    || server_character_replayed == 0 \
    || server_character_rejected != 0)); then
    echo "Server did not prove redundant character-input recovery. Logs: $log_dir" >&2
    echo "runtime: $runtime_line" >&2
    exit 8
fi
if grep -q '^AUTHORITY FAIL-STOP' "$server_log"; then
    echo "Authority entered fail-stop during the proof. Logs: $log_dir" >&2
    exit 7
fi

if ! grep -q '^WRECKWATER_CLIENT_RECONNECTED peer=1 ' "$log_dir/client1.log"; then
    echo "Peer 1 did not complete the scheduled reconnect. Logs: $log_dir" >&2
    exit 8
fi

echo "WRECKWATER_2V2_CHARACTER_PASS roster=4 roster_hash=$expected_roster_hash minimum_deck_displacement_mm=50 target_lead=24 redundancy=fixed_window redundancy_window=3 server_packets=$server_character_packets server_samples=$server_character_samples server_replayed=$server_character_replayed"

peer2_actions="$(
    sed -n \
        's/^WRECKWATER_CLIENT_ACTION peer=2 action=\([^ ]*\).*/\1/p' \
        "$log_dir/client2.log" | paste -sd, -
)"
peer4_actions="$(
    sed -n \
        's/^WRECKWATER_CLIENT_ACTION peer=4 action=\([^ ]*\).*/\1/p' \
        "$log_dir/client4.log" | paste -sd, -
)"
if [[ "$peer2_actions" != "tow,cut,tow,bank" \
    || "$peer4_actions" != "steal" ]]; then
    echo "Deck action chain was not exactly tow,steal,cut,tow,bank. Logs: $log_dir" >&2
    echo "peer2: $peer2_actions" >&2
    echo "peer4: $peer4_actions" >&2
    exit 9
fi
if grep -q ' action=helm ' "$log_dir/client2.log" "$log_dir/client4.log"; then
    echo "A deck peer emitted helm input. Logs: $log_dir" >&2
    exit 9
fi
if grep -Eq ' action=(tow|cut|steal|bank) ' \
    "$log_dir/client1.log" "$log_dir/client3.log"; then
    echo "A helm peer emitted a cargo command. Logs: $log_dir" >&2
    exit 9
fi

if ((chaos_seed != 0)); then
    total_inbound_drops=0
    total_outbound_drops=0
    total_duplicates=0
    total_reorders=0
    total_reliable_duplicates=0
    total_reliable_reorders=0
    for peer in 1 2 3 4; do
        chaos_line="$(
            awk '/^WRECKWATER_CLIENT_CHAOS /{line=$0} END{print line}' \
                "$log_dir/client${peer}.log"
        )"
        expected_seed=$((chaos_seed + peer))
        if [[ -z "$chaos_line" \
            || "$chaos_line" != *" peer=${peer} "* \
            || "$chaos_line" != *" seed=${expected_seed} "* \
            || "$chaos_line" != *" sequence_exhaustions=0 "* \
            || "$chaos_line" != *" faults=0" ]]; then
            echo "Client $peer produced invalid chaos telemetry. Logs: $log_dir" >&2
            echo "chaos: $chaos_line" >&2
            exit 10
        fi
        inbound_drops="$(
            sed -n 's/.* inbound_drop=\([0-9][0-9]*\).*/\1/p' \
                <<<"$chaos_line"
        )"
        outbound_drops="$(
            sed -n 's/.* outbound_drop=\([0-9][0-9]*\).*/\1/p' \
                <<<"$chaos_line"
        )"
        duplicates="$(
            sed -n 's/.* outbound_duplicates=\([0-9][0-9]*\).*/\1/p' \
                <<<"$chaos_line"
        )"
        reliable_duplicates="$(
            sed -n 's/.* reliable_duplicates=\([0-9][0-9]*\).*/\1/p' \
                <<<"$chaos_line"
        )"
        reorders="$(
            sed -n 's/.* outbound_reorders=\([0-9][0-9]*\).*/\1/p' \
                <<<"$chaos_line"
        )"
        reliable_reorders="$(
            sed -n 's/.* reliable_reorders=\([0-9][0-9]*\).*/\1/p' \
                <<<"$chaos_line"
        )"
        if [[ -z "$inbound_drops" || -z "$outbound_drops" \
            || -z "$duplicates" || -z "$reliable_duplicates" \
            || -z "$reorders" || -z "$reliable_reorders" ]]; then
            echo "Client $peer chaos counters were malformed. Logs: $log_dir" >&2
            exit 10
        fi
        total_inbound_drops=$((total_inbound_drops + inbound_drops))
        total_outbound_drops=$((total_outbound_drops + outbound_drops))
        total_duplicates=$((total_duplicates + duplicates))
        total_reorders=$((total_reorders + reorders))
        total_reliable_duplicates=$((total_reliable_duplicates + reliable_duplicates))
        total_reliable_reorders=$((total_reliable_reorders + reliable_reorders))
        if ((peer == 2 || peer == 4)) && ((reliable_duplicates == 0)); then
            echo "Deck peer $peer did not duplicate reliable cargo traffic. Logs: $log_dir" >&2
            exit 10
        fi
    done
    if ((total_inbound_drops == 0 || total_outbound_drops == 0 \
        || total_duplicates == 0 || total_reorders == 0 \
        || total_reliable_duplicates == 0 \
        || total_reliable_reorders == 0)); then
        echo "Chaos profile did not exercise every required policy. Logs: $log_dir" >&2
        echo "drops_in=$total_inbound_drops drops_out=$total_outbound_drops duplicates=$total_duplicates reliable_duplicates=$total_reliable_duplicates reorders=$total_reorders reliable_reorders=$total_reliable_reorders" >&2
        exit 10
    fi

    echo "WRECKWATER_2V2_CHAOS_PASS seed=$chaos_seed delay_quanta=5..10 realtime_loss_permille=100 duplicates=$total_duplicates reliable_duplicates=$total_reliable_duplicates reorders=$total_reorders reliable_reorders=$total_reliable_reorders"
fi
echo "WRECKWATER_2V2_PASS sequence_tuple=$expected_tuple"
echo "WRECKWATER_2V2_LOGS $log_dir"
trap - EXIT INT TERM
