#!/bin/bash

set -eu

interfaces=(sta0 sta1 sta2 ap0 ap1 ap2 ap3)

if [ "$(id -u)" -ne 0 ]; then
	echo "run this script with root privileges" >&2
	exit 1
fi

for command in modprobe ip iw find sort readlink basename; do
	if ! command -v "$command" >/dev/null 2>&1; then
		echo "required command '$command' was not found" >&2
		exit 1
	fi
done

if [ -d /sys/module/mac80211_hwsim ]; then
	if ! modprobe -r mac80211_hwsim; then
		echo "could not unload mac80211_hwsim. stop users of its interfaces first" >&2
		exit 1
	fi
fi

for interface in "${interfaces[@]}"; do
	if [ -e "/sys/class/net/$interface" ]; then
		echo "'$interface' already exists" >&2
		exit 1
	fi
done

complete=false
cleanup_failure() {
	status=$?
	trap - EXIT
	if [ "$complete" != true ]; then
		modprobe -r mac80211_hwsim >/dev/null 2>&1 || true
	fi
	exit "$status"
}
trap cleanup_failure EXIT

modprobe mac80211_hwsim radios=7

mapfile -t radio_interfaces < <(
	for phy_path in $(find /sys/class/ieee80211 -maxdepth 1 -mindepth 1 -name 'phy*' -printf '%p\n' | sort -V); do
		driver_path=$(readlink -f "$phy_path/device/driver" || true)
		[ "${driver_path##*/}" = mac80211_hwsim ] || continue
		for net_path in "$phy_path"/device/net/*; do
			[ -e "$net_path" ] && basename "$net_path"
		done
	done
)

for index in "${!interfaces[@]}"; do
	old_name=${radio_interfaces[$index]}
	new_name=${interfaces[$index]}
	ip link set dev "$old_name" down
	ip link set dev "$old_name" name "$new_name"
done

iw dev sta0 set type monitor
ip link set dev sta0 up

complete=true
trap - EXIT
echo "environment setup: created sta0 sta1 sta2 ap0 ap1 ap2 ap3"
