#!/usr/bin/env bash
# Requires: ssh key-based login from DPU-1 → DPU-0 as user $SSH_USER

set -Eeuo pipefail

### ---- editable parameters --------------------------------------------
DPU0_IP_DATA="192.168.50.2"      # p0 on DPU-0
DPU1_IP_DATA="192.168.50.1"      # p0 on DPU-1
HOST_IP_DATA="192.168.100.1"     # tmfifo_net on host
DPU1_IP_PCIE="192.168.100.2"     # tmfifo_net on DPU1
SSH_USER="ubuntu"
HOST_SSH_USER="dimitrios-ldap"
IPERF_PORT=5201
DOCA_PORT=12345
DURATION_SEC=0                   # 0 = run forever, Ctrl-C to stop
PAR_BITRATE="10gpbs"
LAT_BITRATE="2.25gbps"
CPU_MASK_IPERF="2"                  # pin iperf to core 2
DOCA_PCI_LOCAL="03:00.0"            # in-DPU pcie address
DOCA_PCI_DPU1="87:00.0"             # in-Host pcie address
DOCA_CORE_MASK="4"                  # core for doca_bench
DOCA_CORE_TASK_MASK="5"             # core for the task in doca_bench
RAMLOG_DIR="/dev/shm/iperf_bench"
### ---------------------------------------------------------------------

TIMESTAMP=$(date +%Y%m%d-%H%M%S)
rm -rf "$RAMLOG_DIR"
mkdir -p "$RAMLOG_DIR"

SERVER_LOG_TPUT="${RAMLOG_DIR}/iperf_srv_dma_throughput_${TIMESTAMP}.json"
SERVER_LOG_LATENCY="${RAMLOG_DIR}/iperf_srv_dma_latency_${TIMESTAMP}.json"
CLI_PID_FILE="${RAMLOG_DIR}/iperf_cli.pid"
DOCA_LOG="${RAMLOG_DIR}/doca_dma_${TIMESTAMP}.csv"
DOCA_COMP_STR="proto=tcp,user=${SSH_USER},port=${DOCA_PORT},addr=${DPU1_IP_PCIE},dev=${DOCA_PCI_LOCAL},rep=${DOCA_PCI_DPU1}"

#1 ---------- start iperf3 server on DPU-1 ----------
echo "[`date`]   starting iperf3 server on DPU-1"
setsid taskset -c "$CPU_MASK_IPERF" \
  iperf3 -s -B "$DPU1_IP_DATA" \
         -p "$IPERF_PORT" \
         -i 1 \
         --json \
         --logfile "$SERVER_LOG_TPUT" &
SRV_PGID=$!          # pgid == wrapper PID

#2 ---------- start iperf3 client on DPU-0 (throughput) ----------
echo "[`date`]   ssh into DPU-0 and start iperf3 in TCP"
sleep 1
ssh -n -f -o BatchMode=yes -o StrictHostKeyChecking=accept-new \
  "$SSH_USER@$DPU0_IP_DATA" \
  "mkdir -p $RAMLOG_DIR && \
    nohup taskset -c $CPU_MASK_IPERF \
     iperf3 -c $DPU1_IP_DATA -B $DPU0_IP_DATA \
            -p $IPERF_PORT -t 0 --bitrate $PAR_BITRATE -i 1 \
            > /dev/null 2>&1 < /dev/null & echo \$! > $CLI_PID_FILE"

#3 ---------- start dma workload on DPU-1 ----------
echo "[`date`]  doca_bench dma → cores $DOCA_CORE_MASK (device $DOCA_PCI_DPU1)"
sleep 4
ssh -n -f -o BatchMode=yes -o StrictHostKeyChecking=accept-new \
    "$HOST_SSH_USER@$HOST_IP_DATA" \
    "mkdir -p $RAMLOG_DIR && \
    nohup taskset -c $DOCA_CORE_MASK \
      /opt/mellanox/doca/tools/doca_bench \
        --mode throughput \
        --pipeline-steps doca_dma \
        --device $DOCA_PCI_DPU1 \
        --data-provider random-data \
        --uniform-job-size 2097152 \
        --job-output-buffer-size 2097152 \
        --use-remote-output-buffers \
        --companion-connection-string $DOCA_COMP_STR \
        --core-count 1 \
        --core-list $DOCA_CORE_MASK \
        --run-limit-seconds 15 \
        --csv-output-file $DOCA_LOG \
        > /dev/null 2>&1 < /dev/null &"
