#!/bin/bash

set -eu

interfaces=(sta0 sta1 sta2 ap0 ap1 ap2 ap3)
station_addresses=(192.0.2.1/24 192.0.2.2/24 192.0.2.3/24)
peer_station_interfaces=(sta1 sta2)
peer_station_namespaces=(u80211-sta1 u80211-sta2)

if [ "$(id -u)" -ne 0 ]; then
	echo "run this script with root privileges" >&2
	exit 1
fi

for command in modprobe ip iw find sort readlink basename grep; do
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

for namespace in "${peer_station_namespaces[@]}"; do
	ip netns delete "$namespace" 2>/dev/null || true
done

for interface in "${interfaces[@]}"; do
	if [ -e "/sys/class/net/$interface" ]; then
		echo "'$interface' already exists" >&2
		exit 1
	fi
done

complete=false
created_namespaces=()
cleanup_failure() {
	status=$?
	trap - EXIT
	if [ "$complete" != true ]; then
		for namespace in "${created_namespaces[@]}"; do
			ip netns delete "$namespace" >/dev/null 2>&1 || true
		done
		modprobe -r mac80211_hwsim >/dev/null 2>&1 || true
	fi
	exit "$status"
}
trap cleanup_failure EXIT

# Keep each simulated radio single-channel, like the ordinary hardware path
# this backend exercises.  sta0 is the only interface on its radio.
modprobe mac80211_hwsim radios=7 channels=1

mapfile -t radio_interfaces < <(
	for phy_path in $(find /sys/class/ieee80211 -maxdepth 1 -mindepth 1 -name 'phy*' -printf '%p\n' | sort -V); do
		driver_path=$(readlink -f "$phy_path/device/driver" || true)
		[ "${driver_path##*/}" = mac80211_hwsim ] || continue
		for net_path in "$phy_path"/device/net/*; do
			[ -e "$net_path" ] && basename "$net_path"
		done
	done
)

if [ "${#radio_interfaces[@]}" -ne "${#interfaces[@]}" ]; then
	echo "expected ${#interfaces[@]} hwsim radio interfaces, found ${#radio_interfaces[@]}" >&2
	exit 1
fi

for index in "${!interfaces[@]}"; do
	old_name=${radio_interfaces[$index]}
	new_name=${interfaces[$index]}
	ip link set dev "$old_name" down
	ip link set dev "$old_name" name "$new_name"
done

sta0_phy=$(basename "$(readlink -f /sys/class/net/sta0/phy80211)")
iw dev sta0 del
iw phy "$sta0_phy" interface add sta0 type monitor flags active otherbss

ip address add "${station_addresses[0]}" dev sta0
for index in "${!peer_station_interfaces[@]}"; do
	interface=${peer_station_interfaces[$index]}
	namespace=${peer_station_namespaces[$index]}
	address=${station_addresses[$((index + 1))]}
	phy=$(basename "$(readlink -f "/sys/class/net/$interface/phy80211")")

	ip netns add "$namespace"
	created_namespaces+=("$namespace")
	iw phy "$phy" set netns name "$namespace"
	ip -n "$namespace" link set dev lo up
	ip -n "$namespace" address add "$address" dev "$interface"
done

ip link set dev sta0 up

# An active monitor is the sole interface on this hwsim radio.  Verify that
# mac80211 assigned the channel to that interface instead of only updating the
# shared virtual-monitor request.  Without the corresponding kernel fix, the
# set command succeeds but `iw dev sta0 info` reports no channel and scans stay
# on the initial channel.
if iw dev sta0 set channel 6 \
		&& iw dev sta0 info | grep -Eq '^[[:space:]]*channel 6 \(2437 MHz\)'; then
	echo "environment setup: verified channel switching on the active sta0 monitor"
else
	echo "environment setup: active-monitor channel switching is broken; you need a kernel with this bug fixed" >&2
	exit 1
fi
iw dev sta0 set channel 1

complete=true
trap - EXIT
echo "environment setup: created sta0 sta1 sta2 ap0 ap1 ap2 ap3"
echo "environment setup: assigned 192.0.2.1/24 through 192.0.2.3/24 to sta0 through sta2"
echo "environment setup: placed sta1 and sta2 in u80211-sta1 and u80211-sta2"
