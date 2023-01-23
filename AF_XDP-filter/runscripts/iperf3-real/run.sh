#!/bin/bash -x
# SPDX-License-Identifier: GPL-2.0
# Run script for eBPF TCP performance testing between 2 real machines
# Run this on the server. Assumes server IP address is ${SERVER_IP}
# Assumes client is reachable on another network as ${CLIENT_IP}
# Arranged for a Pensando NIC which uses 16 queues.
# Set FILTER env var to af_xdp_kern or af_xdp_kern_passall according to which filter to use
# Set LEAVE env var non-null for baseline test with no eBPF filter
# Set PORT to choose a port for the server to listen on
# Set TUN_IP, CLIENT_IP, and SERVER_IP as required

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
  sleep 2
  iperf3 -s -p ${PORT}  &
  iperf3_pid=$!
  sleep 2
  ssh ${CLIENT_IP} iperf3 -c ${SERVER_IP} -p ${PORT} | tee client.log
  kill -INT ${iperf3_pid} ${real_pid}
  for device in /proc/sys/net/ipv4/conf/*
  do
    echo 2 >${device}/rp_filter
  done
else
  iperf3 -s -p ${PORT} &
  iperf3_pid=$!
  ssh ${CLIENT_IP} iperf3 -c ${SERVER_IP} -p ${PORT} | tee client.log
  kill -INT ${iperf3_pid}
fi
wait