echo "[`date`]  issued all commands, sleeping..."
sleep 20  # for warmup tasks

#4 ---------- end workload and cleanup ----------
echo "[`date`]  doca_bench dma should have finished"
echo "[`date`]  terminating iperf3 (throughput) locally..."
# kill server pgid (wrapper + iperf3)
kill "$SRV_PGID" 2>/dev/null || true

#5 ---------- kill iperf3 client on DPU-0 ----------
echo "[`date`]  terminating iperf3 (throughput) remotely..."
ssh -o BatchMode=yes "$SSH_USER@$DPU0_IP_DATA" \
    "test -f $CLI_PID_FILE && kill \$(cat $CLI_PID_FILE) || true" || true
sleep 1

#6 ---------- start iperf3 server on DPU-1 (latency RTT) ----------
echo "[`date`]   starting iperf3 server on DPU-1"
setsid taskset -c "$CPU_MASK_IPERF" \
  iperf3 -s -B "$DPU1_IP_DATA" \
         -p "$IPERF_PORT" \
         -i 1 \
         --json \
         --logfile "$SERVER_LOG_LATENCY" &
SRV_PGID=$!          # pgid == wrapper PID

#7 ---------- start iperf3 client on DPU-0 (latency RTT) ----------
echo "[`date`]   ssh into DPU-0 and start iperf3 in UDP"
sleep 1
ssh -n -f -o BatchMode=yes -o StrictHostKeyChecking=accept-new \
  "$SSH_USER@$DPU0_IP_DATA" \
  "mkdir -p $RAMLOG_DIR && \
    nohup taskset -c $CPU_MASK_IPERF \
     iperf3 -c $DPU1_IP_DATA -B $DPU0_IP_DATA \
            -p $IPERF_PORT -t 0 --bitrate $LAT_BITRATE -i 1 -u \
            > /dev/null 2>&1 < /dev/null & echo \$! > $CLI_PID_FILE"

#8 ---------- start dma workload on DPU-1 ----------
echo "[`date`]  doca_bench dma → cores $DOCA_CORE_MASK (device $DOCA_PCI_DPU1)"
sleep 4
ssh -n -f -o BatchMode=yes -o StrictHostKeyChecking=accept-new \
    "$HOST_SSH_USER@$HOST_IP_DATA" \
    "mkdir -p $RAMLOG_DIR && \
    nohup taskset -c $DOCA_CORE_MASK \
      /opt/mellanox/doca/tools/doca_bench \
        --mode throughput \
        --pipeline-steps doca_dma \
        --device $DOCA_PCI_DPU1 \
        --data-provider random-data \
        --uniform-job-size 2097152 \
        --job-output-buffer-size 2097152 \
        --use-remote-output-buffers \
        --companion-connection-string $DOCA_COMP_STR \
        --core-count 1 \
        --core-list $DOCA_CORE_MASK \
        --run-limit-seconds 15 \
        --csv-output-file $DOCA_LOG \
        > /dev/null 2>&1 < /dev/null &"
echo "[`date`]  issued all commands, sleeping..."
sleep 20  # for warmup tasks

#9 ---------- end workload and cleanup ----------
echo "[`date`]  doca_bench dma should have finished"
echo "[`date`]  terminating iperf3 (latency RTT) locally..."
# kill server pgid (wrapper + iperf3)
kill "$SRV_PGID" 2>/dev/null || true

#10 ---------- kill iperf3 client on DPU-0 ----------
echo "[`date`]  terminating iperf3 (latency RTT) remotely..."
ssh -o BatchMode=yes "$SSH_USER@$DPU0_IP_DATA" \
    "test -f $CLI_PID_FILE && kill \$(cat $CLI_PID_FILE) || true" || true

mv $SERVER_LOG_TPUT results/bf3
mv $SERVER_LOG_LATENCY results/bf3
echo "[`date`]   logs in `pwd`/results/bf3, goodbye."
