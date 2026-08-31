#!/bin/bash

set -u

if [ "$#" -lt 7 ]; then
	echo "usage: $0 HOSTAPD HOSTAPD_CLI WPA_SUPPLICANT WPA_CLI HOSTAPD_TEMPLATE WPA_TEMPLATE TEST [ARGS...]" >&2
	exit 2
fi

hostapd_bin=$1
hostapd_cli_bin=$2
wpa_supplicant_bin=$3
wpa_cli_bin=$4
hostapd_template=$5
wpa_template=$6
shift 6

interfaces=(sta0 ap0 ap1 ap2 ap3)
ap_interfaces=(ap0 ap1 ap2 ap3)
ap_ssids=("Foo University" "My House" "Cat Cafe" "Bar Park")
ap_channels=(1 1 6 11)
station_interfaces=(sta1 sta2)
station_namespaces=(u80211-sta1 u80211-sta2)
station_ssids=("Foo University" "My House")

for interface in "${interfaces[@]}"; do
	if [ ! -e "/sys/class/net/$interface" ]; then
		echo "test setup: interface '$interface' does not exist; run ./prepare_env.sh first" >&2
		exit 1
	fi
done

for index in "${!station_interfaces[@]}"; do
	interface=${station_interfaces[$index]}
	namespace=${station_namespaces[$index]}
	if ! ip -n "$namespace" link show dev "$interface" >/dev/null 2>&1; then
		echo "test setup: interface '$interface' does not exist in namespace '$namespace'; run ./prepare_env.sh first" >&2
		exit 1
	fi
done

runtime_dir=$(mktemp -d "${TMPDIR:-/tmp}/hwsim-services.XXXXXX") || exit 1
hostapd_control=$runtime_dir/hostapd-control
wpa_control=$runtime_dir/wpa-control
child_pids=()
log_files=()

dump_logs() {
	for log_file in "${log_files[@]}"; do
		if [ -s "$log_file" ]; then
			echo "----- $log_file -----" >&2
			cat "$log_file" >&2
		fi
	done
}

cleanup() {
	status=$?
	trap - EXIT HUP INT TERM

	for pid in "${child_pids[@]}"; do
		if kill -0 "$pid" 2>/dev/null; then
			kill "$pid" 2>/dev/null || true
		fi
	done
	for pid in "${child_pids[@]}"; do
		wait "$pid" 2>/dev/null || true
	done
	if [ "$status" -ne 0 ]; then
		dump_logs
	fi
	rm -rf "$runtime_dir"
	exit "$status"
}

trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

mkdir "$hostapd_control" "$wpa_control" || exit 1

for index in "${!ap_interfaces[@]}"; do
	interface=${ap_interfaces[$index]}
	ssid=${ap_ssids[$index]}
	channel=${ap_channels[$index]}
	config_file=$runtime_dir/hostapd-$interface.conf
	log_file=$runtime_dir/hostapd-$interface.log

	sed \
		-e "s|@INTERFACE@|$interface|g" \
		-e "s|@CTRL_INTERFACE@|$hostapd_control|g" \
		-e "s|@SSID@|$ssid|g" \
		-e "s|@CHANNEL@|$channel|g" \
		"$hostapd_template" > "$config_file" || exit 1
	"$hostapd_bin" "$config_file" > "$log_file" 2>&1 &
	child_pids+=("$!")
	log_files+=("$log_file")
done

for index in "${!ap_interfaces[@]}"; do
	interface=${ap_interfaces[$index]}
	pid=${child_pids[$index]}
	ready=false
	for ((attempt = 0; attempt < 100; attempt++)); do
		if ! kill -0 "$pid" 2>/dev/null; then
			echo "test setup: hostapd on '$interface' exited before becoming ready" >&2
			exit 1
		fi
		status=$("$hostapd_cli_bin" -p "$hostapd_control" -i "$interface" status 2>/dev/null || true)
		if grep -qx 'state=ENABLED' <<< "$status"; then
			ready=true
			break
		fi
		sleep 0.1
	done
	if [ "$ready" != true ]; then
		echo "test setup: timed out waiting for hostapd on '$interface'" >&2
		exit 1
	fi
done

station_pid_offset=${#child_pids[@]}
for index in "${!station_interfaces[@]}"; do
	interface=${station_interfaces[$index]}
	namespace=${station_namespaces[$index]}
	ssid=${station_ssids[$index]}
	config_file=$runtime_dir/wpa-$interface.conf
	log_file=$runtime_dir/wpa-$interface.log

	sed \
		-e "s|@CTRL_INTERFACE@|$wpa_control|g" \
		-e "s|@SSID@|$ssid|g" \
		"$wpa_template" > "$config_file" || exit 1
	ip netns exec "$namespace" \
		"$wpa_supplicant_bin" -Dnl80211 -i "$interface" -c "$config_file" \
		> "$log_file" 2>&1 &
	child_pids+=("$!")
	log_files+=("$log_file")
done

for index in "${!station_interfaces[@]}"; do
	interface=${station_interfaces[$index]}
	namespace=${station_namespaces[$index]}
	ssid=${station_ssids[$index]}
	pid=${child_pids[$((station_pid_offset + index))]}
	ready=false
	for ((attempt = 0; attempt < 150; attempt++)); do
		if ! kill -0 "$pid" 2>/dev/null; then
			echo "test setup: wpa_supplicant on '$interface' exited before association" >&2
			exit 1
		fi
		status=$(ip netns exec "$namespace" \
			"$wpa_cli_bin" -p "$wpa_control" -i "$interface" status 2>/dev/null || true)
		if grep -qx 'wpa_state=COMPLETED' <<< "$status" \
				&& grep -Fqx "ssid=$ssid" <<< "$status"; then
			ready=true
			break
		fi
		sleep 0.1
	done
	if [ "$ready" != true ]; then
		echo "test setup: timed out waiting for '$interface' to associate with '$ssid'" >&2
		exit 1
	fi
done

"$@"
exit $?
