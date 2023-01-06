#!/bin/bash -x
# SPDX-License-Identifier: GPL-2.0
# Run script for eBPF TCP performance testing between 2 real machines
# Run this on the server. Assumes server IP address is ${SERVER_IP}
# Assumes client is reachable on another network as ${CLIENT_IP}
# Arranged for a Pensando NIC which uses 16 queues.
# Set FILTER env var to af_xdp_kern or af_xdp_kern_passall according to which filter to use
# Set LEAVE env var non-null for baseline test with no eBPF filter
# Set PORT to the port number netserver should listen on

function sequence_tests
{
  ssh -n ${CLIENT_IP} netperf -4 -t TCP_RR -H ${SERVER_IP} -p ${PORT} -- -D 2>&1 | tee client_rr.log
  ssh -n ${CLIENT_IP} netperf -4 -t TCP_STREAM -H ${SERVER_IP} -p ${PORT} -- -D 2>&1 | tee client_stream.log
  ssh -n ${CLIENT_IP} netperf -4 -t TCP_CRR -H ${SERVER_IP} -p ${PORT} -- -D 2>&1 | tee client_crr.log
}

ip link set dev enp25s0 xdpgeneric off
rm -f /sys/fs/bpf/accept_map /sys/fs/bpf/xdp_stats_map
ip tuntap add mode tun tun0
ip link set dev tun0 down
ip addr add ${TUN_IP}/24 dev tun0
ip link set dev tun0 up
if [[ -z "${LEAVE}" ]]
then 
  for device in /proc/sys/net/ipv4/conf/*
  do
    echo 0 >${device}/rp_filter
  done
  cd ../..
  ./af_xdp_user -S -d enp25s0 -Q 16 --filename ./${FILTER}.o &
  real_pid=$!
  netserver -p ${PORT} -4 -D -f &
  netserver_pid=$!
  sequence_tests
  kill -INT ${real_pid}
  kill -INT ${netserver_pid} 
  for device in /proc/sys/net/ipv4/conf/*
  do
    echo 2 >${device}/rp_filter
  done
else
  netserver -p ${PORT} -4 -D -f &
  netserver_pid=$!
  sequence_tests
  kill -INT ${netserver_pid}
fi
wait

