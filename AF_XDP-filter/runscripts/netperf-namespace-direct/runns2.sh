#!/bin/bash -x
# SPDX-License-Identifier: GPL-2.0
# Server side helper script for TCP performance testing with eBPF filter
# Set FILTER env var to af_xdp_kern or af_xdp_kern_passall according to which filter to use
# Set LEAVE env var non-null for baseline test with no eBPF filter
# Set TCPDUMP env var non-null to take tcpdumps of the interfaces
# Set PORT env var to select the port for netserver to listen on
ip link set lo up
ip link set vpeer2 up
ip addr add 10.10.0.20/16 dev vpeer2
ip link set dev vpeer2 xdpgeneric off
if [[ -n "${TCPDUMP}" ]]
then
  tcpdump -i vpeer2 -w vpeer2.tcpdump &
  tcpdump_vpeer2_pid=$!
fi

netserver -p ${PORT} -4 -D -f &
netserver_pid=$!
sleep 60
kill -INT ${netserver_pid}  
wait
if [[ -n "${TCPDUMP}" ]]
then
  kill -INT ${tcpdump_vpeer2_pid}
fi
wait
