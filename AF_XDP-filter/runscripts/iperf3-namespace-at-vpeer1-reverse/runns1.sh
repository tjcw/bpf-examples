#!/bin/bash -x
# SPDX-License-Identifier: GPL-2.0
# Server side helper script for TCP performance testing with eBPF filter
# Set FILTER env var to af_xdp_kern or af_xdp_kern_passall according to which filter to use
# Set LEAVE env var non-null for baseline test with no eBPF filter
# Set TCPDUMP env var non-null to take tcpdumps of the interfaces
ip link set lo up
ip link set vpeer1 up
ip addr add 10.10.0.10/16 dev vpeer1
ip link set dev vpeer1 xdpgeneric off
ethtool -K vpeer1 tx off
if [[ -n "${TCPDUMP}" ]]
then
  tcpdump -v -i vpeer1 -w vpeer1.tcpdump not ip6 &
  tcpdump_vpeer1_pid=$!
fi

iperf3 -s -p ${PORT} &
iperf3_pid=$!
sleep 80
kill -INT ${iperf3_pid}
if [[ -n "${TCPDUMP}" ]]
then
  kill -INT ${tcpdump_vpeer1_pid}
fi
wait
