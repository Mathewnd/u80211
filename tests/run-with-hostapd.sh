#!/bin/sh

set -u

if [ "$#" -lt 4 ]; then
	echo "usage: $0 HOSTAPD HOSTAPD_CLI CONFIG_TEMPLATE TEST [ARGS...]" >&2
	exit 2
fi

hostapd_bin=$1
hostapd_cli_bin=$2
config_template=$3
shift 3

if [ ! -e /sys/class/net/ap0 ]; then
	echo "u80211 test setup: interface 'ap0' does not exist" >&2
	exit 1
fi

runtime_dir=$(mktemp -d "${TMPDIR:-/tmp}/u80211-hostapd.XXXXXX") || exit 1
control_dir=$runtime_dir/control
config_file=$runtime_dir/hostapd.conf
log_file=$runtime_dir/hostapd.log
hostapd_pid=

cleanup() {
	status=$?
	trap - EXIT HUP INT TERM

	if [ -n "$hostapd_pid" ]; then
		if kill -0 "$hostapd_pid" 2>/dev/null; then
			kill "$hostapd_pid" 2>/dev/null
		fi
		wait "$hostapd_pid" 2>/dev/null
	fi

	rm -rf "$runtime_dir"
	exit "$status"
}

trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

sed "s|@CTRL_INTERFACE@|$control_dir|g" "$config_template" > "$config_file" || exit 1

"$hostapd_bin" "$config_file" > "$log_file" 2>&1 &
hostapd_pid=$!

ready=false
attempt=0
while [ "$attempt" -lt 50 ]; do
	if ! kill -0 "$hostapd_pid" 2>/dev/null; then
		wait "$hostapd_pid" 2>/dev/null
		hostapd_pid=
		echo "u80211 test setup: hostapd exited before becoming ready" >&2
		cat "$log_file" >&2
		exit 1
	fi

	if "$hostapd_cli_bin" -p "$control_dir" -i ap0 ping 2>/dev/null | grep -qx PONG; then
		ready=true
		break
	fi

	attempt=$((attempt + 1))
	sleep 0.1
done

if [ "$ready" != true ]; then
	echo "u80211 test setup: timed out waiting for hostapd on 'ap0'" >&2
	cat "$log_file" >&2
	exit 1
fi

"$@"
exit $?
