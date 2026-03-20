#!/usr/bin/env bash
set -euo pipefail

HOST_IP="192.168.100.1/24"
BF_IP="192.168.100.2"
KNOWN_HOSTS="/home/dimitrios-ldap/.ssh/known_hosts"

assign_iface() {
    local iface="$1"

    echo "[*] Flushing ${HOST_IP} from tmfifo_net0 and tmfifo_net1 if present"
    sudo ip addr del "${HOST_IP}" dev tmfifo_net0 2>/dev/null || true
    sudo ip addr del "${HOST_IP}" dev tmfifo_net1 2>/dev/null || true

    echo "[*] Bringing ${iface} up and assigning ${HOST_IP}"
    sudo ip link set "${iface}" up
    sudo ip addr add "${HOST_IP}" dev "${iface}"

    echo "[*] Removing stale SSH host key for ${BF_IP}"
    ssh-keygen -f "${KNOWN_HOSTS}" -R "${BF_IP}" >/dev/null 2>&1 || true
}

unassign_all() {
    echo "[*] Removing ${HOST_IP} from tmfifo_net0 and tmfifo_net1"
    sudo ip addr del "${HOST_IP}" dev tmfifo_net0 2>/dev/null || true
    sudo ip addr del "${HOST_IP}" dev tmfifo_net1 2>/dev/null || true
}

ssh_bf() {
    local cmd="${1:-}"
    if [[ -z "${cmd}" ]]; then
        ssh bf-pcie
    else
        ssh bf-pcie "${cmd}"
    fi
}

echo "[1/7] Unassigning all tmfifo_net IPs..."
unassign_all
sleep 1

echo "[2/7] Assigning ${HOST_IP} to tmfifo_net0..."
assign_iface tmfifo_net0
sleep 3

echo "[3/7] Ready to SSH to first BlueField via bf-pcie"
ssh_bf "sudo ip addr replace 192.168.50.2/24 dev ovsbr1"
ssh_bf "sudo ip link set ovsbr1 up"

echo "[4/7] Unassigning ${HOST_IP}..."
unassign_all
sleep 1

echo "[5/7] Assigning ${HOST_IP} to tmfifo_net1..."
assign_iface tmfifo_net1
sleep 3

echo "[6/7] Ready to SSH to second BlueField via bf-pcie"
ssh_bf "sudo ip addr replace 192.168.50.1/24 dev ovsbr1"
ssh_bf "sudo ip link set ovsbr1 up"
sleep 2

echo "[7/7] Verify assignments worked (ping DPU1 from DPU0)"
ssh_bf "ping -c 5 192.168.50.2"